#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
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

}  // namespace
}  // namespace mir2::storage_engine::l2
