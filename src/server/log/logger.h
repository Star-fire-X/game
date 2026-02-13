/**
 * @file logger.h
 * @brief 日志系统
 */

#ifndef MIR2_LOG_LOGGER_H_
#define MIR2_LOG_LOGGER_H_

#include <mutex>
#include <cstdint>
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

  mutable std::mutex mutex_;
  std::unordered_map<LogCategory, std::shared_ptr<spdlog::logger>> loggers_;
};

struct TraceLogContext {
  uint64_t trace_id = 0;
  uint64_t coroutine_id = 0;
};

TraceLogContext GetCurrentTraceLogContext() noexcept;
void SetCurrentTraceLogContext(uint64_t trace_id, uint64_t coroutine_id) noexcept;

class ScopedTraceLogContext {
 public:
  ScopedTraceLogContext(uint64_t trace_id, uint64_t coroutine_id) noexcept
      : previous_(GetCurrentTraceLogContext()) {
    SetCurrentTraceLogContext(trace_id, coroutine_id);
  }

  ~ScopedTraceLogContext() { SetCurrentTraceLogContext(previous_.trace_id, previous_.coroutine_id); }

 private:
  TraceLogContext previous_{};
};

inline uint64_t CurrentTraceId() noexcept {
  return GetCurrentTraceLogContext().trace_id;
}

inline uint64_t CurrentCoroutineId() noexcept {
  return GetCurrentTraceLogContext().coroutine_id;
}

#define LOG_INFO(cat, fmt, ...) \
  mir2::log::Logger::Instance().GetLogger(cat)->info( \
      "[trace_id={} coroutine_id={}] " fmt, \
      mir2::log::CurrentTraceId(), \
      mir2::log::CurrentCoroutineId(), \
      ##__VA_ARGS__)
#define LOG_DEBUG(cat, fmt, ...) \
  mir2::log::Logger::Instance().GetLogger(cat)->debug( \
      "[trace_id={} coroutine_id={}] " fmt, \
      mir2::log::CurrentTraceId(), \
      mir2::log::CurrentCoroutineId(), \
      ##__VA_ARGS__)
#define LOG_WARN(cat, fmt, ...) \
  mir2::log::Logger::Instance().GetLogger(cat)->warn( \
      "[trace_id={} coroutine_id={}] " fmt, \
      mir2::log::CurrentTraceId(), \
      mir2::log::CurrentCoroutineId(), \
      ##__VA_ARGS__)
#define LOG_ERROR(cat, fmt, ...) \
  mir2::log::Logger::Instance().GetLogger(cat)->error( \
      "[trace_id={} coroutine_id={}] " fmt, \
      mir2::log::CurrentTraceId(), \
      mir2::log::CurrentCoroutineId(), \
      ##__VA_ARGS__)

#define SYSLOG_INFO(fmt, ...) LOG_INFO(mir2::log::LogCategory::kSystem, fmt, ##__VA_ARGS__)
#define SYSLOG_DEBUG(fmt, ...) LOG_DEBUG(mir2::log::LogCategory::kSystem, fmt, ##__VA_ARGS__)
#define SYSLOG_WARN(fmt, ...) LOG_WARN(mir2::log::LogCategory::kSystem, fmt, ##__VA_ARGS__)
#define SYSLOG_ERROR(fmt, ...) LOG_ERROR(mir2::log::LogCategory::kSystem, fmt, ##__VA_ARGS__)

}  // namespace mir2::log

#endif  // MIR2_LOG_LOGGER_H_
