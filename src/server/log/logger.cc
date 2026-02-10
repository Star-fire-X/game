#include "log/logger.h"

#include <array>
#include <filesystem>
#include <mutex>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace mir2::log {

namespace {

constexpr const char* kLogPattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v";
constexpr std::array<LogCategory, 9> kAllCategories = {
    LogCategory::kSystem,
    LogCategory::kNetwork,
    LogCategory::kGame,
    LogCategory::kCombat,
    LogCategory::kTrade,
    LogCategory::kSecurity,
    LogCategory::kDatabase,
    LogCategory::kGateway,
    LogCategory::kWorld,
};

const char* CategoryName(LogCategory category) {
  switch (category) {
    case LogCategory::kSystem:
      return "system";
    case LogCategory::kNetwork:
      return "network";
    case LogCategory::kGame:
      return "game";
    case LogCategory::kCombat:
      return "combat";
    case LogCategory::kTrade:
      return "trade";
    case LogCategory::kSecurity:
      return "security";
    case LogCategory::kDatabase:
      return "database";
    case LogCategory::kGateway:
      return "gateway";
    case LogCategory::kWorld:
      return "world";
    default:
      return "system";
  }
}

}  // namespace

bool Logger::Initialize(const std::string& log_path, const std::string& level,
                        int max_size_mb, int max_files) {
  try {
    std::lock_guard<std::mutex> lock(mutex_);

    std::filesystem::create_directories(log_path);

    spdlog::level::level_enum log_level = spdlog::level::from_str(level);
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(log_level);

    const size_t max_size = static_cast<size_t>(max_size_mb) * 1024 * 1024;
    const size_t max_file_count = static_cast<size_t>(max_files);

    loggers_.clear();
    for (const auto category : kAllCategories) {
      const std::string logger_name = CategoryName(category);
      if (spdlog::get(logger_name)) {
        spdlog::drop(logger_name);
      }

      const std::string file_name = log_path + "/" + logger_name + ".log";
      auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          file_name, max_size, max_file_count);
      file_sink->set_level(log_level);

      std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
      auto logger = std::make_shared<spdlog::logger>(
          logger_name, sinks.begin(), sinks.end());
      logger->set_level(log_level);
      logger->set_pattern(kLogPattern);

      spdlog::register_logger(logger);
      loggers_[category] = logger;
    }

    return true;
  } catch (...) {
    return false;
  }
}

void Logger::Shutdown() {
  std::vector<std::shared_ptr<spdlog::logger>> loggers_to_flush;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    loggers_to_flush.reserve(loggers_.size());
    for (const auto& [_, logger] : loggers_) {
      if (logger) {
        loggers_to_flush.push_back(logger);
      }
    }
    loggers_.clear();
  }

  for (const auto& logger : loggers_to_flush) {
    logger->flush();
  }

  for (const auto category : kAllCategories) {
    const std::string logger_name = CategoryName(category);
    if (spdlog::get(logger_name)) {
      spdlog::drop(logger_name);
    }
  }
  if (spdlog::get("fallback")) {
    spdlog::drop("fallback");
  }
}

std::shared_ptr<spdlog::logger> Logger::GetLogger(LogCategory category) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = loggers_.find(category);
    if (it != loggers_.end()) {
      return it->second;
    }
  }

  static std::mutex fallback_mutex;
  static std::shared_ptr<spdlog::logger> local_fallback;

  std::lock_guard<std::mutex> lock(fallback_mutex);

  if (auto fallback = spdlog::default_logger()) {
    return fallback;
  }
  if (auto fallback = spdlog::get("fallback")) {
    return fallback;
  }
  if (local_fallback) {
    return local_fallback;
  }

  try {
    local_fallback = spdlog::stdout_color_mt("fallback");
  } catch (...) {
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    local_fallback = std::make_shared<spdlog::logger>("fallback_unregistered", sink);
  }

  local_fallback->set_pattern(kLogPattern);
  return local_fallback;
}

void Logger::Flush() {
  std::vector<std::shared_ptr<spdlog::logger>> loggers_to_flush;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    loggers_to_flush.reserve(loggers_.size());
    for (const auto& [_, logger] : loggers_) {
      if (logger) {
        loggers_to_flush.push_back(logger);
      }
    }
  }

  for (const auto& logger : loggers_to_flush) {
    if (logger) {
      logger->flush();
    }
  }
}

}  // namespace mir2::log
