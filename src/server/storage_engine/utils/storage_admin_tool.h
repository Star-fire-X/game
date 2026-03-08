#ifndef MIR2_STORAGE_ENGINE_UTILS_STORAGE_ADMIN_TOOL_H_
#define MIR2_STORAGE_ENGINE_UTILS_STORAGE_ADMIN_TOOL_H_

#include <cstddef>
#include <cstdint>

#include <string>

namespace mir2::storage_engine::utils::storage_admin {

enum class Command {
  kValidate,
  kHealth,
  kCheckpointCreate,
  kCheckpointRestore,
  kDeadLetterReplay,
  kDecodeCharacterSnapshot,
};

struct CommandOptions {
  Command command = Command::kValidate;
  std::string db_path;
  std::string output_path;
  std::string checkpoint_path;
  std::string restore_db_path;
  std::string key_prefix;
  std::string snapshot_hex;
  uint64_t start_ms = 0;
  uint64_t end_ms = UINT64_MAX;
  size_t limit = 0;
  uint32_t ttl_seconds = 3600;
  int expected_x = 0;
  int expected_y = 0;
  bool enable_v2_encode = false;
  bool enable_v2_read_fallback = true;
  bool dry_run = false;
  bool keep_dead_letter = false;
  bool verbose = false;
  bool overwrite = false;
};

struct CommandResult {
  int exit_code = 0;
  std::string stdout_text;
  std::string stderr_text;
};

std::string BuildUsage(const char* argv0);

// Returns false for usage/help scenarios or parse failures.
// When false with non-empty |error|, the caller should print an error.
bool ParseCommandLine(int argc,
                      char** argv,
                      CommandOptions* options,
                      std::string* error);

CommandResult Execute(const CommandOptions& options);

}  // namespace mir2::storage_engine::utils::storage_admin

#endif  // MIR2_STORAGE_ENGINE_UTILS_STORAGE_ADMIN_TOOL_H_
