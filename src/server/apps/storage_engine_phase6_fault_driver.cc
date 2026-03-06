#include "apps/storage_engine_phase6_fault_driver.h"

#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "rocksdb/db.h"
#include "rocksdb/utilities/db_ttl.h"
#include "common/crypto_utils.h"
#include "storage_engine/backends/common/account_storage_codec.h"
#include "storage_engine/storage_engine.h"

namespace mir2::apps {
namespace {

using ::mir2::storage_engine::DeleteOptions;
using ::mir2::storage_engine::IStorageBackend;
using ::mir2::storage_engine::Priority;
using ::mir2::storage_engine::StorageEngine;

struct Options {
  std::string scenario;
  std::string kill_point = "prepare_ready";
  std::string db_path;
  std::string backend_state_path;
  std::string key;
  std::string value_hex = "01020304";
  std::string ready_file;
  uint32_t sleep_ms = 0;
  uint32_t timeout_ms = 5000;
};

struct BackendEntry {
  uint64_t version = 0;
  bool deleted = false;
  std::vector<uint8_t> data;
};

constexpr const char* kCfDataPersistent = "cf_data_persistent";
constexpr const char* kCfDataTtl = "cf_data_ttl";
constexpr const char* kCfOutbox = "cf_outbox";
constexpr const char* kCfDeadLetter = "cf_dead_letter";
constexpr const char* kCfMeta = "cf_meta";

std::string BuildUsageText(const char* argv0) {
  const std::string program =
      (argv0 != nullptr && std::string(argv0).size() > 0)
          ? std::string(argv0)
          : "mir2_storage_engine_phase6_fault_driver";
  std::ostringstream out;
  out << "Usage: " << program << " --scenario <name> --db-path <path> "
      << "--backend-state-path <path> [options]\n"
      << "\n"
      << "Scenarios:\n"
      << "  durable_async_prepare\n"
      << "  durable_async_recover\n"
      << "  tombstone_gc_prepare\n"
      << "  tombstone_gc_recover\n"
      << "\n"
      << "Options:\n"
      << "  --scenario <name>             Scenario name (required)\n"
      << "  --kill-point <name>           Kill point label (default: prepare_ready)\n"
      << "  --db-path <path>              RocksDB path (required)\n"
      << "  --backend-state-path <path>   File-backed backend state path (required)\n"
      << "  --key <key>                   Drill key\n"
      << "  --value-hex <hex>             Drill value payload in hex\n"
      << "  --ready-file <path>           Write marker when prepare step is ready\n"
      << "  --sleep-ms <n>                Prepare-side hold duration\n"
      << "  --timeout-ms <n>              Recover-side wait timeout\n"
      << "  -h, --help                    Show help\n";
  return out.str();
}

bool IsHexDigit(char ch) {
  return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
}

std::optional<uint32_t> ParseUint32(const std::string& value) {
  try {
    return static_cast<uint32_t>(std::stoul(value));
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<std::vector<uint8_t>> DecodeHex(const std::string& value) {
  if (value.size() % 2 != 0) {
    return std::nullopt;
  }
  std::vector<uint8_t> out;
  out.reserve(value.size() / 2);
  for (size_t i = 0; i < value.size(); i += 2) {
    if (!IsHexDigit(value[i]) || !IsHexDigit(value[i + 1])) {
      return std::nullopt;
    }
    const auto byte = static_cast<uint8_t>(
        std::stoul(value.substr(i, 2), nullptr, 16));
    out.push_back(byte);
  }
  return out;
}

std::string EncodeHex(const std::vector<uint8_t>& value) {
  static constexpr char kHexDigits[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(value.size() * 2);
  for (const uint8_t byte : value) {
    hex.push_back(kHexDigits[(byte >> 4) & 0x0F]);
    hex.push_back(kHexDigits[byte & 0x0F]);
  }
  return hex;
}

bool ParseArgs(int argc,
               char** argv,
               Options* options,
               std::string* error) {
  if (options == nullptr || error == nullptr) {
    return false;
  }
  *error = "";

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      return false;
    }

    auto require_value = [&](const char* flag) -> std::optional<std::string> {
      if (i + 1 >= argc) {
        *error = std::string("missing value for ") + flag;
        return std::nullopt;
      }
      ++i;
      return std::string(argv[i]);
    };

    if (arg == "--scenario") {
      auto value = require_value("--scenario");
      if (!value.has_value()) {
        return false;
      }
      options->scenario = *value;
      continue;
    }
    if (arg == "--kill-point") {
      auto value = require_value("--kill-point");
      if (!value.has_value()) {
        return false;
      }
      options->kill_point = *value;
      continue;
    }
    if (arg == "--db-path") {
      auto value = require_value("--db-path");
      if (!value.has_value()) {
        return false;
      }
      options->db_path = *value;
      continue;
    }
    if (arg == "--backend-state-path") {
      auto value = require_value("--backend-state-path");
      if (!value.has_value()) {
        return false;
      }
      options->backend_state_path = *value;
      continue;
    }
    if (arg == "--key") {
      auto value = require_value("--key");
      if (!value.has_value()) {
        return false;
      }
      options->key = *value;
      continue;
    }
    if (arg == "--value-hex") {
      auto value = require_value("--value-hex");
      if (!value.has_value()) {
        return false;
      }
      options->value_hex = *value;
      continue;
    }
    if (arg == "--ready-file") {
      auto value = require_value("--ready-file");
      if (!value.has_value()) {
        return false;
      }
      options->ready_file = *value;
      continue;
    }
    if (arg == "--sleep-ms") {
      auto value = require_value("--sleep-ms");
      if (!value.has_value()) {
        return false;
      }
      auto parsed = ParseUint32(*value);
      if (!parsed.has_value()) {
        *error = "invalid --sleep-ms";
        return false;
      }
      options->sleep_ms = *parsed;
      continue;
    }
    if (arg == "--timeout-ms") {
      auto value = require_value("--timeout-ms");
      if (!value.has_value()) {
        return false;
      }
      auto parsed = ParseUint32(*value);
      if (!parsed.has_value()) {
        *error = "invalid --timeout-ms";
        return false;
      }
      options->timeout_ms = *parsed;
      continue;
    }

    *error = "unknown argument: " + arg;
    return false;
  }

  if (options->scenario.empty()) {
    *error = "--scenario is required";
    return false;
  }
  if (options->db_path.empty()) {
    *error = "--db-path is required";
    return false;
  }
  if (options->backend_state_path.empty()) {
    *error = "--backend-state-path is required";
    return false;
  }
  if (options->key.empty()) {
    if (options->scenario.find("durable_async") != std::string::npos) {
      options->key = "phase6:durable_async:key";
    } else if (options->scenario.find("login_select_move_disconnect") !=
               std::string::npos) {
      options->key = mir2::db::BuildAccountStorageKey("phase6_user");
    } else if (options->scenario.find("checkpoint_restore") !=
               std::string::npos) {
      options->key = "phase6:checkpoint_restore:key";
    } else if (options->scenario.find("startup_validation") !=
               std::string::npos) {
      options->key = "phase6:startup_validation:key";
    } else {
      options->key = "phase6:tombstone_gc:key";
    }
  }
  if (!DecodeHex(options->value_hex).has_value()) {
    *error = "invalid --value-hex";
    return false;
  }
  return true;
}

class FileBackedBackend : public IStorageBackend {
 public:
  FileBackedBackend(std::string state_path, bool healthy)
      : state_path_(std::move(state_path)), healthy_(healthy) {}

  StorageResult Save(const std::string& key,
                     uint64_t version,
                     const std::vector<uint8_t>& data) override {
    if (!healthy_) {
      return StorageResult{false, "backend unhealthy", 0};
    }
    std::lock_guard<std::mutex> lock(mutex_);
    LoadStateLocked();
    auto& entry = state_[key];
    if (entry.version > version) {
      return StorageResult{true, "", 0};
    }
    entry.version = version;
    entry.deleted = false;
    entry.data = data;
    return PersistStateLocked() ? StorageResult{true, "", 0}
                                : StorageResult{false, "persist failed", 0};
  }

  StorageResult SaveBatch(const std::vector<std::tuple<std::string, uint64_t,
                                                       std::vector<uint8_t>>>& items) override {
    if (!healthy_) {
      return StorageResult{false, "backend unhealthy", 0};
    }
    std::lock_guard<std::mutex> lock(mutex_);
    LoadStateLocked();
    for (const auto& [key, version, data] : items) {
      auto& entry = state_[key];
      if (entry.version > version) {
        continue;
      }
      entry.version = version;
      entry.deleted = false;
      entry.data = data;
    }
    return PersistStateLocked() ? StorageResult{true, "", 0}
                                : StorageResult{false, "persist failed", 0};
  }

  StorageResult Delete(const std::string& key,
                       uint64_t version,
                       bool hard_delete) override {
    if (!healthy_) {
      return StorageResult{false, "backend unhealthy", 0};
    }
    std::lock_guard<std::mutex> lock(mutex_);
    LoadStateLocked();
    auto it = state_.find(key);
    if (hard_delete) {
      if (it != state_.end() && it->second.version <= version) {
        state_.erase(it);
      }
    } else {
      if (it == state_.end()) {
        state_[key] = BackendEntry{
            .version = version,
            .deleted = true,
            .data = {},
        };
      } else if (it->second.version <= version) {
        it->second.version = version;
        it->second.deleted = true;
        it->second.data.clear();
      }
    }
    return PersistStateLocked() ? StorageResult{true, "", 0}
                                : StorageResult{false, "persist failed", 0};
  }

  StorageResult DeleteBatch(
      const std::vector<std::pair<std::string, uint64_t>>& items,
      bool hard_delete) override {
    if (!healthy_) {
      return StorageResult{false, "backend unhealthy", 0};
    }
    std::lock_guard<std::mutex> lock(mutex_);
    LoadStateLocked();
    for (const auto& [key, version] : items) {
      auto it = state_.find(key);
      if (hard_delete) {
        if (it != state_.end() && it->second.version <= version) {
          state_.erase(it);
        }
      } else {
        if (it == state_.end()) {
          state_[key] = BackendEntry{
              .version = version,
              .deleted = true,
              .data = {},
          };
        } else if (it->second.version <= version) {
          it->second.version = version;
          it->second.deleted = true;
          it->second.data.clear();
        }
      }
    }
    return PersistStateLocked() ? StorageResult{true, "", 0}
                                : StorageResult{false, "persist failed", 0};
  }

  std::optional<std::pair<uint64_t, std::vector<uint8_t>>> Load(
      const std::string& key) override {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadStateLocked();
    const auto it = state_.find(key);
    if (it == state_.end() || it->second.deleted) {
      return std::nullopt;
    }
    return std::make_pair(it->second.version, it->second.data);
  }

  std::optional<std::map<std::string, std::pair<uint64_t, std::vector<uint8_t>>>> LoadAll()
      override {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadStateLocked();
    std::map<std::string, std::pair<uint64_t, std::vector<uint8_t>>> out;
    for (const auto& [key, entry] : state_) {
      if (!entry.deleted) {
        out[key] = {entry.version, entry.data};
      }
    }
    return out;
  }

  StorageResult Validate() override {
    return StorageResult{true, "", 0};
  }

  bool IsHealthy() const override {
    return healthy_;
  }

  bool HasVisibleKey(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadStateLocked();
    const auto it = state_.find(key);
    return it != state_.end() && !it->second.deleted;
  }

 private:
  static std::string EncodeHex(const std::vector<uint8_t>& data) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t byte : data) {
      out.push_back(kHex[(byte >> 4) & 0x0F]);
      out.push_back(kHex[byte & 0x0F]);
    }
    return out;
  }

  void LoadStateLocked() {
    state_.clear();
    std::ifstream in(state_path_);
    if (!in.is_open()) {
      return;
    }
    std::string line;
    while (std::getline(in, line)) {
      std::istringstream row(line);
      std::string key;
      std::string version_str;
      std::string deleted_str;
      std::string hex_data;
      if (!std::getline(row, key, '\t') ||
          !std::getline(row, version_str, '\t') ||
          !std::getline(row, deleted_str, '\t') ||
          !std::getline(row, hex_data)) {
        continue;
      }
      const auto decoded = DecodeHex(hex_data);
      if (!decoded.has_value()) {
        continue;
      }
      BackendEntry entry;
      entry.version = static_cast<uint64_t>(std::stoull(version_str));
      entry.deleted = deleted_str == "1";
      entry.data = *decoded;
      state_[key] = std::move(entry);
    }
  }

  bool PersistStateLocked() const {
    std::filesystem::create_directories(
        std::filesystem::path(state_path_).parent_path());
    std::ofstream out(state_path_, std::ios::trunc);
    if (!out.is_open()) {
      return false;
    }
    for (const auto& [key, entry] : state_) {
      out << key << '\t'
          << entry.version << '\t'
          << (entry.deleted ? "1" : "0") << '\t'
          << EncodeHex(entry.data) << '\n';
    }
    return true;
  }

  std::string state_path_;
  bool healthy_ = true;
  mutable std::mutex mutex_;
  std::map<std::string, BackendEntry> state_;
};

void TouchReadyFile(const std::string& path) {
  if (path.empty()) {
    return;
  }
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::ofstream out(path);
  out << "ready\n";
}

StorageEngine::Config BuildConfigForScenario(std::string_view scenario,
                                             const std::string& db_path) {
  StorageEngine::Config config;
  config.l2_path = db_path;
  config.enable_strict_write_guarantee = false;
  config.enable_metrics = false;
  config.auto_sync_interval_ms = 10;
  config.batch_size = 1;
  if (scenario == "durable_async_prepare" ||
      scenario == "durable_async_recover") {
    config.enable_outbox = true;
  }
  if (scenario == "checkpoint_restore_prepare" ||
      scenario == "checkpoint_restore_recover") {
    config.enable_v2_encode = true;
    config.enable_v2_read_fallback = true;
  }
  if (scenario == "startup_validation_prepare" ||
      scenario == "startup_validation_recover") {
    config.enable_v2_encode = true;
    config.enable_v2_read_fallback = true;
  }
  if (scenario == "login_select_move_disconnect_prepare" ||
      scenario == "login_select_move_disconnect_verify") {
    config.enable_v2_encode = false;
    config.enable_v2_read_fallback = true;
  }
  if (scenario == "startup_validation_recover") {
    config.startup_fail_on_validation_error = true;
  }
  if (scenario == "tombstone_gc_prepare") {
    config.tombstone_retention_seconds = 2;
    config.tombstone_gc_interval_seconds = 60;
  }
  if (scenario == "tombstone_gc_recover") {
    config.tombstone_retention_seconds = 2;
    config.tombstone_gc_interval_seconds = 1;
  }
  return config;
}

bool WaitUntil(std::chrono::milliseconds timeout,
               const std::function<bool()>& predicate) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return predicate();
}

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
  status = db->Get(rocksdb::ReadOptions{}, target, key, &value);
  if (!status.ok() || value.empty()) {
    DestroyL2Handles(db.get(), handles);
    return false;
  }
  value.back() = static_cast<char>(value.back() ^ 0xFF);
  status = db->Put(rocksdb::WriteOptions{}, target, key, value);
  DestroyL2Handles(db.get(), handles);
  return status.ok();
}

int RunPrepareScenario(const Options& options,
                       const std::vector<uint8_t>& payload,
                       std::ostream* out,
                       std::ostream* err) {
  const bool backend_healthy =
      options.scenario == "tombstone_gc_prepare" ||
      options.scenario == "startup_validation_prepare" ||
      options.scenario == "login_select_move_disconnect_prepare";
  auto backend = std::make_unique<FileBackedBackend>(
      options.backend_state_path, backend_healthy);
  const auto config = BuildConfigForScenario(options.scenario, options.db_path);

  if (StorageEngine::IsInitialized()) {
    StorageEngine::Shutdown();
  }
  if (!StorageEngine::Initialize(std::move(backend), config)) {
    if (options.scenario == "startup_validation_recover") {
      if (out) {
        *out << "phase6_fault_driver_result scenario=startup_validation_recover"
             << " kill_point=" << options.kill_point
             << " status=init_failed corruption_detected=true\n";
      }
      return 0;
    }
    if (err) {
      *err << "failed to initialize storage engine\n";
    }
    return 1;
  }

  if (options.scenario == "startup_validation_recover") {
    if (out) {
      *out << "phase6_fault_driver_result scenario=startup_validation_recover"
           << " kill_point=" << options.kill_point
           << " status=unexpected_init_success corruption_detected=false\n";
    }
    StorageEngine::Shutdown();
    return 1;
  }

  auto& engine = StorageEngine::Instance();
  if (options.scenario == "durable_async_prepare") {
    if (!engine.Set(options.key, payload, Priority::NORMAL)) {
      if (err) {
        *err << "failed to enqueue durable async write\n";
      }
      StorageEngine::Shutdown();
      return 1;
    }
  } else if (options.scenario == "checkpoint_restore_prepare") {
    if (!engine.Set(options.key, payload, Priority::NORMAL)) {
      if (err) {
        *err << "failed to write checkpoint restore seed record\n";
      }
      StorageEngine::Shutdown();
      return 1;
    }
  } else if (options.scenario == "startup_validation_prepare") {
    if (!engine.Set(options.key, payload, Priority::NORMAL)) {
      if (err) {
        *err << "failed to write startup validation seed record\n";
      }
      StorageEngine::Shutdown();
      return 1;
    }
    StorageEngine::Shutdown();
    if (!CorruptRawL2Value(options.db_path, options.key, kCfDataTtl)) {
      if (err) {
        *err << "failed to corrupt startup validation L2 record\n";
      }
      return 1;
    }
    TouchReadyFile(options.ready_file);
    if (out) {
      *out << "phase6_fault_driver_result scenario=" << options.scenario
           << " kill_point=" << options.kill_point
           << " status=ready corruption_injected=true\n";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(options.sleep_ms));
    return 0;
  } else if (options.scenario == "login_select_move_disconnect_prepare") {
    std::string username = "phase6_user";
    if (const auto parsed = mir2::db::ParseAccountStorageKey(options.key);
        parsed.has_value()) {
      username = *parsed;
    }
    mir2::db::AccountData account;
    account.username = username;
    account.password_hash = mir2::common::HashPassword("phase6_pw");
    account.email = username + "@example.com";
    account.created_at = 1700000000000;
    account.last_login = 1700000001000;
    account.banned = false;
    if (!engine.Set(options.key,
                    mir2::db::EncodeAccountData(account),
                    Priority::HIGH)) {
      if (err) {
        *err << "failed to seed phase6 gateway login account\n";
      }
      StorageEngine::Shutdown();
      return 1;
    }
    TouchReadyFile(options.ready_file);
    if (out) {
      *out << "phase6_fault_driver_result scenario=" << options.scenario
           << " kill_point=" << options.kill_point
           << " status=ready account_seeded=true key=" << options.key << "\n";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(options.sleep_ms));
    StorageEngine::Shutdown();
    return 0;
  } else {
    DeleteOptions delete_options;
    delete_options.hard_delete = false;
    delete_options.write_tombstone = true;
    if (!engine.Delete(options.key, delete_options)) {
      if (err) {
        *err << "failed to enqueue tombstone gc delete\n";
      }
      StorageEngine::Shutdown();
      return 1;
    }
  }

  TouchReadyFile(options.ready_file);
  if (out) {
    *out << "phase6_fault_driver_result scenario=" << options.scenario
         << " kill_point=" << options.kill_point
         << " status=ready\n";
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(options.sleep_ms));
  StorageEngine::Shutdown();
  return 0;
}

int RunRecoverScenario(const Options& options,
                       std::ostream* out,
                       std::ostream* err) {
  auto backend = std::make_unique<FileBackedBackend>(
      options.backend_state_path, true);
  auto* backend_ptr = backend.get();
  const auto config = BuildConfigForScenario(options.scenario, options.db_path);

  if (StorageEngine::IsInitialized()) {
    StorageEngine::Shutdown();
  }
  if (!StorageEngine::Initialize(std::move(backend), config)) {
    if (options.scenario == "startup_validation_recover") {
      if (out) {
        *out << "phase6_fault_driver_result scenario=startup_validation_recover"
             << " kill_point=" << options.kill_point
             << " status=init_failed corruption_detected=true\n";
      }
      return 0;
    }
    if (err) {
      *err << "failed to initialize storage engine\n";
    }
    return 1;
  }

  if (options.scenario == "startup_validation_recover") {
    if (out) {
      *out << "phase6_fault_driver_result scenario=startup_validation_recover"
           << " kill_point=" << options.kill_point
           << " status=unexpected_init_success corruption_detected=false\n";
    }
    StorageEngine::Shutdown();
    return 1;
  }

  auto& engine = StorageEngine::Instance();

  if (options.scenario == "login_select_move_disconnect_verify") {
    const auto stored = engine.Get(options.key);
    const bool present = stored.has_value();
    if (out) {
      *out << "phase6_fault_driver_verify_result"
           << " scenario=login_select_move_disconnect_verify"
           << " kill_point=" << options.kill_point
           << " snapshot_present=" << (present ? "true" : "false")
           << " snapshot_version=" << (present ? stored->version : 0)
           << " snapshot_hex="
           << (present ? EncodeHex(stored->data) : std::string())
           << " key=" << options.key << "\n";
    }
    StorageEngine::Shutdown();
    return present ? 0 : 1;
  }

  if (options.kill_point == "recover_wait") {
    TouchReadyFile(options.ready_file);
  }
  const bool ready = WaitUntil(
      std::chrono::milliseconds(options.timeout_ms),
      [&]() {
        const auto metrics = engine.GetHealthMetrics();
        if (options.scenario == "durable_async_recover") {
          return backend_ptr->HasVisibleKey(options.key) &&
                 metrics.outbox_depth == 0;
        }
        if (options.scenario == "checkpoint_restore_recover") {
          const auto restored = engine.Get(options.key);
          return restored.has_value();
        }
        return metrics.tombstone_gc_pending == 0 &&
               metrics.tombstone_gc_reclaimed_total >= 1 &&
               metrics.tombstone_gc_failed_total == 0;
      });

  const auto metrics = engine.GetHealthMetrics();
  if (out) {
    if (options.scenario == "durable_async_recover") {
      *out << "phase6_fault_driver_result scenario=durable_async_recover"
           << " kill_point=" << options.kill_point
           << " status=" << (ready ? "ok" : "timeout")
           << " backend_key_present="
           << (backend_ptr->HasVisibleKey(options.key) ? "true" : "false")
           << " outbox_depth=" << metrics.outbox_depth
           << "\n";
    } else if (options.scenario == "checkpoint_restore_recover") {
      const auto restored = engine.Get(options.key);
      *out << "phase6_fault_driver_result scenario=checkpoint_restore_recover"
           << " kill_point=" << options.kill_point
           << " status=" << (ready ? "ok" : "timeout")
           << " restored_key_present="
           << (restored.has_value() ? "true" : "false")
           << "\n";
    } else {
      *out << "phase6_fault_driver_result scenario=tombstone_gc_recover"
           << " kill_point=" << options.kill_point
           << " status=" << (ready ? "ok" : "timeout")
           << " tombstone_gc_pending=" << metrics.tombstone_gc_pending
           << " tombstone_gc_reclaimed_total="
           << metrics.tombstone_gc_reclaimed_total
           << " tombstone_gc_failed_total="
           << metrics.tombstone_gc_failed_total
           << "\n";
    }
  }
  if (!ready && err) {
    *err << "recover scenario timed out\n";
  }
  StorageEngine::Shutdown();
  return ready ? 0 : 1;
}

}  // namespace

int RunStorageEnginePhase6FaultDriver(int argc,
                                      char** argv,
                                      std::ostream* out,
                                      std::ostream* err) {
  Options options;
  std::string error;
  if (!ParseArgs(argc, argv, &options, &error)) {
    const std::string usage = BuildUsageText(argc > 0 ? argv[0] : nullptr);
    if (!error.empty()) {
      if (err) {
        *err << "Error: " << error << "\n\n" << usage;
      }
      return 1;
    }
    if (out) {
      *out << usage;
    }
    return 0;
  }

  const auto payload = DecodeHex(options.value_hex);
  if (!payload.has_value()) {
    if (err) {
      *err << "failed to decode payload hex\n";
    }
    return 1;
  }

  if (options.scenario == "durable_async_prepare" ||
      options.scenario == "checkpoint_restore_prepare" ||
      options.scenario == "tombstone_gc_prepare" ||
      options.scenario == "startup_validation_prepare" ||
      options.scenario == "login_select_move_disconnect_prepare") {
    return RunPrepareScenario(options, *payload, out, err);
  }
  if (options.scenario == "durable_async_recover" ||
      options.scenario == "checkpoint_restore_recover" ||
      options.scenario == "tombstone_gc_recover" ||
      options.scenario == "startup_validation_recover" ||
      options.scenario == "login_select_move_disconnect_verify") {
    return RunRecoverScenario(options, out, err);
  }

  if (err) {
    *err << "unsupported scenario: " << options.scenario << "\n";
  }
  return 1;
}

}  // namespace mir2::apps
