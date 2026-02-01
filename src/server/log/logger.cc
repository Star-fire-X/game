#include "log/logger.h"

#include <filesystem>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace mir2::log {

namespace {

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
  std::filesystem::create_directories(log_path);

  spdlog::level::level_enum log_level = spdlog::level::from_str(level);
  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(log_level);

  const size_t max_size = static_cast<size_t>(max_size_mb) * 1024 * 1024;
  const size_t max_file_count = static_cast<size_t>(max_files);

  for (const auto category : {
           LogCategory::kSystem,
           LogCategory::kNetwork,
           LogCategory::kGame,
           LogCategory::kCombat,
           LogCategory::kTrade,
           LogCategory::kSecurity,
           LogCategory::kDatabase,
           LogCategory::kGateway,
           LogCategory::kWorld}) {
    const std::string file_name = log_path + "/" + CategoryName(category) + ".log";
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        file_name, max_size, max_file_count);
    file_sink->set_level(log_level);

    std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
    auto logger = std::make_shared<spdlog::logger>(CategoryName(category), sinks.begin(), sinks.end());
    logger->set_level(log_level);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v");

    spdlog::register_logger(logger);
    loggers_[category] = logger;
  }

  return true;
}

void Logger::Shutdown() {
  loggers_.clear();
  spdlog::shutdown();
}

std::shared_ptr<spdlog::logger> Logger::GetLogger(LogCategory category) {
  auto it = loggers_.find(category);
  if (it != loggers_.end()) {
    return it->second;
  }
  auto fallback = spdlog::default_logger();
  if (!fallback) {
    fallback = spdlog::stdout_color_mt("fallback");
  }
  return fallback;
}

void Logger::Flush() {
  for (auto& [_, logger] : loggers_) {
    if (logger) {
      logger->flush();
    }
  }
}

}  // namespace mir2::log
