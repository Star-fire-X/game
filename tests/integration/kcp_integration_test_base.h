#ifndef MIR2_TESTS_INTEGRATION_KCP_INTEGRATION_TEST_BASE_H_
#define MIR2_TESTS_INTEGRATION_KCP_INTEGRATION_TEST_BASE_H_

#include <asio.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

#include "client/network/dual_channel_client.h"
#include "integration/mock_game_server.h"
#include "integration/test_helpers.h"

namespace mir2::test::integration {

class KcpIntegrationTestBase : public ::testing::Test {
 protected:
  // Default integration test endpoints.
  static constexpr const char* kHost = "127.0.0.1";
  static constexpr uint16_t kTcpPort = 7000;
  static constexpr uint16_t kUdpPort = 7001;
  static constexpr int kMaxConnections = 128;

  void SetUp() override {
    io_work_guard_.emplace(io_context_.get_executor());
    server_ = CreateTestServer();
    if (server_) {
      server_->EnableLoginResponse();
    }
    client_ = CreateTestClient();
    StartIoThread();
  }

  void TearDown() override {
    if (client_) {
      client_->disconnect();
    }
    if (server_) {
      server_->Stop();
    }
    StopIoThread();
    client_.reset();
    server_.reset();
  }

  virtual std::unique_ptr<MockGameServer> CreateTestServer() {
    MockGameServer::Config config;
    config.bind_ip = kHost;
    config.tcp_port = kTcpPort;
    config.udp_port = kUdpPort;
    config.max_connections = kMaxConnections;

    auto server = std::make_unique<MockGameServer>(io_context_, config);
    server->Start();
    return server;
  }

  virtual std::unique_ptr<mir2::client::DualChannelClient> CreateTestClient() {
    return std::make_unique<mir2::client::DualChannelClient>();
  }

  void PumpOnce() {
    if (server_) {
      server_->Tick();
    }
    if (client_) {
      client_->update();
    }
  }

  void PumpFor(std::chrono::milliseconds duration,
               std::chrono::milliseconds interval = std::chrono::milliseconds(5)) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
      PumpOnce();
      std::this_thread::sleep_for(interval);
    }
  }

  template <typename Predicate, typename Rep, typename Period>
  bool WaitForCondition(Predicate predicate,
                        std::chrono::duration<Rep, Period> timeout,
                        std::chrono::milliseconds poll_interval =
                            std::chrono::milliseconds(5)) {
    return mir2::test::integration::WaitForCondition(
        std::move(predicate), timeout, poll_interval, [this]() { PumpOnce(); });
  }

  asio::io_context io_context_;
  std::optional<asio::executor_work_guard<asio::io_context::executor_type>> io_work_guard_;
  std::thread io_thread_;

  std::unique_ptr<MockGameServer> server_;
  std::unique_ptr<mir2::client::DualChannelClient> client_;

 private:
  void StartIoThread() {
    if (io_thread_.joinable()) {
      return;
    }
    io_thread_ = std::thread([this]() { io_context_.run(); });
  }

  void StopIoThread() {
    io_work_guard_.reset();
    io_context_.stop();
    if (io_thread_.joinable()) {
      io_thread_.join();
    }
  }
};

}  // namespace mir2::test::integration

#endif  // MIR2_TESTS_INTEGRATION_KCP_INTEGRATION_TEST_BASE_H_
