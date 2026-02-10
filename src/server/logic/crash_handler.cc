#include "logic/crash_handler.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

#include "log/logger.h"

#if defined(__linux__)
#include "client/linux/handler/exception_handler.h"
#include "client/linux/handler/minidump_descriptor.h"
#endif

namespace mir2::logic {

namespace {

constexpr const char* kDumpDir = "/var/crash/mir2/logic";
constexpr size_t kMaxDumpFiles = 10;
constexpr const char* kDumpDirEnv = "MIR2_LOGIC_DUMP_DIR";
constexpr const char* kMaxDumpFilesEnv = "MIR2_LOGIC_MAX_DUMPS";

struct CrashHandlerConfig {
  std::filesystem::path dump_dir;
  size_t max_dumps = kMaxDumpFiles;
};

CrashHandlerConfig g_config;
std::mutex g_mutex;
std::atomic<bool> g_installed{false};

std::filesystem::path ResolveDumpDirFromEnv() {
  const char* env = std::getenv(kDumpDirEnv);
  if (env == nullptr || *env == '\0') {
    return kDumpDir;
  }
  return std::filesystem::path(env);
}

size_t ResolveMaxDumpFilesFromEnv() {
  const char* env = std::getenv(kMaxDumpFilesEnv);
  if (env == nullptr || *env == '\0') {
    return kMaxDumpFiles;
  }

  errno = 0;
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(env, &end, 10);
  if (errno != 0 || end == env || (end && *end != '\0') || parsed == 0) {
    SYSLOG_WARN("CrashHandler: invalid {}='{}', using default={}",
                kMaxDumpFilesEnv, env, kMaxDumpFiles);
    return kMaxDumpFiles;
  }

  const auto max_size_t =
      static_cast<unsigned long long>(std::numeric_limits<size_t>::max());
  return static_cast<size_t>(std::min(parsed, max_size_t));
}

bool EnsureDumpDirectory(std::filesystem::path* dump_dir) {
  if (dump_dir == nullptr || dump_dir->empty()) {
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(*dump_dir, ec);
  std::error_code exists_ec;
  const bool exists = std::filesystem::exists(*dump_dir, exists_ec);
  if (!ec && exists) {
    return true;
  }

  const auto failed_dir = *dump_dir;
  std::error_code tmp_ec;
  const auto tmp_root = std::filesystem::temp_directory_path(tmp_ec);
  if (!tmp_ec && !tmp_root.empty()) {
    std::filesystem::path fallback_dir = tmp_root / "mir2" / "logic";
    std::error_code fallback_create_ec;
    std::filesystem::create_directories(fallback_dir, fallback_create_ec);
    std::error_code fallback_exists_ec;
    const bool fallback_exists =
        std::filesystem::exists(fallback_dir, fallback_exists_ec);
    if (!fallback_create_ec && fallback_exists) {
      SYSLOG_WARN("CrashHandler: dump dir '{}' unavailable ({}), fallback to '{}'",
                  failed_dir.string(),
                  ec.message(),
                  fallback_dir.string());
      *dump_dir = std::move(fallback_dir);
      return true;
    }
  }

  SYSLOG_ERROR("CrashHandler: create dump dir failed: '{}' ({})",
               failed_dir.string(),
               ec.message());
  return false;
}

#if defined(__linux__)
std::unique_ptr<google_breakpad::ExceptionHandler> g_handler;

void PruneOldDumps(const std::filesystem::path& dump_dir,
                   size_t max_dumps) noexcept {
  if (max_dumps == 0) {
    return;
  }

  std::error_code ec;
  std::filesystem::path target_dir = dump_dir;
  if (!std::filesystem::is_directory(target_dir, ec)) {
    target_dir = target_dir.parent_path();
  }
  if (target_dir.empty()) {
    return;
  }
  if (!std::filesystem::exists(target_dir, ec)) {
    return;
  }

  std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> files;
  for (const auto& entry : std::filesystem::directory_iterator(target_dir, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file(ec)) {
      continue;
    }
    const auto& path = entry.path();
    if (path.extension() != ".dmp") {
      continue;
    }
    const auto timestamp = entry.last_write_time(ec);
    files.emplace_back(path, timestamp);
  }

  if (files.size() <= max_dumps) {
    return;
  }

  std::sort(files.begin(), files.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; });

  const size_t remove_count = files.size() - max_dumps;
  for (size_t i = 0; i < remove_count; ++i) {
    std::filesystem::remove(files[i].first, ec);
  }
}

bool DumpCallback(const google_breakpad::MinidumpDescriptor& descriptor,
                  void* /*context*/,
                  bool succeeded) noexcept {
  log::Logger::Instance().Flush();
  // No StorageEngine flush here — calling mutexes/CV from a signal handler
  // risks deadlock. RocksDB WAL is the crash-safe commit point;
  // PerformStartupRecovery() replays uncommitted entries on next startup.
  PruneOldDumps(descriptor.path(), g_config.max_dumps);
  return succeeded;
}
#endif

}  // namespace

bool CrashHandler::Initialize() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_installed.load(std::memory_order_acquire)) {
    return true;
  }

#if defined(__linux__)
  g_config.dump_dir = ResolveDumpDirFromEnv();
  g_config.max_dumps = ResolveMaxDumpFilesFromEnv();

  if (!EnsureDumpDirectory(&g_config.dump_dir)) {
    return false;
  }

  google_breakpad::MinidumpDescriptor descriptor(g_config.dump_dir.string());
  g_handler = std::make_unique<google_breakpad::ExceptionHandler>(
      descriptor, nullptr, DumpCallback, &g_config, true, -1);
  if (!g_handler) {
    SYSLOG_ERROR("CrashHandler: Breakpad handler init failed");
    return false;
  }

  g_installed.store(true, std::memory_order_release);
  SYSLOG_INFO("CrashHandler initialized (dump_dir={}, max_dumps={})",
              g_config.dump_dir.string(), g_config.max_dumps);
  return true;
#else
  SYSLOG_WARN("CrashHandler: Breakpad not supported on this platform");
  return false;
#endif
}

void CrashHandler::Shutdown() {
  std::lock_guard<std::mutex> lock(g_mutex);
#if defined(__linux__)
  g_handler.reset();
#endif
  g_installed.store(false, std::memory_order_release);
}

}  // namespace mir2::logic
