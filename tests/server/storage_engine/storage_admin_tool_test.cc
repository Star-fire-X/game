#include "storage_engine/utils/storage_admin_tool.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "rocksdb/db.h"
#include "rocksdb/utilities/db_ttl.h"
#include "ecs/character_snapshot_codec.h"
#include "storage_engine/l2/rocksdb_cache.h"
#include "storage_engine/storage_engine.h"
#include "storage_engine/test_backend_mocks.h"

namespace mir2::storage_engine::utils::storage_admin {
namespace {

constexpr const char* kL2CfDataPersistent = "cf_data_persistent";
constexpr const char* kL2CfDataTtl = "cf_data_ttl";
constexpr const char* kL2CfOutbox = "cf_outbox";
constexpr const char* kL2CfDeadLetter = "cf_dead_letter";
constexpr const char* kL2CfMeta = "cf_meta";
constexpr const char* kRuntimeEncryptionEnv =
    "MIR2_STORAGE_ADMIN_RUNTIME_AUDIT_TEST_KEYS";

std::string MakeTempPath(const std::string& suffix) {
  return "/tmp/mir2_storage_admin_tool_test_" + suffix + "_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count());
}

std::string EncodeHex(const std::vector<uint8_t>& data) {
  static constexpr char kHexDigits[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(data.size() * 2);
  for (const uint8_t byte : data) {
    hex.push_back(kHexDigits[(byte >> 4) & 0x0F]);
    hex.push_back(kHexDigits[byte & 0x0F]);
  }
  return hex;
}

std::string BuildCharacterSnapshotHex(int x, int y) {
  mir2::common::CharacterData data;
  data.id = 7001;
  data.account_id = 9001;
  data.name = "phase6_role";
  data.char_class = mir2::common::CharacterClass::WARRIOR;
  data.gender = mir2::common::Gender::MALE;
  data.map_id = 1;
  data.position = {x, y};
  return EncodeHex(mir2::ecs::SerializeCharacterSnapshot(data));
}

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

class TombstoneGcCaptureBackend : public test::NoopStorageBackend {
 public:
  IStorageBackend::StorageResult Delete(const std::string&,
                                        uint64_t,
                                        bool) override {
    delete_calls.fetch_add(1, std::memory_order_relaxed);
    return IStorageBackend::StorageResult{true, "", 0};
  }

  bool WaitForCallCount(size_t expected, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
      if (delete_calls.load(std::memory_order_relaxed) >= expected) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } while (std::chrono::steady_clock::now() < deadline);
    return delete_calls.load(std::memory_order_relaxed) >= expected;
  }

  std::atomic<uint32_t> delete_calls{0};
};

void DestroyL2Handles(rocksdb::DBWithTTL* db,
                      const std::vector<rocksdb::ColumnFamilyHandle*>& handles) {
  if (db == nullptr) {
    return;
  }
  for (auto* handle : handles) {
    if (handle != nullptr) {
      db->DestroyColumnFamilyHandle(handle);
    }
  }
}

bool CorruptRawL2Value(const std::string& path,
                       const std::string& key,
                       const std::string& cf_name) {
  std::vector<rocksdb::ColumnFamilyDescriptor> descriptors{
      {rocksdb::kDefaultColumnFamilyName, rocksdb::ColumnFamilyOptions{}},
      {kL2CfDataPersistent, rocksdb::ColumnFamilyOptions{}},
      {kL2CfDataTtl, rocksdb::ColumnFamilyOptions{}},
      {kL2CfOutbox, rocksdb::ColumnFamilyOptions{}},
      {kL2CfDeadLetter, rocksdb::ColumnFamilyOptions{}},
      {kL2CfMeta, rocksdb::ColumnFamilyOptions{}},
  };
  std::vector<int32_t> ttls{3600, 0, 3600, 0, 0, 0};
  std::vector<rocksdb::ColumnFamilyHandle*> handles;
  rocksdb::DBWithTTL* raw_db = nullptr;
  const rocksdb::Status status = rocksdb::DBWithTTL::Open(
      rocksdb::DBOptions{}, path, descriptors, &handles, &raw_db, ttls, false);
  if (!status.ok()) {
    return false;
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
    DestroyL2Handles(db.get(), handles);
    return false;
  }

  std::string value;
  auto get_status = db->Get(rocksdb::ReadOptions{}, target, key, &value);
  if (!get_status.ok() || value.empty()) {
    DestroyL2Handles(db.get(), handles);
    return false;
  }
  value.back() = static_cast<char>(value.back() ^ 0xFF);
  auto put_status = db->Put(rocksdb::WriteOptions{}, target, key, value);
  DestroyL2Handles(db.get(), handles);
  return put_status.ok();
}

TEST(StorageAdminToolTest, ParseCommandLineRejectsMissingSubcommand) {
  char arg0[] = "mir2_storage_admin";
  char* argv[] = {arg0};
  CommandOptions options;
  std::string error;
  EXPECT_FALSE(ParseCommandLine(1, argv, &options, &error));
  EXPECT_NE(error.find("missing subcommand"), std::string::npos);
}

TEST(StorageAdminToolTest, ParseCommandLineParsesValidateOptions) {
  char arg0[] = "mir2_storage_admin";
  char arg1[] = "validate";
  char arg2[] = "--db-path";
  char arg3[] = "/tmp/mir2_storage_admin_parse";
  char arg4[] = "--ttl-seconds";
  char arg5[] = "7200";
  char arg6[] = "--enable-v2-encode";
  char arg7[] = "true";
  char arg8[] = "--enable-v2-read-fallback";
  char arg9[] = "false";
  char* argv[] = {arg0, arg1, arg2, arg3, arg4,
                  arg5, arg6, arg7, arg8, arg9};
  CommandOptions options;
  std::string error;
  ASSERT_TRUE(ParseCommandLine(10, argv, &options, &error));
  EXPECT_EQ(options.command, Command::kValidate);
  EXPECT_EQ(options.db_path, "/tmp/mir2_storage_admin_parse");
  EXPECT_EQ(options.ttl_seconds, 7200u);
  EXPECT_TRUE(options.enable_v2_encode);
  EXPECT_FALSE(options.enable_v2_read_fallback);
}

TEST(StorageAdminToolTest, ParseCommandLineParsesCheckpointCreateOptions) {
  char arg0[] = "mir2_storage_admin";
  char arg1[] = "checkpoint-create";
  char arg2[] = "--db-path";
  char arg3[] = "/tmp/mir2_storage_admin_db";
  char arg4[] = "--output-path";
  char arg5[] = "/tmp/mir2_storage_admin_cp";
  char arg6[] = "--overwrite";
  char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6};
  CommandOptions options;
  std::string error;
  ASSERT_TRUE(ParseCommandLine(7, argv, &options, &error));
  EXPECT_EQ(options.command, Command::kCheckpointCreate);
  EXPECT_EQ(options.db_path, "/tmp/mir2_storage_admin_db");
  EXPECT_EQ(options.output_path, "/tmp/mir2_storage_admin_cp");
  EXPECT_TRUE(options.overwrite);
}

TEST(StorageAdminToolTest, ParseCommandLineParsesDeadLetterReplayOptions) {
  char arg0[] = "mir2_storage_admin";
  char arg1[] = "dead-letter-replay";
  char arg2[] = "--db-path";
  char arg3[] = "/tmp/mir2_storage_admin_db";
  char arg4[] = "--prefix";
  char arg5[] = "role:";
  char arg6[] = "--start-ms";
  char arg7[] = "100";
  char arg8[] = "--end-ms";
  char arg9[] = "200";
  char arg10[] = "--limit";
  char arg11[] = "8";
  char arg12[] = "--dry-run";
  char arg13[] = "--keep-dead-letter";
  char arg14[] = "--verbose";
  char* argv[] = {arg0, arg1,  arg2,  arg3,  arg4,  arg5, arg6, arg7,
                  arg8, arg9,  arg10, arg11, arg12, arg13, arg14};
  CommandOptions options;
  std::string error;
  ASSERT_TRUE(ParseCommandLine(15, argv, &options, &error));
  EXPECT_EQ(options.command, Command::kDeadLetterReplay);
  EXPECT_EQ(options.db_path, "/tmp/mir2_storage_admin_db");
  EXPECT_EQ(options.key_prefix, "role:");
  EXPECT_EQ(options.start_ms, 100u);
  EXPECT_EQ(options.end_ms, 200u);
  EXPECT_EQ(options.limit, 8u);
  EXPECT_TRUE(options.dry_run);
  EXPECT_TRUE(options.keep_dead_letter);
  EXPECT_TRUE(options.verbose);
}

TEST(StorageAdminToolTest, ParseCommandLineParsesDecodeCharacterSnapshotOptions) {
  char arg0[] = "mir2_storage_admin";
  char arg1[] = "decode-character-snapshot";
  char arg2[] = "--hex";
  auto hex = BuildCharacterSnapshotHex(101, 100);
  std::vector<char> hex_buf(hex.begin(), hex.end());
  hex_buf.push_back('\0');
  char arg3[] = "--expected-x";
  char arg4[] = "101";
  char arg5[] = "--expected-y";
  char arg6[] = "100";
  char* argv[] = {arg0, arg1, arg2, hex_buf.data(), arg3, arg4, arg5, arg6};
  CommandOptions options;
  std::string error;
  ASSERT_TRUE(ParseCommandLine(8, argv, &options, &error));
  EXPECT_EQ(options.command, Command::kDecodeCharacterSnapshot);
  EXPECT_EQ(options.snapshot_hex, hex);
  EXPECT_EQ(options.expected_x, 101);
  EXPECT_EQ(options.expected_y, 100);
}

TEST(StorageAdminToolTest, ExecuteValidateReturnsZeroForHealthyDb) {
  const std::string db_path = MakeTempPath("validate_clean");
  std::error_code ec;
  std::filesystem::remove_all(db_path, ec);

  {
    l2::RocksDBCache::Config cfg;
    cfg.db_path = db_path;
    cfg.enable_v2_encode = true;
    cfg.enable_v2_read_fallback = true;
    l2::RocksDBCache cache(cfg);
    ASSERT_TRUE(cache.Initialize());
    VersionedData data;
    data.version = 1;
    data.timestamp_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    data.data = {1, 2, 3, 4};
    ASSERT_TRUE(cache.Set("storage_admin:clean", data,
                          l2::RocksDBCache::DataTier::kTtl));
  }

  CommandOptions options;
  options.command = Command::kValidate;
  options.db_path = db_path;
  options.enable_v2_encode = true;
  options.enable_v2_read_fallback = true;
  const auto result = Execute(options);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(result.stdout_text.find("total_corrupted=0"), std::string::npos);
  EXPECT_NE(result.stdout_text.find("l2_soft_limit_write_total="),
            std::string::npos);
  EXPECT_NE(result.stdout_text.find("l2_hard_limit_reject_total="),
            std::string::npos);
  EXPECT_NE(result.stdout_text.find("l2_hard_limit_bypass_total="),
            std::string::npos);
  EXPECT_NE(result.stdout_text.find("tombstone_gc_pending="),
            std::string::npos);
  EXPECT_NE(result.stdout_text.find("tombstone_gc_reclaimed_total="),
            std::string::npos);
  EXPECT_NE(result.stdout_text.find("tombstone_gc_failed_total="),
            std::string::npos);
  EXPECT_TRUE(result.stderr_text.empty());

  std::filesystem::remove_all(db_path, ec);
}

TEST(StorageAdminToolTest, ExecuteHealthIncludesEncryptionDecodeCounters) {
  const std::string db_path = MakeTempPath("health_codec_counters");
  std::error_code ec;
  std::filesystem::remove_all(db_path, ec);

  {
    l2::RocksDBCache::Config cfg;
    cfg.db_path = db_path;
    cfg.enable_v2_encode = true;
    cfg.enable_v2_read_fallback = true;
    l2::RocksDBCache cache(cfg);
    ASSERT_TRUE(cache.Initialize());
    VersionedData data;
    data.version = 1;
    data.timestamp_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    data.data = {9, 8, 7, 6};
    ASSERT_TRUE(cache.Set("storage_admin:health:key",
                          data,
                          l2::RocksDBCache::DataTier::kTtl));
  }

  CommandOptions options;
  options.command = Command::kHealth;
  options.db_path = db_path;
  options.enable_v2_encode = true;
  options.enable_v2_read_fallback = true;
  const auto result = Execute(options);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(result.stdout_text.find("storage_admin_health_summary"),
            std::string::npos);
  EXPECT_NE(result.stdout_text.find("enable_data_encryption="),
            std::string::npos);
  EXPECT_NE(result.stdout_text.find("encrypted_decode_reads="),
            std::string::npos);
  EXPECT_NE(result.stdout_text.find("decrypt_failures="),
            std::string::npos);
  EXPECT_NE(result.stdout_text.find(
                "runtime_config_audit_key_enable_data_encryption_total="),
            std::string::npos);
  EXPECT_NE(
      result.stdout_text.find("runtime_config_audit_key_encryption_key_env_total="),
      std::string::npos);
  EXPECT_NE(result.stdout_text.find("l2_soft_limit_write_total="),
            std::string::npos);
  EXPECT_NE(result.stdout_text.find("l2_hard_limit_reject_total="),
            std::string::npos);
  EXPECT_NE(result.stdout_text.find("l2_hard_limit_bypass_total="),
            std::string::npos);
  EXPECT_NE(result.stdout_text.find("tombstone_gc_pending="),
            std::string::npos);
  EXPECT_NE(result.stdout_text.find("tombstone_gc_reclaimed_total="),
            std::string::npos);
  EXPECT_NE(result.stdout_text.find("tombstone_gc_failed_total="),
            std::string::npos);
  EXPECT_TRUE(result.stderr_text.empty());

  std::filesystem::remove_all(db_path, ec);
}

TEST(StorageAdminToolTest, ExecuteDecodeCharacterSnapshotReportsPositionMatch) {
  CommandOptions options;
  options.command = Command::kDecodeCharacterSnapshot;
  options.snapshot_hex = BuildCharacterSnapshotHex(101, 100);
  options.expected_x = 101;
  options.expected_y = 100;

  const auto result = Execute(options);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(result.stdout_text.find("actual_x=101"), std::string::npos);
  EXPECT_NE(result.stdout_text.find("actual_y=100"), std::string::npos);
  EXPECT_NE(result.stdout_text.find("position_matches=true"), std::string::npos);
  EXPECT_TRUE(result.stderr_text.empty());
}

TEST(StorageAdminToolTest, ExecuteHealthReadsPersistedRuntimeAuditCounters) {
  if (StorageEngine::IsInitialized()) {
    StorageEngine::Shutdown();
  }

  const std::string db_path = MakeTempPath("health_runtime_audit_counters");
  std::error_code ec;
  std::filesystem::remove_all(db_path, ec);

  ScopedEnvVar env_guard(
      kRuntimeEncryptionEnv,
      "k1=00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");

  StorageEngine::Config config;
  config.l2_path = db_path;
  config.enable_v2_encode = true;
  config.enable_v2_read_fallback = true;
  config.enable_data_encryption = true;
  config.encryption_active_key_id = "k1";
  config.encryption_key_env = kRuntimeEncryptionEnv;
  config.enable_audit_log = false;

  auto backend = std::make_unique<test::NoopStorageBackend>();
  ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));
  auto& engine = StorageEngine::Instance();

  StorageEngine::RuntimeTunableConfig runtime_cfg;
  runtime_cfg.enable_data_encryption = true;
  runtime_cfg.encryption_key_env = kRuntimeEncryptionEnv;
  ASSERT_TRUE(engine.ApplyRuntimeConfig(runtime_cfg));

  StorageEngine::Shutdown();

  CommandOptions options;
  options.command = Command::kHealth;
  options.db_path = db_path;
  options.enable_v2_encode = true;
  options.enable_v2_read_fallback = true;
  const auto result = Execute(options);

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(
      result.stdout_text.find(
          "runtime_config_audit_key_enable_data_encryption_total=1"),
      std::string::npos);
  EXPECT_NE(
      result.stdout_text.find("runtime_config_audit_key_encryption_key_env_total=1"),
      std::string::npos);
  EXPECT_TRUE(result.stderr_text.empty());

  std::filesystem::remove_all(db_path, ec);
}

TEST(StorageAdminToolTest, ExecuteHealthReadsPersistedCapacityGovernanceCounters) {
  if (StorageEngine::IsInitialized()) {
    StorageEngine::Shutdown();
  }

  const std::string db_path = MakeTempPath("health_capacity_governance_counters");
  std::error_code ec;
  std::filesystem::remove_all(db_path, ec);

  StorageEngine::Config config;
  config.l2_path = db_path;
  config.enable_strict_write_guarantee = false;
  config.l2_usage_soft_limit_ratio = 0.0;
  config.l2_usage_hard_limit_ratio = 0.0;
  config.critical_key_prefixes = {"critical:"};
  config.sync_write_key_prefixes = {"critical:"};

  auto backend = std::make_unique<test::NoopStorageBackend>();
  ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));
  auto& engine = StorageEngine::Instance();

  EXPECT_FALSE(engine.Set("normal:capacity:reject", {1, 2, 3}, Priority::NORMAL));
  EXPECT_TRUE(engine.Set("critical:capacity:bypass", {4, 5, 6}, Priority::NORMAL));

  StorageEngine::Shutdown();

  CommandOptions options;
  options.command = Command::kHealth;
  options.db_path = db_path;
  const auto result = Execute(options);

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(result.stdout_text.find("l2_soft_limit_write_total=2"),
            std::string::npos);
  EXPECT_NE(result.stdout_text.find("l2_hard_limit_reject_total=1"),
            std::string::npos);
  EXPECT_NE(result.stdout_text.find("l2_hard_limit_bypass_total=1"),
            std::string::npos);
  EXPECT_TRUE(result.stderr_text.empty());

  std::filesystem::remove_all(db_path, ec);
}

TEST(StorageAdminToolTest, ExecuteHealthReadsPersistedTombstoneGcCounters) {
  if (StorageEngine::IsInitialized()) {
    StorageEngine::Shutdown();
  }

  const std::string db_path = MakeTempPath("health_tombstone_gc_counters");
  std::error_code ec;
  std::filesystem::remove_all(db_path, ec);

  StorageEngine::Config config;
  config.l2_path = db_path;
  config.enable_strict_write_guarantee = false;
  config.tombstone_retention_seconds = 1;
  config.tombstone_gc_interval_seconds = 1;

  auto backend = std::make_unique<TombstoneGcCaptureBackend>();
  auto* backend_ptr = backend.get();
  ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));
  auto& engine = StorageEngine::Instance();

  DeleteOptions options;
  options.hard_delete = false;
  options.write_tombstone = true;
  ASSERT_TRUE(engine.Delete("storage_admin:tombstone:gc", options));
  ASSERT_TRUE(
      backend_ptr->WaitForCallCount(2, std::chrono::milliseconds(3500)));
  StorageEngine::Shutdown();

  CommandOptions health_options;
  health_options.command = Command::kHealth;
  health_options.db_path = db_path;
  const auto health_result = Execute(health_options);
  EXPECT_EQ(health_result.exit_code, 0);
  EXPECT_NE(health_result.stdout_text.find("tombstone_gc_pending=0"),
            std::string::npos);
  EXPECT_NE(health_result.stdout_text.find("tombstone_gc_reclaimed_total=1"),
            std::string::npos);
  EXPECT_NE(health_result.stdout_text.find("tombstone_gc_failed_total=0"),
            std::string::npos);

  std::filesystem::remove_all(db_path, ec);
}

TEST(StorageAdminToolTest, ExecuteCheckpointCreateAndRestoreRoundTrip) {
  const std::string db_path = MakeTempPath("checkpoint_source");
  const std::string checkpoint_path = MakeTempPath("checkpoint_snapshot");
  const std::string restore_path = MakeTempPath("checkpoint_restore");
  std::error_code ec;
  std::filesystem::remove_all(db_path, ec);
  std::filesystem::remove_all(checkpoint_path, ec);
  std::filesystem::remove_all(restore_path, ec);

  const std::string key = "storage_admin:checkpoint:key";
  const std::vector<uint8_t> payload{2, 4, 6, 8};

  {
    l2::RocksDBCache::Config cfg;
    cfg.db_path = db_path;
    cfg.enable_v2_encode = true;
    cfg.enable_v2_read_fallback = true;
    l2::RocksDBCache cache(cfg);
    ASSERT_TRUE(cache.Initialize());
    VersionedData data;
    data.version = 1;
    data.timestamp_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    data.data = payload;
    ASSERT_TRUE(cache.Set(key, data, l2::RocksDBCache::DataTier::kTtl));
  }

  CommandOptions checkpoint_options;
  checkpoint_options.command = Command::kCheckpointCreate;
  checkpoint_options.db_path = db_path;
  checkpoint_options.output_path = checkpoint_path;
  checkpoint_options.overwrite = true;
  const auto checkpoint_result = Execute(checkpoint_options);
  EXPECT_EQ(checkpoint_result.exit_code, 0);
  EXPECT_NE(checkpoint_result.stdout_text.find("storage_admin_checkpoint_create_summary"),
            std::string::npos);

  CommandOptions restore_options;
  restore_options.command = Command::kCheckpointRestore;
  restore_options.checkpoint_path = checkpoint_path;
  restore_options.restore_db_path = restore_path;
  restore_options.overwrite = true;
  const auto restore_result = Execute(restore_options);
  EXPECT_EQ(restore_result.exit_code, 0);
  EXPECT_NE(restore_result.stdout_text.find("storage_admin_checkpoint_restore_summary"),
            std::string::npos);

  l2::RocksDBCache::Config restored_cfg;
  restored_cfg.db_path = restore_path;
  restored_cfg.enable_v2_encode = true;
  restored_cfg.enable_v2_read_fallback = true;
  l2::RocksDBCache restored_cache(restored_cfg);
  ASSERT_TRUE(restored_cache.Initialize());
  const auto restored = restored_cache.Get(key, l2::RocksDBCache::DataTier::kTtl);
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->data, payload);

  std::filesystem::remove_all(db_path, ec);
  std::filesystem::remove_all(checkpoint_path, ec);
  std::filesystem::remove_all(restore_path, ec);
}

TEST(StorageAdminToolTest, ExecuteDeadLetterReplayDryRunKeepsRows) {
  const std::string db_path = MakeTempPath("dead_letter_dry_run");
  std::error_code ec;
  std::filesystem::remove_all(db_path, ec);

  const std::string key = "storage_admin:dead_letter:dry_run";
  const std::vector<uint8_t> payload{1, 3, 5, 7};
  {
    l2::RocksDBCache::Config cfg;
    cfg.db_path = db_path;
    cfg.enable_v2_encode = true;
    cfg.enable_v2_read_fallback = true;
    l2::RocksDBCache cache(cfg);
    ASSERT_TRUE(cache.Initialize());
    l2::RocksDBCache::DeadLetterEntry entry;
    entry.key = key;
    entry.priority = Priority::NORMAL;
    entry.attempts = 2;
    entry.recorded_at_ms = 1000;
    entry.error_message = "dry-run";
    entry.data.version = 2;
    entry.data.timestamp_ms = 1000;
    entry.data.data = payload;
    ASSERT_TRUE(cache.AppendDeadLetter(entry));
  }

  CommandOptions options;
  options.command = Command::kDeadLetterReplay;
  options.db_path = db_path;
  options.dry_run = true;
  const auto result = Execute(options);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(result.stdout_text.find("storage_admin_dead_letter_replay_summary"),
            std::string::npos);
  EXPECT_NE(result.stdout_text.find("matched=1"), std::string::npos);
  EXPECT_NE(result.stdout_text.find("replayed=0"), std::string::npos);
  EXPECT_NE(result.stdout_text.find("acked=0"), std::string::npos);
  EXPECT_TRUE(result.stderr_text.empty());

  l2::RocksDBCache::Config verify_cfg;
  verify_cfg.db_path = db_path;
  verify_cfg.enable_v2_encode = true;
  verify_cfg.enable_v2_read_fallback = true;
  l2::RocksDBCache verify_cache(verify_cfg);
  ASSERT_TRUE(verify_cache.Initialize());
  EXPECT_EQ(verify_cache.DeadLetterDepth(), 1u);
  EXPECT_EQ(verify_cache.OutboxDepth(), 0u);

  std::filesystem::remove_all(db_path, ec);
}

TEST(StorageAdminToolTest, ExecuteDeadLetterReplayAppendsOutboxAndAcks) {
  const std::string db_path = MakeTempPath("dead_letter_replay");
  std::error_code ec;
  std::filesystem::remove_all(db_path, ec);

  const std::string key = "storage_admin:dead_letter:replay";
  const std::vector<uint8_t> payload{2, 4, 6, 8};
  {
    l2::RocksDBCache::Config cfg;
    cfg.db_path = db_path;
    cfg.enable_v2_encode = true;
    cfg.enable_v2_read_fallback = true;
    l2::RocksDBCache cache(cfg);
    ASSERT_TRUE(cache.Initialize());
    l2::RocksDBCache::DeadLetterEntry entry;
    entry.key = key;
    entry.priority = Priority::HIGH;
    entry.attempts = 3;
    entry.recorded_at_ms = 2000;
    entry.error_message = "replay";
    entry.data.version = 3;
    entry.data.timestamp_ms = 2000;
    entry.data.data = payload;
    ASSERT_TRUE(cache.AppendDeadLetter(entry));
  }

  CommandOptions options;
  options.command = Command::kDeadLetterReplay;
  options.db_path = db_path;
  const auto result = Execute(options);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(result.stdout_text.find("storage_admin_dead_letter_replay_summary"),
            std::string::npos);
  EXPECT_NE(result.stdout_text.find("matched=1"), std::string::npos);
  EXPECT_NE(result.stdout_text.find("replayed=1"), std::string::npos);
  EXPECT_NE(result.stdout_text.find("acked=1"), std::string::npos);
  EXPECT_NE(result.stdout_text.find("failed=0"), std::string::npos);
  EXPECT_TRUE(result.stderr_text.empty());

  l2::RocksDBCache::Config verify_cfg;
  verify_cfg.db_path = db_path;
  verify_cfg.enable_v2_encode = true;
  verify_cfg.enable_v2_read_fallback = true;
  l2::RocksDBCache verify_cache(verify_cfg);
  ASSERT_TRUE(verify_cache.Initialize());
  EXPECT_EQ(verify_cache.DeadLetterDepth(), 0u);
  EXPECT_EQ(verify_cache.OutboxDepth(), 1u);

  std::filesystem::remove_all(db_path, ec);
}

TEST(StorageAdminToolTest, ExecuteValidateReturnsNonZeroWhenCorruptedEntryDetected) {
  const std::string db_path = MakeTempPath("validate_corrupted");
  std::error_code ec;
  std::filesystem::remove_all(db_path, ec);

  {
    l2::RocksDBCache::Config cfg;
    cfg.db_path = db_path;
    cfg.enable_v2_encode = true;
    cfg.enable_v2_read_fallback = true;
    l2::RocksDBCache cache(cfg);
    ASSERT_TRUE(cache.Initialize());
    VersionedData data;
    data.version = 1;
    data.timestamp_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    data.data = {9, 8, 7, 6};
    ASSERT_TRUE(cache.Set(
        "storage_admin:corrupted", data, l2::RocksDBCache::DataTier::kTtl));
  }

  ASSERT_TRUE(CorruptRawL2Value(
      db_path, "storage_admin:corrupted", kL2CfDataTtl));

  CommandOptions options;
  options.command = Command::kValidate;
  options.db_path = db_path;
  options.enable_v2_encode = true;
  options.enable_v2_read_fallback = true;
  const auto result = Execute(options);
  EXPECT_EQ(result.exit_code, 3);
  EXPECT_NE(result.stdout_text.find("total_corrupted=1"), std::string::npos);
  EXPECT_NE(result.stderr_text.find("corrupted entries detected"), std::string::npos);

  std::filesystem::remove_all(db_path, ec);
}

}  // namespace
}  // namespace mir2::storage_engine::utils::storage_admin
