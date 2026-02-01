/**
 * @file application.h
 * @brief 服务器应用框架
 */

#ifndef MIR2_CORE_APPLICATION_H
#define MIR2_CORE_APPLICATION_H

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include <asio/io_context.hpp>
#include <asio/executor_work_guard.hpp>

#include "config/config_manager.h"
#include "core/timer.h"

namespace mir2::core {

/**
 * @brief 服务器应用框架
 *
 * 负责IO线程、主循环Tick与统一的生命周期管理。
 */
class Application {
 public:
  Application();
  ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  /**
   * @brief 初始化应用
   * @param server_config 服务器配置
   * @return 初始化是否成功
   */
  bool Initialize(const config::ServerConfig& server_config);

  /**
   * @brief 运行主循环
   * @param tick_callback 逻辑Tick回调
   */
  void Run(const std::function<void(float)>& tick_callback);

  /**
   * @brief 请求停止
   */
  void Stop();

  /**
   * @brief 关闭并回收资源
   */
  void Shutdown();

  /**
   * @brief 获取IO上下文
   */
  asio::io_context& GetIoContext();

  /**
   * @brief 查询运行状态
   */
  bool IsRunning() const { return running_.load(); }

 private:
  std::unique_ptr<asio::io_context> io_context_;
  std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_;
  std::vector<std::thread> io_threads_;
  std::unique_ptr<TickTimer> tick_timer_;
  std::atomic<bool> running_{false};
  int io_thread_count_ = 0;
};

}  // namespace mir2::core

#endif  // MIR2_CORE_APPLICATION_H
