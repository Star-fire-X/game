#include <gtest/gtest.h>

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "config/config_manager.h"
#include "gateway/gateway_server.h"
#include "network/tcp_connection.h"
#include "network/tcp_session.h"

namespace mir2::gateway {

namespace {

std::string WriteTempConfig(const std::string& contents) {
  const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("gateway_heartbeat_test_" + std::to_string(timestamp) + ".yaml");
  std::ofstream output(path, std::ios::binary);
  output << contents;
  output.close();
  return path.string();
}

std::shared_ptr<network::TcpSession> CreateSession(asio::io_context& io_context) {
  asio::ip::tcp::acceptor acceptor(
      io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
  const uint16_t port = acceptor.local_endpoint().port();

  asio::ip::tcp::socket client_socket(io_context);
  client_socket.connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), port));

  asio::ip::tcp::socket server_socket = acceptor.accept();

  auto connection = std::make_shared<network::TcpConnection>(
      std::make_unique<network::AsioSocketAdapter>(std::move(server_socket)),
      1);
  auto session = std::make_shared<network::TcpSession>(connection);
  std::weak_ptr<network::TcpSession> weak_session = session;
  connection->SetReadHandler([weak_session](const uint8_t* data, size_t size) {
    if (auto locked = weak_session.lock()) {
      locked->HandleBytes(data, size);
    }
  });
  connection->SetDisconnectHandler([weak_session](uint64_t id) {
    if (auto locked = weak_session.lock()) {
      locked->HandleDisconnect(id);
    }
  });
  session->Start();
  return session;
}

class TestGatewayServer : public GatewayServer {
 public:
  void CheckHeartbeatTimeouts(
      const std::vector<std::shared_ptr<network::TcpSession>>& sessions,
      int64_t now_ms) override {
    tick_checked = true;
    GatewayServer::CheckHeartbeatTimeouts(sessions, now_ms);
  }

  void TickForTest(float delta_time) { Tick(delta_time); }

  bool tick_checked = false;
};

}  // namespace

TEST(GatewayHeartbeatConfigTest, DefaultTimeoutIs30000) {
  config::ServerConfig config;
  EXPECT_EQ(config.heartbeat_timeout_ms, 30000);
}

TEST(GatewayHeartbeatConfigTest, LoadsTimeoutFromYaml) {
  const std::string path = WriteTempConfig("server:\n  heartbeat_timeout_ms: 45000\n");
  ASSERT_TRUE(config::ConfigManager::Instance().Load(path));
  EXPECT_EQ(config::ConfigManager::Instance().GetServerConfig().heartbeat_timeout_ms, 45000);
  std::filesystem::remove(path);
}

TEST(GatewayHeartbeatTest, SessionsWithinTimeoutNotClosed) {
  const int timeout_ms = 1000;
  const std::string path = WriteTempConfig("server:\n  heartbeat_timeout_ms: 1000\n");
  ASSERT_TRUE(config::ConfigManager::Instance().Load(path));
  std::filesystem::remove(path);

  asio::io_context io_context;
  auto session = CreateSession(io_context);
  TestGatewayServer server;

  const int64_t last_heartbeat_ms = session->GetLastHeartbeatMs();
  server.CheckHeartbeatTimeouts({session}, last_heartbeat_ms + timeout_ms - 1);

  EXPECT_EQ(session->GetState(), network::TcpSession::SessionState::kActive);
}

TEST(GatewayHeartbeatTest, SessionsPastTimeoutAreClosed) {
  const int timeout_ms = 1000;
  const std::string path = WriteTempConfig("server:\n  heartbeat_timeout_ms: 1000\n");
  ASSERT_TRUE(config::ConfigManager::Instance().Load(path));
  std::filesystem::remove(path);

  asio::io_context io_context;
  auto session = CreateSession(io_context);
  TestGatewayServer server;

  const int64_t last_heartbeat_ms = session->GetLastHeartbeatMs();
  server.CheckHeartbeatTimeouts({session}, last_heartbeat_ms + timeout_ms);

  EXPECT_EQ(session->GetState(), network::TcpSession::SessionState::kClosing);
}

TEST(GatewayHeartbeatTest, TickInvokesHeartbeatCheck) {
  TestGatewayServer server;
  server.TickForTest(0.0f);
  EXPECT_TRUE(server.tick_checked);
}

}  // namespace mir2::gateway
