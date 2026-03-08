#include "storage_engine/utils/storage_admin_tool.h"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "ecs/character_snapshot_codec.h"
#include "rocksdb/db.h"
#include "rocksdb/utilities/checkpoint.h"
#include "storage_engine/l2/rocksdb_cache.h"

namespace mir2::storage_engine::utils::storage_admin {
namespace {

constexpr int kExitInitFailed = 2;
constexpr int kExitCorruptionDetected = 3;
constexpr int kExitCheckpointFailed = 4;
constexpr int kExitReplayFailed = 5;

constexpr const char* kL2CfDataPersistent = "cf_data_persistent";
constexpr const char* kL2CfDataTtl = "cf_data_ttl";
constexpr const char* kL2CfOutbox = "cf_outbox";
constexpr const char* kL2CfDeadLetter = "cf_dead_letter";
constexpr const char* kL2CfMeta = "cf_meta";

std::optional<uint64_t> ParseUint64(const std::string& value) {
  try {
    return std::stoull(value);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<int> ParseInt(const std::string& value) {
  try {
    return std::stoi(value);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::vector<uint8_t>> DecodeHex(std::string_view value) {
  if (value.empty() || value.size() % 2 != 0) {
    return std::nullopt;
  }

  auto decode_nibble = [](char c) -> std::optional<uint8_t> {
    if (c >= '0' && c <= '9') {
      return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
      return static_cast<uint8_t>(10 + (c - 'a'));
    }
    if (c >= 'A' && c <= 'F') {
      return static_cast<uint8_t>(10 + (c - 'A'));
    }
    return std::nullopt;
  };

  std::vector<uint8_t> bytes;
  bytes.reserve(value.size() / 2);
  for (size_t i = 0; i < value.size(); i += 2) {
    const auto high = decode_nibble(value[i]);
    const auto low = decode_nibble(value[i + 1]);
    if (!high.has_value() || !low.has_value()) {
      return std::nullopt;
    }
    bytes.push_back(static_cast<uint8_t>((*high << 4) | *low));
  }
  return bytes;
}

std::optional<bool> ParseBool(const std::string& value) {
  std::string lowered;
  lowered.reserve(value.size());
  for (const char ch : value) {
    lowered.push_back(static_cast<char>(std::tolower(
        static_cast<unsigned char>(ch))));
  }
  if (lowered == "1" || lowered == "true" || lowered == "yes") {
    return true;
  }
  if (lowered == "0" || lowered == "false" || lowered == "no") {
    return false;
  }
  return std::nullopt;
}

bool OpenDbForCheckpoint(
    const std::string& db_path,
    std::unique_ptr<rocksdb::DB>* db,
    std::vector<rocksdb::ColumnFamilyHandle*>* handles,
    std::string* error) {
  if (db == nullptr || handles == nullptr || error == nullptr) {
    return false;
  }

  std::vector<rocksdb::ColumnFamilyDescriptor> descriptors{
      {rocksdb::kDefaultColumnFamilyName, rocksdb::ColumnFamilyOptions{}},
      {kL2CfDataPersistent, rocksdb::ColumnFamilyOptions{}},
      {kL2CfDataTtl, rocksdb::ColumnFamilyOptions{}},
      {kL2CfOutbox, rocksdb::ColumnFamilyOptions{}},
      {kL2CfDeadLetter, rocksdb::ColumnFamilyOptions{}},
      {kL2CfMeta, rocksdb::ColumnFamilyOptions{}},
  };

  rocksdb::DBOptions db_options;
  db_options.create_if_missing = false;
  db_options.create_missing_column_families = false;
  rocksdb::DB* raw_db = nullptr;
  std::vector<rocksdb::ColumnFamilyHandle*> raw_handles;
  const rocksdb::Status status = rocksdb::DB::Open(
      db_options, db_path, descriptors, &raw_handles, &raw_db);
  if (!status.ok()) {
    *error = "open db failed: " + status.ToString();
    return false;
  }
  db->reset(raw_db);
  *handles = std::move(raw_handles);
  return true;
}

void DestroyDbHandles(rocksdb::DB* db,
                      std::vector<rocksdb::ColumnFamilyHandle*>* handles) {
  if (db == nullptr || handles == nullptr) {
    return;
  }
  for (auto* handle : *handles) {
    if (handle != nullptr) {
      db->DestroyColumnFamilyHandle(handle);
    }
  }
  handles->clear();
}

bool RequirePath(const std::string& value,
                 const char* field_name,
                 std::string* error) {
  if (!value.empty()) {
    return true;
  }
  if (error != nullptr && field_name != nullptr) {
    *error = std::string(field_name) + " is required";
  }
  return false;
}

bool MatchesDeadLetterFilter(
    const l2::RocksDBCache::DeadLetterEntry& entry,
    const CommandOptions& options) {
  if (!options.key_prefix.empty() &&
      !std::string_view(entry.key).starts_with(options.key_prefix)) {
    return false;
  }
  if (entry.recorded_at_ms < options.start_ms ||
      entry.recorded_at_ms > options.end_ms) {
    return false;
  }
  return true;
}

}  // namespace

std::string BuildUsage(const char* argv0) {
  const std::string program =
      (argv0 != nullptr && std::string(argv0).size() > 0)
          ? std::string(argv0)
          : "mir2_storage_admin";
  std::ostringstream out;
  out << "Usage: " << program
      << " <validate|health|checkpoint-create|checkpoint-restore|dead-letter-replay|decode-character-snapshot> [options]\n"
      << "\n"
      << "Options:\n"
      << "  --db-path <path>                RocksDB path (required)\n"
      << "  --hex <hex>                     Hex-encoded character snapshot (decode-character-snapshot)\n"
      << "  --output-path <path>            Checkpoint output path (checkpoint-create)\n"
      << "  --checkpoint-path <path>        Source checkpoint path (checkpoint-restore)\n"
      << "  --restore-db-path <path>        Restore target DB path (checkpoint-restore)\n"
      << "  --expected-x <x>                Expected character x position (decode-character-snapshot)\n"
      << "  --expected-y <y>                Expected character y position (decode-character-snapshot)\n"
      << "  --prefix <key_prefix>           Dead-letter key prefix filter (dead-letter-replay)\n"
      << "  --start-ms <ts_ms>              Dead-letter start timestamp filter (dead-letter-replay)\n"
      << "  --end-ms <ts_ms>                Dead-letter end timestamp filter (dead-letter-replay)\n"
      << "  --limit <n>                     Max dead-letter rows scanned (0 = all)\n"
      << "  --ttl-seconds <n>               L2 TTL seconds (default: 3600)\n"
      << "  --enable-v2-encode <bool>       Read/write codec v2 toggle (default: false)\n"
      << "  --enable-v2-read-fallback <bool> Allow v1 fallback decode (default: true)\n"
      << "  --dry-run                       Scan and print dead letters without replaying\n"
      << "  --keep-dead-letter              Keep dead-letter rows after replay\n"
      << "  --verbose                       Print matched dead-letter rows\n"
      << "  --overwrite                     Overwrite destination path when exists\n"
      << "  -h, --help                      Show help\n";
  return out.str();
}

bool ParseCommandLine(int argc,
                      char** argv,
                      CommandOptions* options,
                      std::string* error) {
  if (options == nullptr || error == nullptr) {
    return false;
  }
  *error = "";
  if (argc <= 1) {
    *error = "missing subcommand";
    return false;
  }

  const std::string subcommand = argv[1];
  if (subcommand == "-h" || subcommand == "--help") {
    return false;
  }
  if (subcommand == "validate") {
    options->command = Command::kValidate;
  } else if (subcommand == "health") {
    options->command = Command::kHealth;
  } else if (subcommand == "checkpoint-create") {
    options->command = Command::kCheckpointCreate;
  } else if (subcommand == "checkpoint-restore") {
    options->command = Command::kCheckpointRestore;
  } else if (subcommand == "dead-letter-replay") {
    options->command = Command::kDeadLetterReplay;
  } else if (subcommand == "decode-character-snapshot") {
    options->command = Command::kDecodeCharacterSnapshot;
  } else {
    *error = "unknown subcommand: " + subcommand;
    return false;
  }

  for (int i = 2; i < argc; ++i) {
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
    if (arg == "--db-path") {
      auto value = require_value("--db-path");
      if (!value.has_value()) {
        return false;
      }
      options->db_path = *value;
      continue;
    }
    if (arg == "--output-path") {
      auto value = require_value("--output-path");
      if (!value.has_value()) {
        return false;
      }
      options->output_path = *value;
      continue;
    }
    if (arg == "--checkpoint-path") {
      auto value = require_value("--checkpoint-path");
      if (!value.has_value()) {
        return false;
      }
      options->checkpoint_path = *value;
      continue;
    }
    if (arg == "--restore-db-path") {
      auto value = require_value("--restore-db-path");
      if (!value.has_value()) {
        return false;
      }
      options->restore_db_path = *value;
      continue;
    }
    if (arg == "--prefix") {
      auto value = require_value("--prefix");
      if (!value.has_value()) {
        return false;
      }
      options->key_prefix = *value;
      continue;
    }
    if (arg == "--hex") {
      auto value = require_value("--hex");
      if (!value.has_value()) {
        return false;
      }
      options->snapshot_hex = *value;
      continue;
    }
    if (arg == "--start-ms") {
      auto value = require_value("--start-ms");
      if (!value.has_value()) {
        return false;
      }
      auto parsed = ParseUint64(*value);
      if (!parsed.has_value()) {
        *error = "invalid --start-ms";
        return false;
      }
      options->start_ms = *parsed;
      continue;
    }
    if (arg == "--end-ms") {
      auto value = require_value("--end-ms");
      if (!value.has_value()) {
        return false;
      }
      auto parsed = ParseUint64(*value);
      if (!parsed.has_value()) {
        *error = "invalid --end-ms";
        return false;
      }
      options->end_ms = *parsed;
      continue;
    }
    if (arg == "--limit") {
      auto value = require_value("--limit");
      if (!value.has_value()) {
        return false;
      }
      auto parsed = ParseUint64(*value);
      if (!parsed.has_value() ||
          *parsed > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        *error = "invalid --limit";
        return false;
      }
      options->limit = static_cast<size_t>(*parsed);
      continue;
    }
    if (arg == "--ttl-seconds") {
      auto value = require_value("--ttl-seconds");
      if (!value.has_value()) {
        return false;
      }
      auto parsed = ParseUint64(*value);
      if (!parsed.has_value() ||
          *parsed > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
        *error = "invalid --ttl-seconds";
        return false;
      }
      options->ttl_seconds = static_cast<uint32_t>(*parsed);
      continue;
    }
    if (arg == "--expected-x") {
      auto value = require_value("--expected-x");
      if (!value.has_value()) {
        return false;
      }
      auto parsed = ParseInt(*value);
      if (!parsed.has_value()) {
        *error = "invalid --expected-x";
        return false;
      }
      options->expected_x = *parsed;
      continue;
    }
    if (arg == "--expected-y") {
      auto value = require_value("--expected-y");
      if (!value.has_value()) {
        return false;
      }
      auto parsed = ParseInt(*value);
      if (!parsed.has_value()) {
        *error = "invalid --expected-y";
        return false;
      }
      options->expected_y = *parsed;
      continue;
    }
    if (arg == "--enable-v2-encode") {
      auto value = require_value("--enable-v2-encode");
      if (!value.has_value()) {
        return false;
      }
      auto parsed = ParseBool(*value);
      if (!parsed.has_value()) {
        *error = "invalid --enable-v2-encode";
        return false;
      }
      options->enable_v2_encode = *parsed;
      continue;
    }
    if (arg == "--enable-v2-read-fallback") {
      auto value = require_value("--enable-v2-read-fallback");
      if (!value.has_value()) {
        return false;
      }
      auto parsed = ParseBool(*value);
      if (!parsed.has_value()) {
        *error = "invalid --enable-v2-read-fallback";
        return false;
      }
      options->enable_v2_read_fallback = *parsed;
      continue;
    }
    if (arg == "--overwrite") {
      options->overwrite = true;
      continue;
    }
    if (arg == "--dry-run") {
      options->dry_run = true;
      continue;
    }
    if (arg == "--keep-dead-letter") {
      options->keep_dead_letter = true;
      continue;
    }
    if (arg == "--verbose") {
      options->verbose = true;
      continue;
    }

    *error = "unknown argument: " + arg;
    return false;
  }

  switch (options->command) {
    case Command::kValidate:
    case Command::kHealth:
      if (!RequirePath(options->db_path, "--db-path", error)) {
        return false;
      }
      break;
    case Command::kCheckpointCreate:
      if (!RequirePath(options->db_path, "--db-path", error)) {
        return false;
      }
      if (!RequirePath(options->output_path, "--output-path", error)) {
        return false;
      }
      break;
    case Command::kCheckpointRestore:
      if (!RequirePath(options->checkpoint_path, "--checkpoint-path", error)) {
        return false;
      }
      if (!RequirePath(options->restore_db_path, "--restore-db-path", error)) {
        return false;
      }
      break;
    case Command::kDeadLetterReplay:
      if (!RequirePath(options->db_path, "--db-path", error)) {
        return false;
      }
      if (options->start_ms > options->end_ms) {
        *error = "--start-ms must be <= --end-ms";
        return false;
      }
      break;
    case Command::kDecodeCharacterSnapshot:
      if (options->snapshot_hex.empty()) {
        *error = "--hex is required";
        return false;
      }
      break;
  }
  return true;
}

CommandResult Execute(const CommandOptions& options) {
  CommandResult result;

  if (options.command == Command::kDecodeCharacterSnapshot) {
    const auto snapshot_bytes = DecodeHex(options.snapshot_hex);
    if (!snapshot_bytes.has_value()) {
      result.exit_code = 1;
      result.stderr_text = "invalid snapshot hex\n";
      return result;
    }

    const auto snapshot = mir2::ecs::DeserializeCharacterSnapshot(
        snapshot_bytes->data(), snapshot_bytes->size());
    if (!snapshot.has_value()) {
      result.exit_code = 1;
      result.stderr_text = "failed to decode character snapshot\n";
      return result;
    }

    const bool position_matches =
        snapshot->position.x == options.expected_x &&
        snapshot->position.y == options.expected_y;
    std::ostringstream out;
    out << "storage_admin_decode_character_snapshot"
        << " actual_x=" << snapshot->position.x
        << " actual_y=" << snapshot->position.y
        << " position_matches=" << (position_matches ? "true" : "false")
        << "\n";
    result.stdout_text = out.str();
    result.exit_code = 0;
    return result;
  }

  if (options.command == Command::kCheckpointCreate) {
    std::error_code fs_error;
    const std::filesystem::path checkpoint_path(options.output_path);
    if (checkpoint_path.empty()) {
      result.exit_code = 1;
      result.stderr_text = "--output-path is required\n";
      return result;
    }
    if (std::filesystem::exists(checkpoint_path, fs_error)) {
      if (!options.overwrite) {
        result.exit_code = kExitCheckpointFailed;
        result.stderr_text =
            "checkpoint output path already exists (use --overwrite): " +
            options.output_path + "\n";
        return result;
      }
      std::filesystem::remove_all(checkpoint_path, fs_error);
      if (fs_error) {
        result.exit_code = kExitCheckpointFailed;
        result.stderr_text =
            "failed to clear output path: " + fs_error.message() + "\n";
        return result;
      }
    }

    std::unique_ptr<rocksdb::DB> db;
    std::vector<rocksdb::ColumnFamilyHandle*> handles;
    std::string open_error;
    if (!OpenDbForCheckpoint(options.db_path, &db, &handles, &open_error)) {
      result.exit_code = kExitInitFailed;
      result.stderr_text = open_error + "\n";
      return result;
    }

    rocksdb::Checkpoint* raw_checkpoint = nullptr;
    rocksdb::Status status = rocksdb::Checkpoint::Create(db.get(),
                                                         &raw_checkpoint);
    std::unique_ptr<rocksdb::Checkpoint> checkpoint(raw_checkpoint);
    if (!status.ok()) {
      DestroyDbHandles(db.get(), &handles);
      result.exit_code = kExitCheckpointFailed;
      result.stderr_text =
          "create checkpoint handle failed: " + status.ToString() + "\n";
      return result;
    }

    status = checkpoint->CreateCheckpoint(options.output_path);
    DestroyDbHandles(db.get(), &handles);
    if (!status.ok()) {
      result.exit_code = kExitCheckpointFailed;
      result.stderr_text =
          "create checkpoint failed: " + status.ToString() + "\n";
      return result;
    }

    std::ostringstream out;
    out << "storage_admin_checkpoint_create_summary"
        << " db_path=" << options.db_path
        << " output_path=" << options.output_path
        << " status=ok\n";
    result.stdout_text = out.str();
    result.exit_code = 0;
    return result;
  }

  if (options.command == Command::kCheckpointRestore) {
    std::error_code fs_error;
    const std::filesystem::path checkpoint_path(options.checkpoint_path);
    const std::filesystem::path restore_path(options.restore_db_path);

    if (!std::filesystem::exists(checkpoint_path, fs_error)) {
      result.exit_code = kExitCheckpointFailed;
      result.stderr_text = "checkpoint path does not exist: " +
                           options.checkpoint_path + "\n";
      return result;
    }
    if (std::filesystem::exists(restore_path, fs_error)) {
      if (!options.overwrite) {
        result.exit_code = kExitCheckpointFailed;
        result.stderr_text =
            "restore target already exists (use --overwrite): " +
            options.restore_db_path + "\n";
        return result;
      }
      std::filesystem::remove_all(restore_path, fs_error);
      if (fs_error) {
        result.exit_code = kExitCheckpointFailed;
        result.stderr_text =
            "failed to clear restore target: " + fs_error.message() + "\n";
        return result;
      }
    }

    const auto parent = restore_path.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent, fs_error);
      if (fs_error) {
        result.exit_code = kExitCheckpointFailed;
        result.stderr_text =
            "failed to create restore parent directory: " + fs_error.message() +
            "\n";
        return result;
      }
    }

    std::filesystem::copy(
        checkpoint_path, restore_path,
        std::filesystem::copy_options::recursive,
        fs_error);
    if (fs_error) {
      result.exit_code = kExitCheckpointFailed;
      result.stderr_text =
          "restore copy failed: " + fs_error.message() + "\n";
      return result;
    }

    std::ostringstream out;
    out << "storage_admin_checkpoint_restore_summary"
        << " checkpoint_path=" << options.checkpoint_path
        << " restore_db_path=" << options.restore_db_path
        << " status=ok\n";
    result.stdout_text = out.str();
    result.exit_code = 0;
    return result;
  }

  l2::RocksDBCache::Config config;
  config.db_path = options.db_path;
  config.ttl_seconds = static_cast<int32_t>(options.ttl_seconds);
  config.enable_v2_encode = options.enable_v2_encode;
  config.enable_v2_read_fallback = options.enable_v2_read_fallback;

  l2::RocksDBCache cache(config);
  if (!cache.Initialize()) {
    result.exit_code = kExitInitFailed;
    result.stderr_text =
        "failed to initialize RocksDB cache at path: " + options.db_path + "\n";
    return result;
  }

  if (options.command == Command::kValidate) {
    const size_t ttl_corrupted =
        cache.CountCorruptedEntries(l2::RocksDBCache::DataTier::kTtl);
    const size_t persistent_corrupted =
        cache.CountCorruptedEntries(l2::RocksDBCache::DataTier::kPersistent);
    const size_t total_corrupted = ttl_corrupted + persistent_corrupted;
    const auto codec_stats = cache.GetCodecRuntimeStats();
    const auto runtime_audit_stats = cache.GetPersistedRuntimeConfigAuditStats();
    const auto capacity_stats = cache.GetPersistedCapacityGovernanceStats();
    const auto tombstone_gc_stats = cache.GetPersistedTombstoneGcStats();

    std::ostringstream out;
    out << "storage_admin_validate_summary"
        << " db_path=" << options.db_path
        << " ttl_corrupted=" << ttl_corrupted
        << " persistent_corrupted=" << persistent_corrupted
        << " total_corrupted=" << total_corrupted
        << " enable_v2_encode=" << (codec_stats.enable_v2_encode ? "true" : "false")
        << " enable_v2_read_fallback="
        << (codec_stats.enable_v2_read_fallback ? "true" : "false")
        << " enable_data_encryption="
        << (codec_stats.enable_data_encryption ? "true" : "false")
        << " v2_decode_reads=" << codec_stats.v2_decode_reads
        << " v1_fallback_reads=" << codec_stats.v1_fallback_reads
        << " v1_reject_reads=" << codec_stats.v1_reject_reads
        << " decode_errors=" << codec_stats.decode_errors
        << " encrypted_decode_reads=" << codec_stats.encrypted_decode_reads
        << " decrypt_failures=" << codec_stats.decrypt_failures
        << " runtime_config_audit_key_enable_data_encryption_total="
        << runtime_audit_stats.runtime_config_audit_key_enable_data_encryption_total
        << " runtime_config_audit_key_encryption_key_env_total="
        << runtime_audit_stats.runtime_config_audit_key_encryption_key_env_total
        << " l2_soft_limit_write_total="
        << capacity_stats.l2_soft_limit_write_total
        << " l2_hard_limit_reject_total="
        << capacity_stats.l2_hard_limit_reject_total
        << " l2_hard_limit_bypass_total="
        << capacity_stats.l2_hard_limit_bypass_total
        << " tombstone_gc_pending=" << cache.TombstoneGcDepth()
        << " tombstone_gc_reclaimed_total="
        << tombstone_gc_stats.tombstone_gc_reclaimed_total
        << " tombstone_gc_failed_total="
        << tombstone_gc_stats.tombstone_gc_failed_total
        << "\n";
    result.stdout_text = out.str();

    if (total_corrupted > 0) {
      result.exit_code = kExitCorruptionDetected;
      result.stderr_text = "corrupted entries detected\n";
      return result;
    }
    result.exit_code = 0;
    return result;
  }

  if (options.command == Command::kDeadLetterReplay) {
    size_t scanned = 0;
    size_t matched = 0;
    size_t replayed = 0;
    size_t acked = 0;
    size_t failed = 0;
    std::ostringstream verbose_out;
    std::ostringstream error_out;

    cache.ReplayDeadLetter(
        options.limit,
        [&cache, &options, &scanned, &matched, &replayed, &acked, &failed,
         &verbose_out,
         &error_out](const l2::RocksDBCache::DeadLetterEntry& entry) {
          ++scanned;
          if (!MatchesDeadLetterFilter(entry, options)) {
            return true;
          }

          ++matched;
          if (options.verbose) {
            verbose_out << "match dead_letter_id=" << entry.dead_letter_id
                        << " key=" << entry.key
                        << " attempts=" << entry.attempts
                        << " recorded_at_ms=" << entry.recorded_at_ms << "\n";
          }

          if (options.dry_run) {
            return true;
          }

          if (entry.durable_outbox_id == 0) {
            uint64_t outbox_id = 0;
            if (!cache.AppendOutbox(entry.key, entry.data, entry.priority,
                                    &outbox_id)) {
              ++failed;
              error_out << "replay failed dead_letter_id=" << entry.dead_letter_id
                        << " key=" << entry.key
                        << " reason=append_outbox_failed\n";
              return true;
            }
          }
          ++replayed;

          if (options.keep_dead_letter) {
            return true;
          }

          if (!cache.AckDeadLetter(entry.dead_letter_id)) {
            ++failed;
            error_out << "ack failed dead_letter_id=" << entry.dead_letter_id
                      << " key=" << entry.key << "\n";
            return true;
          }
          ++acked;
          return true;
        });

    std::ostringstream out;
    if (options.verbose) {
      out << verbose_out.str();
    }
    out << "storage_admin_dead_letter_replay_summary"
        << " scanned=" << scanned
        << " matched=" << matched
        << " replayed=" << replayed
        << " acked=" << acked
        << " failed=" << failed
        << " dry_run=" << (options.dry_run ? "true" : "false")
        << " keep_dead_letter=" << (options.keep_dead_letter ? "true" : "false")
        << "\n";
    result.stdout_text = out.str();
    result.stderr_text = error_out.str();
    result.exit_code = failed > 0 ? kExitReplayFailed : 0;
    return result;
  }

  uint64_t pending_compaction_bytes = 0;
  uint64_t running_compactions = 0;
  uint64_t running_flushes = 0;
  uint64_t block_cache_usage = 0;
  uint64_t immutable_memtables = 0;
  uint64_t write_stopped = 0;
  (void)cache.GetUInt64Property("rocksdb.estimate-pending-compaction-bytes",
                                &pending_compaction_bytes);
  (void)cache.GetUInt64Property("rocksdb.num-running-compactions",
                                &running_compactions);
  (void)cache.GetUInt64Property("rocksdb.num-running-flushes",
                                &running_flushes);
  (void)cache.GetUInt64Property("rocksdb.block-cache-usage",
                                &block_cache_usage);
  (void)cache.GetUInt64Property("rocksdb.num-immutable-mem-table",
                                &immutable_memtables);
  (void)cache.GetUInt64Property("rocksdb.is-write-stopped", &write_stopped);
  const auto codec_stats = cache.GetCodecRuntimeStats();
  const auto runtime_audit_stats = cache.GetPersistedRuntimeConfigAuditStats();
  const auto capacity_stats = cache.GetPersistedCapacityGovernanceStats();
  const auto tombstone_gc_stats = cache.GetPersistedTombstoneGcStats();

  std::ostringstream out;
  out << "storage_admin_health_summary"
      << " db_path=" << options.db_path
      << " approx_size_bytes=" << cache.GetApproximateSizeBytes()
      << " outbox_depth=" << cache.OutboxDepth()
      << " dead_letter_depth=" << cache.DeadLetterDepth()
      << " pending_compaction_bytes=" << pending_compaction_bytes
      << " running_compactions=" << running_compactions
      << " running_flushes=" << running_flushes
      << " block_cache_usage=" << block_cache_usage
      << " immutable_memtables=" << immutable_memtables
      << " write_stopped=" << write_stopped
      << " enable_v2_encode=" << (codec_stats.enable_v2_encode ? "true" : "false")
      << " enable_v2_read_fallback="
      << (codec_stats.enable_v2_read_fallback ? "true" : "false")
      << " enable_data_encryption="
      << (codec_stats.enable_data_encryption ? "true" : "false")
      << " v2_decode_reads=" << codec_stats.v2_decode_reads
      << " v1_fallback_reads=" << codec_stats.v1_fallback_reads
      << " v1_reject_reads=" << codec_stats.v1_reject_reads
      << " decode_errors=" << codec_stats.decode_errors
      << " encrypted_decode_reads=" << codec_stats.encrypted_decode_reads
      << " decrypt_failures=" << codec_stats.decrypt_failures
      << " runtime_config_audit_key_enable_data_encryption_total="
      << runtime_audit_stats.runtime_config_audit_key_enable_data_encryption_total
      << " runtime_config_audit_key_encryption_key_env_total="
      << runtime_audit_stats.runtime_config_audit_key_encryption_key_env_total
      << " l2_soft_limit_write_total="
      << capacity_stats.l2_soft_limit_write_total
      << " l2_hard_limit_reject_total="
      << capacity_stats.l2_hard_limit_reject_total
      << " l2_hard_limit_bypass_total="
      << capacity_stats.l2_hard_limit_bypass_total
      << " tombstone_gc_pending=" << cache.TombstoneGcDepth()
      << " tombstone_gc_reclaimed_total="
      << tombstone_gc_stats.tombstone_gc_reclaimed_total
      << " tombstone_gc_failed_total="
      << tombstone_gc_stats.tombstone_gc_failed_total
      << "\n";
  result.stdout_text = out.str();
  result.exit_code = 0;
  return result;
}

}  // namespace mir2::storage_engine::utils::storage_admin
