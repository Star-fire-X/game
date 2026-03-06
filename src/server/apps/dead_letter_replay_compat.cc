#include "apps/dead_letter_replay_compat.h"

#include <cstdint>

#include <exception>
#include <limits>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "storage_engine/storage_engine.h"
#include "storage_engine/utils/storage_admin_tool.h"

namespace mir2::apps {
namespace {

namespace storage_admin = ::mir2::storage_engine::utils::storage_admin;

using storage_admin::CommandOptions;
using storage_admin::ParseCommandLine;

struct LegacyOptions {
  std::string db_path;
  std::string config_path = "config/logic.yaml";
  std::string key_prefix;
  uint64_t start_ms = 0;
  uint64_t end_ms = std::numeric_limits<uint64_t>::max();
  std::optional<uint32_t> ttl_seconds;
  size_t limit = 0;
  bool dry_run = false;
  bool keep_dead_letter = false;
  bool verbose = false;
};

enum class ParseStatus {
  kOk,
  kHelp,
  kError,
};

std::string BuildUsageText(const char* argv0) {
  const std::string program =
      (argv0 != nullptr && std::string(argv0).size() > 0)
          ? std::string(argv0)
          : "mir2_dead_letter_replay";
  std::ostringstream out;
  out << "Usage: " << program << " --db-path <path> [options]\n"
      << "\n"
      << "Options:\n"
      << "  --db-path <path>        RocksDB path (required)\n"
      << "  --config <path>         Logic YAML config path (default: config/logic.yaml)\n"
      << "  --ttl-seconds <n>       Override L2 TTL seconds (fallback: config storage_engine.l2_ttl_seconds)\n"
      << "  --prefix <key_prefix>   Only replay keys with this prefix\n"
      << "  --start-ms <ts_ms>      Include dead letters recorded at or after ts\n"
      << "  --end-ms <ts_ms>        Include dead letters recorded at or before ts\n"
      << "  --limit <n>             Max dead-letter rows scanned (0 = all)\n"
      << "  --dry-run               Match and print without replay\n"
      << "  --keep-dead-letter      Keep dead-letter rows after replay\n"
      << "  --verbose               Print matched rows\n"
      << "  -h, --help              Show help\n";
  return out.str();
}

std::optional<uint64_t> ParseUint64(const std::string& value) {
  try {
    return std::stoull(value);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

ParseStatus ParseLegacyArgs(int argc,
                            char** argv,
                            LegacyOptions* options,
                            std::string* error) {
  if (options == nullptr || error == nullptr) {
    return ParseStatus::kError;
  }
  *error = "";

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      return ParseStatus::kHelp;
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
        return ParseStatus::kError;
      }
      options->db_path = *value;
      continue;
    }
    if (arg == "--config") {
      auto value = require_value("--config");
      if (!value.has_value()) {
        return ParseStatus::kError;
      }
      options->config_path = *value;
      continue;
    }
    if (arg == "--ttl-seconds") {
      auto value = require_value("--ttl-seconds");
      if (!value.has_value()) {
        return ParseStatus::kError;
      }
      auto parsed = ParseUint64(*value);
      if (!parsed.has_value() ||
          *parsed > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
        *error = "invalid --ttl-seconds";
        return ParseStatus::kError;
      }
      options->ttl_seconds = static_cast<uint32_t>(*parsed);
      continue;
    }
    if (arg == "--prefix") {
      auto value = require_value("--prefix");
      if (!value.has_value()) {
        return ParseStatus::kError;
      }
      options->key_prefix = *value;
      continue;
    }
    if (arg == "--start-ms") {
      auto value = require_value("--start-ms");
      if (!value.has_value()) {
        return ParseStatus::kError;
      }
      auto parsed = ParseUint64(*value);
      if (!parsed.has_value()) {
        *error = "invalid --start-ms";
        return ParseStatus::kError;
      }
      options->start_ms = *parsed;
      continue;
    }
    if (arg == "--end-ms") {
      auto value = require_value("--end-ms");
      if (!value.has_value()) {
        return ParseStatus::kError;
      }
      auto parsed = ParseUint64(*value);
      if (!parsed.has_value()) {
        *error = "invalid --end-ms";
        return ParseStatus::kError;
      }
      options->end_ms = *parsed;
      continue;
    }
    if (arg == "--limit") {
      auto value = require_value("--limit");
      if (!value.has_value()) {
        return ParseStatus::kError;
      }
      auto parsed = ParseUint64(*value);
      if (!parsed.has_value() ||
          *parsed > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        *error = "invalid --limit";
        return ParseStatus::kError;
      }
      options->limit = static_cast<size_t>(*parsed);
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
    return ParseStatus::kError;
  }

  if (options->db_path.empty()) {
    *error = "--db-path is required";
    return ParseStatus::kError;
  }
  if (options->start_ms > options->end_ms) {
    *error = "--start-ms must be <= --end-ms";
    return ParseStatus::kError;
  }

  return ParseStatus::kOk;
}

std::optional<uint32_t> ResolveTtlSeconds(const LegacyOptions& options,
                                          std::string* error) {
  if (error == nullptr) {
    return std::nullopt;
  }

  if (options.ttl_seconds.has_value()) {
    return options.ttl_seconds;
  }

  try {
    const YAML::Node root = YAML::LoadFile(options.config_path);
    const YAML::Node storage_engine = root["storage_engine"];
    if (storage_engine && storage_engine["l2_ttl_seconds"]) {
      const uint64_t parsed = storage_engine["l2_ttl_seconds"].as<uint64_t>();
      if (parsed > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
        *error = "storage_engine.l2_ttl_seconds exceeds int32 range";
        return std::nullopt;
      }
      return static_cast<uint32_t>(parsed);
    }
  } catch (const YAML::Exception& ex) {
    *error = std::string("failed to load config: ") + ex.what();
    return std::nullopt;
  }

  return storage_engine::StorageEngine::Config{}.l2_ttl_seconds;
}

std::vector<std::string> BuildForwardArgs(const LegacyOptions& options,
                                          uint32_t ttl_seconds) {
  std::vector<std::string> args;
  args.emplace_back("mir2_storage_admin");
  args.emplace_back("dead-letter-replay");
  args.emplace_back("--db-path");
  args.push_back(options.db_path);
  args.emplace_back("--ttl-seconds");
  args.push_back(std::to_string(ttl_seconds));
  if (!options.key_prefix.empty()) {
    args.emplace_back("--prefix");
    args.push_back(options.key_prefix);
  }
  if (options.start_ms > 0) {
    args.emplace_back("--start-ms");
    args.push_back(std::to_string(options.start_ms));
  }
  if (options.end_ms < std::numeric_limits<uint64_t>::max()) {
    args.emplace_back("--end-ms");
    args.push_back(std::to_string(options.end_ms));
  }
  if (options.limit > 0) {
    args.emplace_back("--limit");
    args.push_back(std::to_string(options.limit));
  }
  if (options.dry_run) {
    args.emplace_back("--dry-run");
  }
  if (options.keep_dead_letter) {
    args.emplace_back("--keep-dead-letter");
  }
  if (options.verbose) {
    args.emplace_back("--verbose");
  }
  return args;
}

}  // namespace

int RunDeadLetterReplayCompat(int argc,
                              char** argv,
                              std::ostream* out,
                              std::ostream* err) {
  if (out == nullptr || err == nullptr) {
    return 1;
  }

  LegacyOptions options;
  std::string parse_error;
  const ParseStatus status = ParseLegacyArgs(argc, argv, &options, &parse_error);
  if (status == ParseStatus::kHelp) {
    *out << BuildUsageText(argc > 0 ? argv[0] : "mir2_dead_letter_replay");
    return 0;
  }
  if (status == ParseStatus::kError) {
    *err << "Error: " << parse_error << "\n\n"
         << BuildUsageText(argc > 0 ? argv[0] : "mir2_dead_letter_replay");
    return 1;
  }

  std::string ttl_error;
  const auto ttl_seconds = ResolveTtlSeconds(options, &ttl_error);
  if (!ttl_seconds.has_value()) {
    *err << "Error: " << ttl_error << "\n";
    return 1;
  }

  auto forward_args = BuildForwardArgs(options, *ttl_seconds);
  std::vector<char*> forward_argv;
  forward_argv.reserve(forward_args.size());
  for (auto& arg : forward_args) {
    forward_argv.push_back(const_cast<char*>(arg.c_str()));
  }

  CommandOptions admin_options;
  std::string admin_parse_error;
  if (!ParseCommandLine(static_cast<int>(forward_argv.size()), forward_argv.data(),
                        &admin_options, &admin_parse_error)) {
    *err << "Error: failed to forward to mir2_storage_admin: "
         << admin_parse_error << "\n";
    return 1;
  }

  const auto result = storage_admin::Execute(admin_options);
  if (!result.stdout_text.empty()) {
    *out << result.stdout_text;
  }
  if (!result.stderr_text.empty()) {
    *err << result.stderr_text;
  }
  return result.exit_code;
}

}  // namespace mir2::apps
