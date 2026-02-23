#include <cstdint>

#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "storage_engine/l2/rocksdb_cache.h"

namespace {

struct Options {
  std::string db_path;
  std::string key_prefix;
  uint64_t start_ms = 0;
  uint64_t end_ms = std::numeric_limits<uint64_t>::max();
  size_t limit = 0;
  bool dry_run = false;
  bool keep_dead_letter = false;
  bool verbose = false;
};

void PrintUsage(const char* argv0) {
  std::cout
      << "Usage: " << argv0 << " --db-path <path> [options]\n"
      << "\n"
      << "Options:\n"
      << "  --db-path <path>        RocksDB path (required)\n"
      << "  --prefix <key_prefix>   Only replay keys with this prefix\n"
      << "  --start-ms <ts_ms>      Include dead letters recorded at or after ts\n"
      << "  --end-ms <ts_ms>        Include dead letters recorded at or before ts\n"
      << "  --limit <n>             Max dead-letter rows scanned (0 = all)\n"
      << "  --dry-run               Match and print without replay\n"
      << "  --keep-dead-letter      Keep dead-letter rows after replay\n"
      << "  --verbose               Print matched rows\n"
      << "  -h, --help              Show help\n";
}

std::optional<uint64_t> ParseUint64(const std::string& value) {
  try {
    return std::stoull(value);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<size_t> ParseSizeT(const std::string& value) {
  try {
    return static_cast<size_t>(std::stoull(value));
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool ParseArgs(int argc, char** argv, Options* options, std::string* error) {
  if (options == nullptr || error == nullptr) {
    return false;
  }

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      PrintUsage(argv[0]);
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
    if (arg == "--prefix") {
      auto value = require_value("--prefix");
      if (!value.has_value()) {
        return false;
      }
      options->key_prefix = *value;
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
      auto parsed = ParseSizeT(*value);
      if (!parsed.has_value()) {
        *error = "invalid --limit";
        return false;
      }
      options->limit = *parsed;
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

  if (options->db_path.empty()) {
    *error = "--db-path is required";
    return false;
  }
  if (options->start_ms > options->end_ms) {
    *error = "--start-ms must be <= --end-ms";
    return false;
  }

  return true;
}

bool MatchesFilter(const mir2::storage_engine::l2::RocksDBCache::DeadLetterEntry& entry,
                   const Options& options) {
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

int main(int argc, char** argv) {
  Options options;
  std::string error;
  if (!ParseArgs(argc, argv, &options, &error)) {
    if (!error.empty()) {
      std::cerr << "Error: " << error << "\n\n";
      PrintUsage(argv[0]);
      return 1;
    }
    return 0;
  }

  mir2::storage_engine::l2::RocksDBCache::Config config;
  config.db_path = options.db_path;
  config.ttl_seconds = 3600;

  mir2::storage_engine::l2::RocksDBCache cache(config);
  if (!cache.Initialize()) {
    std::cerr << "Error: failed to initialize RocksDB cache at " << options.db_path
              << "\n";
    return 2;
  }

  size_t scanned = 0;
  size_t matched = 0;
  size_t replayed = 0;
  size_t acked = 0;
  size_t failed = 0;

  cache.ReplayDeadLetter(
      options.limit,
      [&cache, &options, &scanned, &matched, &replayed, &acked,
       &failed](const mir2::storage_engine::l2::RocksDBCache::DeadLetterEntry&
                    entry) {
        ++scanned;
        if (!MatchesFilter(entry, options)) {
          return true;
        }

        ++matched;
        if (options.verbose) {
          std::cout << "match dead_letter_id=" << entry.dead_letter_id
                    << " key=" << entry.key
                    << " attempts=" << entry.attempts
                    << " recorded_at_ms=" << entry.recorded_at_ms
                    << "\n";
        }

        if (options.dry_run) {
          return true;
        }

        if (entry.durable_outbox_id == 0) {
          uint64_t outbox_id = 0;
          if (!cache.AppendOutbox(entry.key, entry.data, entry.priority,
                                  &outbox_id)) {
            ++failed;
            std::cerr << "replay failed dead_letter_id=" << entry.dead_letter_id
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
          std::cerr << "ack failed dead_letter_id=" << entry.dead_letter_id
                    << " key=" << entry.key << "\n";
          return true;
        }
        ++acked;
        return true;
      });

  std::cout << "dead_letter_replay_summary"
            << " scanned=" << scanned
            << " matched=" << matched
            << " replayed=" << replayed
            << " acked=" << acked
            << " failed=" << failed
            << " dry_run=" << (options.dry_run ? "true" : "false")
            << " keep_dead_letter="
            << (options.keep_dead_letter ? "true" : "false") << "\n";

  if (failed > 0) {
    return 3;
  }
  return 0;
}
