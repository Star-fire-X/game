#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "rocksdb/db.h"
#include "rocksdb/utilities/db_ttl.h"
#include "storage_engine/l2/rocksdb_cache.h"

namespace mir2::storage_engine::l2 {
namespace {

constexpr const char* kCfDataPersistent = "cf_data_persistent";
constexpr const char* kCfDataTtl = "cf_data_ttl";
constexpr const char* kCfOutbox = "cf_outbox";
constexpr const char* kCfDeadLetter = "cf_dead_letter";
constexpr const char* kCfMeta = "cf_meta";
constexpr const char* kSchemaVersionKey = "__storage_schema_version__";
constexpr const char* kEncryptionEnv = "MIR2_TEST_STORAGE_ENCRYPTION_KEYS";

std::optional<std::string> GetEnvVar(const std::string& name) {
  const char* value = std::getenv(name.c_str());
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::string(value);
}

void SetEnvVar(const std::string& name, const std::string& value) {
#if defined(_WIN32)
  _putenv_s(name.c_str(), value.c_str());
#else
  setenv(name.c_str(), value.c_str(), 1);
#endif
}

void UnsetEnvVar(const std::string& name) {
#if defined(_WIN32)
  _putenv_s(name.c_str(), "");
#else
  unsetenv(name.c_str());
#endif
}

class ScopedEnvVar {
 public:
  ScopedEnvVar(std::string name, std::string value)
      : name_(std::move(name)),
        old_value_(GetEnvVar(name_)),
        had_old_value_(old_value_.has_value()) {
    SetEnvVar(name_, value);
  }

  ~ScopedEnvVar() {
    if (had_old_value_) {
      SetEnvVar(name_, *old_value_);
      return;
    }
    UnsetEnvVar(name_);
  }

 private:
  std::string name_;
  std::optional<std::string> old_value_;
  bool had_old_value_ = false;
};

bool ContainsBytes(const std::string& haystack, const std::vector<uint8_t>& needle) {
  if (needle.empty()) {
    return true;
  }
  const auto it = std::search(haystack.begin(), haystack.end(),
                              needle.begin(), needle.end());
  return it != haystack.end();
}

std::vector<uint8_t> MakePayload(size_t bytes, uint8_t seed) {
  std::vector<uint8_t> payload(bytes, seed);
  for (size_t i = 0; i < bytes; ++i) {
    payload[i] = static_cast<uint8_t>((seed + i) & 0xFF);
  }
  return payload;
}

bool BenchmarkOnlyEnabled() {
  const char* env = std::getenv("LEGEND2_BENCHMARK_ONLY");
  if (!env) {
    return false;
  }

  const std::string value(env);
  return value == "1" || value == "true" || value == "TRUE";
}

void DestroyHandles(rocksdb::DBWithTTL* db,
                    const std::vector<rocksdb::ColumnFamilyHandle*>& handles) {
  if (!db) {
    return;
  }
  for (auto* handle : handles) {
    if (handle) {
      db->DestroyColumnFamilyHandle(handle);
    }
  }
}

std::vector<std::string> ListColumnFamilyNames(const std::string& path) {
  std::vector<std::string> names;
  rocksdb::Status status =
      rocksdb::DB::ListColumnFamilies(rocksdb::DBOptions{}, path, &names);
  if (!status.ok()) {
    return {};
  }
  return names;
}

std::optional<std::string> ReadSchemaVersionMarker(const std::string& path) {
  std::vector<rocksdb::ColumnFamilyDescriptor> descriptors{
      {rocksdb::kDefaultColumnFamilyName, rocksdb::ColumnFamilyOptions{}},
      {kCfDataPersistent, rocksdb::ColumnFamilyOptions{}},
      {kCfDataTtl, rocksdb::ColumnFamilyOptions{}},
      {kCfOutbox, rocksdb::ColumnFamilyOptions{}},
      {kCfDeadLetter, rocksdb::ColumnFamilyOptions{}},
      {kCfMeta, rocksdb::ColumnFamilyOptions{}},
  };
  std::vector<int32_t> ttls{3600, 0, 3600, 0, 0, 0};
  std::vector<rocksdb::ColumnFamilyHandle*> handles;
  rocksdb::DBWithTTL* raw_db = nullptr;
  rocksdb::Status status = rocksdb::DBWithTTL::Open(
      rocksdb::DBOptions{}, path, descriptors, &handles, &raw_db, ttls, true);
  if (!status.ok()) {
    return std::nullopt;
  }
  if (handles.size() < 6) {
    DestroyHandles(raw_db, handles);
    delete raw_db;
    return std::nullopt;
  }

  std::unique_ptr<rocksdb::DBWithTTL> db(raw_db);
  std::string marker;
  status = db->Get(rocksdb::ReadOptions(), handles[5], kSchemaVersionKey, &marker);
  DestroyHandles(db.get(), handles);
  if (!status.ok()) {
    return std::nullopt;
  }
  return marker;
}

std::optional<std::string> ReadRawDataValue(const std::string& path,
                                            const std::string& key,
                                            const std::string& cf_name) {
  std::vector<rocksdb::ColumnFamilyDescriptor> descriptors{
      {rocksdb::kDefaultColumnFamilyName, rocksdb::ColumnFamilyOptions{}},
      {kCfDataPersistent, rocksdb::ColumnFamilyOptions{}},
      {kCfDataTtl, rocksdb::ColumnFamilyOptions{}},
      {kCfOutbox, rocksdb::ColumnFamilyOptions{}},
      {kCfDeadLetter, rocksdb::ColumnFamilyOptions{}},
      {kCfMeta, rocksdb::ColumnFamilyOptions{}},
  };
  std::vector<int32_t> ttls{3600, 0, 3600, 0, 0, 0};
  std::vector<rocksdb::ColumnFamilyHandle*> handles;
  rocksdb::DBWithTTL* raw_db = nullptr;
  rocksdb::Status status = rocksdb::DBWithTTL::Open(
      rocksdb::DBOptions{}, path, descriptors, &handles, &raw_db, ttls, true);
  if (!status.ok()) {
    return std::nullopt;
  }
  std::unique_ptr<rocksdb::DBWithTTL> db(raw_db);

  rocksdb::ColumnFamilyHandle* target = nullptr;
  for (auto* handle : handles) {
    if (handle != nullptr && handle->GetName() == cf_name) {
      target = handle;
      break;
    }
  }
  if (target == nullptr) {
    DestroyHandles(db.get(), handles);
    return std::nullopt;
  }

  std::string value;
  status = db->Get(rocksdb::ReadOptions{}, target, key, &value);
  DestroyHandles(db.get(), handles);
  if (!status.ok()) {
    return std::nullopt;
  }
  return value;
}

class RocksDBCacheP1Test : public ::testing::Test {
 protected:
  void SetUp() override {
    db_path_ =
        "/tmp/mir2_storage_engine_l2_p1_" +
        std::to_string(std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count());

    config_.db_path = db_path_;
    config_.block_cache_size = 16 * 1024 * 1024;
    config_.ttl_seconds = 3600;
    config_.max_version_persist_step = 64;

    cache_ = std::make_unique<RocksDBCache>(config_);
    ASSERT_TRUE(cache_->Initialize());
  }

  void TearDown() override {
    cache_.reset();
    std::error_code ec;
    std::filesystem::remove_all(db_path_, ec);
  }

  static VersionedData MakeVersionedData(uint64_t version, uint8_t value) {
    return VersionedData{
        .version = version,
        .data = std::vector<uint8_t>{value},
        .timestamp_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count())};
  }

  RocksDBCache::Config config_;
  std::unique_ptr<RocksDBCache> cache_;
  std::string db_path_;
};

TEST_F(RocksDBCacheP1Test, MaxVersionTracksHighestWrite) {
  ASSERT_TRUE(cache_->Set("v100", MakeVersionedData(100, 1)));
  ASSERT_TRUE(cache_->Set("v200", MakeVersionedData(200, 2)));
  ASSERT_TRUE(cache_->Set("v150", MakeVersionedData(150, 3)));
  EXPECT_EQ(cache_->GetMaxVersion(), 200U);
}

TEST_F(RocksDBCacheP1Test, BatchSetTracksHighestVersion) {
  std::vector<std::pair<std::string, VersionedData>> batch{
      {"k1", MakeVersionedData(10, 1)},
      {"k2", MakeVersionedData(80, 2)},
      {"k3", MakeVersionedData(30, 3)}};
  ASSERT_TRUE(cache_->BatchSet(batch));
  EXPECT_EQ(cache_->GetMaxVersion(), 80U);
}

TEST_F(RocksDBCacheP1Test, DestructorFlushesMaxVersionEvenWhenPersistStepNotReached) {
  cache_.reset();

  config_.max_version_persist_step = 1000;
  cache_ = std::make_unique<RocksDBCache>(config_);
  ASSERT_TRUE(cache_->Initialize());

  ASSERT_TRUE(cache_->Set("key", MakeVersionedData(10, 7)));
  ASSERT_EQ(cache_->GetMaxVersion(), 10U);

  cache_.reset();

  auto reopened = std::make_unique<RocksDBCache>(config_);
  ASSERT_TRUE(reopened->Initialize());
  EXPECT_EQ(reopened->GetMaxVersion(), 10U);
}

TEST_F(RocksDBCacheP1Test, InitializesRequiredColumnFamiliesAndSchemaMarker) {
  auto names = ListColumnFamilyNames(db_path_);
  ASSERT_FALSE(names.empty());

  EXPECT_NE(std::find(names.begin(), names.end(), rocksdb::kDefaultColumnFamilyName),
            names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), kCfDataPersistent), names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), kCfDataTtl), names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), kCfOutbox), names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), kCfDeadLetter), names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), kCfMeta), names.end());

  auto marker = ReadSchemaVersionMarker(db_path_);
  ASSERT_TRUE(marker.has_value());
  EXPECT_EQ(*marker, "multi_cf_v1");
}

TEST_F(RocksDBCacheP1Test, SchemaMarkerPersistsAcrossRestart) {
  cache_.reset();
  cache_ = std::make_unique<RocksDBCache>(config_);
  ASSERT_TRUE(cache_->Initialize());

  auto marker = ReadSchemaVersionMarker(db_path_);
  ASSERT_TRUE(marker.has_value());
  EXPECT_EQ(*marker, "multi_cf_v1");
}

TEST_F(RocksDBCacheP1Test, OutboxAppendReplayAckIsIdempotent) {
  uint64_t id1 = 0;
  uint64_t id2 = 0;
  ASSERT_TRUE(cache_->AppendOutbox(
      "player:1", MakeVersionedData(101, 1), Priority::HIGH, &id1));
  ASSERT_TRUE(cache_->AppendOutbox(
      "player:2", MakeVersionedData(202, 2), Priority::LOW, &id2));
  ASSERT_GT(id1, 0U);
  ASSERT_EQ(id2, id1 + 1);
  EXPECT_EQ(cache_->OutboxDepth(), 2U);

  std::vector<RocksDBCache::OutboxEntry> replayed;
  const size_t replayed_count = cache_->ReplayOutbox(
      100, [&replayed](const RocksDBCache::OutboxEntry& entry) {
        replayed.push_back(entry);
        return true;
      });
  ASSERT_EQ(replayed_count, 2U);
  ASSERT_EQ(replayed.size(), 2U);
  EXPECT_EQ(replayed[0].outbox_id, id1);
  EXPECT_EQ(replayed[0].key, "player:1");
  EXPECT_EQ(replayed[0].priority, Priority::HIGH);
  EXPECT_EQ(replayed[1].outbox_id, id2);
  EXPECT_EQ(replayed[1].key, "player:2");
  EXPECT_EQ(replayed[1].priority, Priority::LOW);

  ASSERT_TRUE(cache_->AckOutbox(id1));
  ASSERT_TRUE(cache_->AckOutbox(id1));  // idempotent ack
  EXPECT_EQ(cache_->OutboxDepth(), 1U);

  replayed.clear();
  const size_t remaining = cache_->ReplayOutbox(
      100, [&replayed](const RocksDBCache::OutboxEntry& entry) {
        replayed.push_back(entry);
        return true;
      });
  ASSERT_EQ(remaining, 1U);
  ASSERT_EQ(replayed.size(), 1U);
  EXPECT_EQ(replayed[0].outbox_id, id2);
}

TEST_F(RocksDBCacheP1Test, OutboxNextIdPersistsAcrossRestart) {
  uint64_t first_id = 0;
  ASSERT_TRUE(cache_->AppendOutbox(
      "persist:1", MakeVersionedData(1, 9), Priority::NORMAL, &first_id));

  cache_.reset();
  cache_ = std::make_unique<RocksDBCache>(config_);
  ASSERT_TRUE(cache_->Initialize());

  uint64_t second_id = 0;
  ASSERT_TRUE(cache_->AppendOutbox(
      "persist:2", MakeVersionedData(2, 8), Priority::NORMAL, &second_id));
  EXPECT_EQ(second_id, first_id + 1);

  std::vector<uint64_t> ids;
  const size_t replayed = cache_->ReplayOutbox(
      100, [&ids](const RocksDBCache::OutboxEntry& entry) {
        ids.push_back(entry.outbox_id);
        return true;
      });
  ASSERT_EQ(replayed, 2U);
  ASSERT_EQ(ids.size(), 2U);
  EXPECT_EQ(ids[0], first_id);
  EXPECT_EQ(ids[1], second_id);
}

TEST_F(RocksDBCacheP1Test, ReplayOutboxHonorsLimit) {
  for (uint64_t i = 0; i < 3; ++i) {
    uint64_t outbox_id = 0;
    ASSERT_TRUE(cache_->AppendOutbox(
        "limit:" + std::to_string(i),
        MakeVersionedData(10 + i, static_cast<uint8_t>(i)),
        Priority::NORMAL, &outbox_id));
  }

  std::vector<std::string> keys;
  const size_t replayed = cache_->ReplayOutbox(
      2, [&keys](const RocksDBCache::OutboxEntry& entry) {
        keys.push_back(entry.key);
        return true;
      });
  ASSERT_EQ(replayed, 2U);
  ASSERT_EQ(keys.size(), 2U);
  EXPECT_EQ(keys[0], "limit:0");
  EXPECT_EQ(keys[1], "limit:1");
}

TEST_F(RocksDBCacheP1Test, DeadLetterAppendReplayAckRoundTrip) {
  RocksDBCache::DeadLetterEntry entry{
      .dead_letter_id = 0,
      .key = "dead:key:1",
      .data = MakeVersionedData(77, 7),
      .priority = Priority::HIGH,
      .attempts = 3,
      .durable_outbox_id = 15,
      .recorded_at_ms = 123456,
      .error_message = "forced failure"};

  uint64_t dead_letter_id = 0;
  ASSERT_TRUE(cache_->AppendDeadLetter(entry, &dead_letter_id));
  ASSERT_GT(dead_letter_id, 0U);
  EXPECT_EQ(cache_->DeadLetterDepth(), 1U);

  std::vector<RocksDBCache::DeadLetterEntry> replayed;
  const size_t replayed_count = cache_->ReplayDeadLetter(
      100, [&replayed](const RocksDBCache::DeadLetterEntry& replay_entry) {
        replayed.push_back(replay_entry);
        return true;
      });
  ASSERT_EQ(replayed_count, 1U);
  ASSERT_EQ(replayed.size(), 1U);
  EXPECT_EQ(replayed[0].dead_letter_id, dead_letter_id);
  EXPECT_EQ(replayed[0].key, "dead:key:1");
  EXPECT_EQ(replayed[0].data.version, 77U);
  EXPECT_EQ(replayed[0].priority, Priority::HIGH);
  EXPECT_EQ(replayed[0].attempts, 3U);
  EXPECT_EQ(replayed[0].durable_outbox_id, 15U);
  EXPECT_EQ(replayed[0].recorded_at_ms, 123456U);
  EXPECT_EQ(replayed[0].error_message, "forced failure");

  ASSERT_TRUE(cache_->AckDeadLetter(dead_letter_id));
  ASSERT_TRUE(cache_->AckDeadLetter(dead_letter_id));  // idempotent ack
  EXPECT_EQ(cache_->DeadLetterDepth(), 0U);
}

TEST_F(RocksDBCacheP1Test, TombstoneGcAppendReplayAckRoundTrip) {
  uint64_t id1 = 0;
  uint64_t id2 = 0;
  ASSERT_TRUE(cache_->AppendTombstoneGcEntry(
      "tombstone:key:1", 101, 1000, &id1));
  ASSERT_TRUE(cache_->AppendTombstoneGcEntry(
      "tombstone:key:2", 202, 5000, &id2));
  ASSERT_GT(id1, 0U);
  ASSERT_EQ(id2, id1 + 1);
  EXPECT_EQ(cache_->TombstoneGcDepth(), 2U);

  std::vector<RocksDBCache::TombstoneGcEntry> replayed;
  const size_t replayed_count = cache_->ReplayDueTombstoneGc(
      100, 2000, [&replayed](const RocksDBCache::TombstoneGcEntry& entry) {
        replayed.push_back(entry);
        return true;
      });
  ASSERT_EQ(replayed_count, 1U);
  ASSERT_EQ(replayed.size(), 1U);
  EXPECT_EQ(replayed[0].tombstone_gc_id, id1);
  EXPECT_EQ(replayed[0].key, "tombstone:key:1");
  EXPECT_EQ(replayed[0].delete_version, 101U);
  EXPECT_EQ(replayed[0].due_at_ms, 1000U);

  ASSERT_TRUE(cache_->AckTombstoneGcEntry(id1));
  ASSERT_TRUE(cache_->AckTombstoneGcEntry(id1));
  EXPECT_EQ(cache_->TombstoneGcDepth(), 1U);
}

TEST_F(RocksDBCacheP1Test, DataTierIsolationRoutesDataToExpectedColumnFamily) {
  const VersionedData ttl_data = MakeVersionedData(11, 1);
  const VersionedData persistent_data = MakeVersionedData(22, 2);

  ASSERT_TRUE(cache_->Set(
      "tier:ttl", ttl_data, RocksDBCache::DataTier::kTtl));
  ASSERT_TRUE(cache_->Set(
      "tier:persistent", persistent_data,
      RocksDBCache::DataTier::kPersistent));

  auto ttl_hit = cache_->Get("tier:ttl", RocksDBCache::DataTier::kTtl);
  ASSERT_TRUE(ttl_hit.has_value());
  EXPECT_EQ(ttl_hit->version, ttl_data.version);
  EXPECT_FALSE(
      cache_->Get("tier:ttl", RocksDBCache::DataTier::kPersistent).has_value());

  auto persistent_hit =
      cache_->Get("tier:persistent", RocksDBCache::DataTier::kPersistent);
  ASSERT_TRUE(persistent_hit.has_value());
  EXPECT_EQ(persistent_hit->version, persistent_data.version);
  EXPECT_FALSE(
      cache_->Get("tier:persistent", RocksDBCache::DataTier::kTtl).has_value());

  std::vector<std::string> persistent_keys;
  cache_->ForEach(
      [&persistent_keys](const std::string& key, const VersionedData&) {
        persistent_keys.push_back(key);
        return true;
      },
      RocksDBCache::DataTier::kPersistent);
  ASSERT_EQ(persistent_keys.size(), 1U);
  EXPECT_EQ(persistent_keys[0], "tier:persistent");

  std::vector<std::string> ttl_keys;
  cache_->ForEach(
      [&ttl_keys](const std::string& key, const VersionedData&) {
        ttl_keys.push_back(key);
        return true;
      },
      RocksDBCache::DataTier::kTtl);
  ASSERT_EQ(ttl_keys.size(), 1U);
  EXPECT_EQ(ttl_keys[0], "tier:ttl");
}

TEST_F(RocksDBCacheP1Test, StrictTtlReadsTreatExpiredTtlEntryAsMiss) {
  cache_.reset();
  config_.ttl_seconds = 1;
  config_.strict_ttl_reads = true;
  cache_ = std::make_unique<RocksDBCache>(config_);
  ASSERT_TRUE(cache_->Initialize());

  const uint64_t now_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  const VersionedData expired{
      .version = 1,
      .data = std::vector<uint8_t>{7},
      .timestamp_ms = now_ms > 5000 ? now_ms - 5000 : 1};
  ASSERT_TRUE(cache_->Set("ttl:expired", expired, RocksDBCache::DataTier::kTtl));
  EXPECT_FALSE(
      cache_->Get("ttl:expired", RocksDBCache::DataTier::kTtl).has_value());
}

TEST_F(RocksDBCacheP1Test, NonStrictTtlReadsCanSeeExpiredTtlEntryBeforeCompaction) {
  cache_.reset();
  config_.ttl_seconds = 1;
  config_.strict_ttl_reads = false;
  cache_ = std::make_unique<RocksDBCache>(config_);
  ASSERT_TRUE(cache_->Initialize());

  const uint64_t now_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  const VersionedData expired{
      .version = 2,
      .data = std::vector<uint8_t>{8},
      .timestamp_ms = now_ms > 5000 ? now_ms - 5000 : 1};
  ASSERT_TRUE(cache_->Set("ttl:expired:legacy", expired,
                          RocksDBCache::DataTier::kTtl));
  auto hit = cache_->Get("ttl:expired:legacy", RocksDBCache::DataTier::kTtl);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->version, 2U);
}

TEST_F(RocksDBCacheP1Test, DeleteByPrefixRemovesOnlyMatchingTierKeys) {
  ASSERT_TRUE(cache_->Set("pref:ttl:1", MakeVersionedData(1, 1),
                          RocksDBCache::DataTier::kTtl));
  ASSERT_TRUE(cache_->Set("pref:ttl:2", MakeVersionedData(2, 2),
                          RocksDBCache::DataTier::kTtl));
  ASSERT_TRUE(cache_->Set("pref:persistent:1", MakeVersionedData(3, 3),
                          RocksDBCache::DataTier::kPersistent));
  ASSERT_TRUE(cache_->Set("other:ttl:1", MakeVersionedData(4, 4),
                          RocksDBCache::DataTier::kTtl));

  EXPECT_EQ(
      cache_->DeleteByPrefix("pref:ttl:", RocksDBCache::DataTier::kTtl, 1), 2U);
  EXPECT_FALSE(cache_->Get("pref:ttl:1", RocksDBCache::DataTier::kTtl).has_value());
  EXPECT_FALSE(cache_->Get("pref:ttl:2", RocksDBCache::DataTier::kTtl).has_value());
  EXPECT_TRUE(
      cache_->Get("pref:persistent:1", RocksDBCache::DataTier::kPersistent)
          .has_value());
  EXPECT_TRUE(cache_->Get("other:ttl:1", RocksDBCache::DataTier::kTtl).has_value());

  EXPECT_EQ(cache_->DeleteByPrefix("pref:persistent:",
                                   RocksDBCache::DataTier::kPersistent, 2),
            1U);
  EXPECT_FALSE(
      cache_->Get("pref:persistent:1", RocksDBCache::DataTier::kPersistent)
          .has_value());
}

TEST_F(RocksDBCacheP1Test, UInt64PropertyReadsAggregatedAcrossColumnFamilies) {
  ASSERT_TRUE(cache_->Set("metrics:ttl:1", MakeVersionedData(10, 1),
                          RocksDBCache::DataTier::kTtl));
  ASSERT_TRUE(cache_->Set("metrics:persistent:1", MakeVersionedData(11, 2),
                          RocksDBCache::DataTier::kPersistent));

  uint64_t num_keys = 0;
  ASSERT_TRUE(cache_->GetUInt64Property("rocksdb.estimate-num-keys", &num_keys));
  EXPECT_GT(num_keys, 0U);
}

TEST_F(RocksDBCacheP1Test, LegacyDefaultApisWriteToTtlTier) {
  const VersionedData data = MakeVersionedData(33, 9);
  ASSERT_TRUE(cache_->Set("legacy:ttl", data));

  EXPECT_TRUE(cache_->Get("legacy:ttl", RocksDBCache::DataTier::kTtl).has_value());
  EXPECT_FALSE(
      cache_->Get("legacy:ttl", RocksDBCache::DataTier::kPersistent)
          .has_value());
}

TEST_F(RocksDBCacheP1Test,
       BenchmarkForEachScanIsolationPreservesBlockCacheHitRateVsBaseline) {
  if (!BenchmarkOnlyEnabled()) {
    GTEST_SKIP() << "Set LEGEND2_BENCHMARK_ONLY=1 to run benchmark tests.";
  }

  cache_.reset();

  struct ScenarioResult {
    double hit_ratio = 0.0;
    uint64_t hit_delta = 0;
    uint64_t miss_delta = 0;
    size_t point_reads = 0;
    size_t scan_rounds = 0;
  };

  auto run_scenario = [&](bool isolate_scan_reader) -> ScenarioResult {
    ScenarioResult result;
    RocksDBCache::Config scenario_config = config_;
    scenario_config.db_path =
        db_path_ + (isolate_scan_reader ? "_scan_isolated" : "_scan_baseline");
    scenario_config.block_cache_size = 8 * 1024 * 1024;
    scenario_config.scan_fill_cache = false;
    scenario_config.iter_pin_data = true;
    scenario_config.isolate_foreach_scan_reader = isolate_scan_reader;
    scenario_config.enable_statistics = true;
    scenario_config.data_write_buffer_size = 2 * 1024 * 1024;

    std::error_code ec;
    std::filesystem::remove_all(scenario_config.db_path, ec);

    auto scenario_cache = std::make_unique<RocksDBCache>(scenario_config);
    const bool initialized = scenario_cache->Initialize();
    EXPECT_TRUE(initialized);
    if (!initialized) {
      return result;
    }

    constexpr size_t kTotalKeys = 24000;
    constexpr size_t kHotKeys = 1024;
    constexpr size_t kPayloadBytes = 512;

    std::vector<std::string> hot_keys;
    hot_keys.reserve(kHotKeys);
    const uint64_t now_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    for (size_t i = 0; i < kTotalKeys; ++i) {
      const std::string key = "scan:key:" + std::to_string(i);
      VersionedData data{
          .version = static_cast<uint64_t>(i + 1),
          .data = MakePayload(kPayloadBytes, static_cast<uint8_t>(i & 0xFF)),
          .timestamp_ms = now_ms};
      if (!scenario_cache->Set(key, data, RocksDBCache::DataTier::kTtl)) {
        ADD_FAILURE() << "Failed to populate test key " << key;
        return result;
      }
      if (i < kHotKeys) {
        hot_keys.push_back(key);
      }
    }

    // Reopen once so subsequent reads go through SST/block-cache path
    // instead of mostly serving from memtable.
    scenario_cache.reset();
    scenario_cache = std::make_unique<RocksDBCache>(scenario_config);
    if (!scenario_cache->Initialize()) {
      ADD_FAILURE() << "Failed to reopen benchmark cache";
      return result;
    }

    // Warm the hot read set into block cache.
    for (size_t round = 0; round < 6; ++round) {
      for (const auto& key : hot_keys) {
        auto hit = scenario_cache->Get(key, RocksDBCache::DataTier::kTtl);
        if (!hit.has_value()) {
          ADD_FAILURE() << "Warm-up miss for key " << key;
          return result;
        }
      }
    }

    uint64_t hit_before = 0;
    uint64_t miss_before = 0;
    if (!scenario_cache->GetUInt64Property("rocksdb.block-cache-hit",
                                           &hit_before)) {
      ADD_FAILURE() << "Failed to read rocksdb.block-cache-hit before workload";
      return result;
    }
    if (!scenario_cache->GetUInt64Property("rocksdb.block-cache-miss",
                                           &miss_before)) {
      ADD_FAILURE() << "Failed to read rocksdb.block-cache-miss before workload";
      return result;
    }

    std::atomic<bool> stop_scan{false};
    std::atomic<size_t> scan_rounds{0};
    std::thread scanner([&]() {
      while (!stop_scan.load(std::memory_order_relaxed)) {
        scenario_cache->ForEach(
            [](const std::string&, const VersionedData&) { return true; },
            RocksDBCache::DataTier::kTtl);
        scan_rounds.fetch_add(1, std::memory_order_relaxed);
      }
    });

    std::mt19937 rng(0xC0FFEE);
    std::uniform_int_distribution<size_t> pick_hot_key(0, kHotKeys - 1);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    size_t point_reads = 0;
    while (std::chrono::steady_clock::now() < deadline) {
      const std::string& key = hot_keys[pick_hot_key(rng)];
      auto hit = scenario_cache->Get(key, RocksDBCache::DataTier::kTtl);
      if (!hit.has_value()) {
        ADD_FAILURE() << "Point read miss during benchmark for key " << key;
        break;
      }
      ++point_reads;
    }

    stop_scan.store(true, std::memory_order_relaxed);
    scanner.join();

    uint64_t hit_after = 0;
    uint64_t miss_after = 0;
    if (!scenario_cache->GetUInt64Property("rocksdb.block-cache-hit",
                                           &hit_after)) {
      ADD_FAILURE() << "Failed to read rocksdb.block-cache-hit after workload";
      return result;
    }
    if (!scenario_cache->GetUInt64Property("rocksdb.block-cache-miss",
                                           &miss_after)) {
      ADD_FAILURE() << "Failed to read rocksdb.block-cache-miss after workload";
      return result;
    }

    result.hit_delta = hit_after - hit_before;
    result.miss_delta = miss_after - miss_before;
    result.point_reads = point_reads;
    result.scan_rounds = scan_rounds.load(std::memory_order_relaxed);
    const uint64_t total = result.hit_delta + result.miss_delta;
    if (total > 0) {
      result.hit_ratio = static_cast<double>(result.hit_delta) /
                         static_cast<double>(total);
    }

    std::cout << "[rocksdb_scan_bench] isolate=" << isolate_scan_reader
              << " reads=" << result.point_reads
              << " scans=" << result.scan_rounds
              << " hit_delta=" << result.hit_delta
              << " miss_delta=" << result.miss_delta
              << " hit_ratio=" << result.hit_ratio << std::endl;

    scenario_cache.reset();
    std::filesystem::remove_all(scenario_config.db_path, ec);
    return result;
  };

  const ScenarioResult baseline = run_scenario(false);
  const ScenarioResult isolated = run_scenario(true);

  const uint64_t baseline_total = baseline.hit_delta + baseline.miss_delta;
  const uint64_t isolated_total = isolated.hit_delta + isolated.miss_delta;
  ASSERT_GT(baseline.point_reads, 10000U);
  ASSERT_GT(isolated.point_reads, 10000U);
  ASSERT_GT(baseline_total, 1000U);
  ASSERT_GT(isolated_total, 1000U);

  // Scan isolation should not significantly worsen cache hit quality.
  EXPECT_GE(isolated.hit_ratio, baseline.hit_ratio * 0.95)
      << "baseline_hit_ratio=" << baseline.hit_ratio
      << ", isolated_hit_ratio=" << isolated.hit_ratio;
}

TEST_F(RocksDBCacheP1Test, Phase2ReadFallbackCanReadLegacyV1PayloadWhenEnabled) {
  const VersionedData legacy_v1 = MakeVersionedData(88, 9);
  ASSERT_TRUE(cache_->Set("phase2:v1:fallback:on", legacy_v1,
                          RocksDBCache::DataTier::kTtl));

  cache_.reset();

  config_.enable_v2_encode = true;
  config_.enable_v2_read_fallback = true;
  cache_ = std::make_unique<RocksDBCache>(config_);
  ASSERT_TRUE(cache_->Initialize());

  auto hit = cache_->Get("phase2:v1:fallback:on", RocksDBCache::DataTier::kTtl);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->version, legacy_v1.version);
  EXPECT_EQ(hit->data, legacy_v1.data);
}

TEST_F(RocksDBCacheP1Test,
       Phase2ReadFallbackRejectsLegacyV1PayloadWhenDisabled) {
  const VersionedData legacy_v1 = MakeVersionedData(89, 10);
  ASSERT_TRUE(cache_->Set("phase2:v1:fallback:off", legacy_v1,
                          RocksDBCache::DataTier::kTtl));

  cache_.reset();

  config_.enable_v2_encode = true;
  config_.enable_v2_read_fallback = false;
  cache_ = std::make_unique<RocksDBCache>(config_);
  ASSERT_TRUE(cache_->Initialize());

  auto hit = cache_->Get("phase2:v1:fallback:off", RocksDBCache::DataTier::kTtl);
  EXPECT_FALSE(hit.has_value());
}

TEST_F(RocksDBCacheP1Test, Phase2WritePathDefaultsToLegacyV1Encoding) {
  const VersionedData data = MakeVersionedData(90, 11);
  ASSERT_TRUE(cache_->Set("phase2:encode:v1", data, RocksDBCache::DataTier::kTtl));

  const auto raw = ReadRawDataValue(db_path_, "phase2:encode:v1", kCfDataTtl);
  ASSERT_TRUE(raw.has_value());
  ASSERT_GE(raw->size(), sizeof(uint64_t));

  uint64_t encoded_version = 0;
  std::memcpy(&encoded_version, raw->data(), sizeof(uint64_t));
  EXPECT_EQ(encoded_version, data.version);
}

TEST_F(RocksDBCacheP1Test, Phase2WritePathUsesV2EncodingWhenFlagEnabled) {
  cache_.reset();
  config_.enable_v2_encode = true;
  config_.enable_v2_read_fallback = true;
  cache_ = std::make_unique<RocksDBCache>(config_);
  ASSERT_TRUE(cache_->Initialize());

  const VersionedData data = MakeVersionedData(91, 12);
  ASSERT_TRUE(cache_->Set("phase2:encode:v2", data, RocksDBCache::DataTier::kTtl));

  const auto raw = ReadRawDataValue(db_path_, "phase2:encode:v2", kCfDataTtl);
  ASSERT_TRUE(raw.has_value());
  ASSERT_GE(raw->size(), 4U);
  EXPECT_EQ(static_cast<uint8_t>((*raw)[0]), static_cast<uint8_t>('M'));
  EXPECT_EQ(static_cast<uint8_t>((*raw)[1]), static_cast<uint8_t>('2'));
  EXPECT_EQ(static_cast<uint8_t>((*raw)[2]), static_cast<uint8_t>('V'));
  EXPECT_EQ(static_cast<uint8_t>((*raw)[3]), static_cast<uint8_t>('2'));
}

TEST_F(RocksDBCacheP1Test, Phase4EncryptionStoresCiphertextAndRoundTrips) {
  cache_.reset();
  config_.enable_v2_encode = true;
  config_.enable_v2_read_fallback = true;
  config_.enable_data_encryption = true;
  config_.encryption_active_key_id = "k1";
  config_.encryption_key_env = kEncryptionEnv;

  ScopedEnvVar encryption_env(
      kEncryptionEnv,
      "k1=00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");

  cache_ = std::make_unique<RocksDBCache>(config_);
  ASSERT_TRUE(cache_->Initialize());

  const uint64_t now_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  const VersionedData data{
      .version = 501,
      .data = std::vector<uint8_t>{'s', 'e', 'c', 'r', 'e', 't', '-', 'v', '2'},
      .timestamp_ms = now_ms};
  ASSERT_TRUE(cache_->Set("phase4:enc:v2", data, RocksDBCache::DataTier::kTtl));

  const auto raw = ReadRawDataValue(db_path_, "phase4:enc:v2", kCfDataTtl);
  ASSERT_TRUE(raw.has_value());
  EXPECT_FALSE(ContainsBytes(*raw, data.data));

  const auto loaded = cache_->Get("phase4:enc:v2", RocksDBCache::DataTier::kTtl);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->version, data.version);
  EXPECT_EQ(loaded->data, data.data);
}

TEST_F(RocksDBCacheP1Test, Phase4EncryptionKeyRotationReadsOldAndNewPayloads) {
  cache_.reset();
  config_.enable_v2_encode = true;
  config_.enable_v2_read_fallback = true;
  config_.enable_data_encryption = true;
  config_.encryption_active_key_id = "k1";
  config_.encryption_key_env = kEncryptionEnv;

  ScopedEnvVar initial_env(
      kEncryptionEnv,
      "k1=00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");

  cache_ = std::make_unique<RocksDBCache>(config_);
  ASSERT_TRUE(cache_->Initialize());
  const uint64_t now_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  ASSERT_TRUE(cache_->Set(
      "phase4:key:old",
      VersionedData{.version = 610,
                    .data = std::vector<uint8_t>{1, 2, 3},
                    .timestamp_ms = now_ms},
      RocksDBCache::DataTier::kTtl));

  SetEnvVar(
      kEncryptionEnv,
      "k1=00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff,"
      "k2=ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100");
  ASSERT_TRUE(
      cache_->ApplyRuntimeEncryptionConfig(true, "k2", kEncryptionEnv));

  ASSERT_TRUE(cache_->Set(
      "phase4:key:new",
      VersionedData{.version = 611,
                    .data = std::vector<uint8_t>{4, 5, 6},
                    .timestamp_ms = now_ms + 1},
      RocksDBCache::DataTier::kTtl));

  auto old_loaded = cache_->Get("phase4:key:old", RocksDBCache::DataTier::kTtl);
  ASSERT_TRUE(old_loaded.has_value());
  EXPECT_EQ(old_loaded->data, (std::vector<uint8_t>{1, 2, 3}));

  auto new_loaded = cache_->Get("phase4:key:new", RocksDBCache::DataTier::kTtl);
  ASSERT_TRUE(new_loaded.has_value());
  EXPECT_EQ(new_loaded->data, (std::vector<uint8_t>{4, 5, 6}));
}

TEST_F(RocksDBCacheP1Test, Phase4EncryptionInitFailsWithoutActiveKeyMaterial) {
  cache_.reset();
  config_.enable_v2_encode = true;
  config_.enable_data_encryption = true;
  config_.encryption_active_key_id = "missing";
  config_.encryption_key_env = kEncryptionEnv;

  UnsetEnvVar(kEncryptionEnv);

  cache_ = std::make_unique<RocksDBCache>(config_);
  EXPECT_FALSE(cache_->Initialize());
}

}  // namespace
}  // namespace mir2::storage_engine::l2
