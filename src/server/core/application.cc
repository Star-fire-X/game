#include "core/application.h"

#include <iostream>

namespace mir2::core {

Application::Application() = default;

Application::~Application() {
  Shutdown();
}

bool Application::Initialize(const config::ServerConfig& server_config) {
  if (running_.load()) {
    return false;
  }

  io_thread_count_ = server_config.io_threads;
  if (io_thread_count_ <= 0) {
    io_thread_count_ = 1;
  }

  io_context_ = std::make_unique<asio::io_context>();
  work_guard_ = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(
      asio::make_work_guard(*io_context_));

  tick_timer_ = std::make_unique<TickTimer>(server_config.tick_interval_ms);

  io_threads_.reserve(static_cast<size_t>(io_thread_count_));
  for (int i = 0; i < io_thread_count_; ++i) {
    io_threads_.emplace_back([this]() {
      if (io_context_) {
        io_context_->run();
      }
    });
  }

  return true;
}

void Application::Run(const std::function<void(float)>& tick_callback) {
  if (!tick_timer_) {
    return;
  }

  running_.store(true);

  while (running_.load()) {
    tick_timer_->BeginTick();

    if (tick_callback) {
      tick_callback(tick_timer_->GetDeltaTime());
    }

    tick_timer_->EndTick();
  }
}

void Application::Stop() {
  running_.store(false);
}

void Application::Shutdown() {
  if (!io_context_) {
    return;
  }

  running_.store(false);

  if (work_guard_) {
    work_guard_.reset();
  }

  io_context_->stop();

  for (auto& thread : io_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  io_threads_.clear();
  io_context_.reset();
  tick_timer_.reset();
}

asio::io_context& Application::GetIoContext() {
  return *io_context_;
}

}  // namespace mir2::core
