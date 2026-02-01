/**
 * @file logger.h
 * @brief 日志系统
 */

#ifndef MIR2_LOG_LOGGER_H
#define MIR2_LOG_LOGGER_H

#include <memory>
#include <string>
#include <unordered_map>

#include <spdlog/logger.h>

#include "core/singleton.h"

namespace mir2::log {

/**
 * @brief 日志分类
 */
enum class LogCategory {
  kSystem,
  kNetwork,
  kGame,
  kCombat,
  kTrade,
  kSecurity,
  kDatabase,
  kGateway,
  kWorld
};

/**
 * @brief 日志管理器
 *
 * 为不同模块提供分类日志输出。
 */
class Logger : public core::Singleton<Logger> {
  friend class core::Singleton<Logger>;

 public:
  /**
   * @brief 初始化日志系统
   * @param log_path 日志目录
   * @param level 日志等级字符串
   */
  bool Initialize(const std::string& log_path, const std::string& level,
                  int max_size_mb = 100, int max_files = 10);

  /**
   * @brief 关闭日志系统
   */
  void Shutdown();

  /**
   * @brief 获取指定分类的logger
   */
  std::shared_ptr<spdlog::logger> GetLogger(LogCategory category);

  /**
   * @brief 刷新日志
   */
  void Flush();

 private:
  Logger() = default;

  std::unordered_map<LogCategory, std::shared_ptr<spdlog::logger>> loggers_;
};

#define LOG_INFO(cat, fmt, ...) \
  mir2::log::Logger::Instance().GetLogger(cat)->info(fmt, ##__VA_ARGS__)
#define LOG_DEBUG(cat, fmt, ...) \
  mir2::log::Logger::Instance().GetLogger(cat)->debug(fmt, ##__VA_ARGS__)
#define LOG_WARN(cat, fmt, ...) \
  mir2::log::Logger::Instance().GetLogger(cat)->warn(fmt, ##__VA_ARGS__)
#define LOG_ERROR(cat, fmt, ...) \
  mir2::log::Logger::Instance().GetLogger(cat)->error(fmt, ##__VA_ARGS__)

#define SYSLOG_INFO(fmt, ...) LOG_INFO(mir2::log::LogCategory::kSystem, fmt, ##__VA_ARGS__)
#define SYSLOG_DEBUG(fmt, ...) LOG_DEBUG(mir2::log::LogCategory::kSystem, fmt, ##__VA_ARGS__)
#define SYSLOG_WARN(fmt, ...) LOG_WARN(mir2::log::LogCategory::kSystem, fmt, ##__VA_ARGS__)
#define SYSLOG_ERROR(fmt, ...) LOG_ERROR(mir2::log::LogCategory::kSystem, fmt, ##__VA_ARGS__)

}  // namespace mir2::log

#endif  // MIR2_LOG_LOGGER_H
