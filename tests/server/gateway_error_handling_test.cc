#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <asio/io_context.hpp>

#include "config/config_manager.h"
#include "gateway/gateway_server.h"
#include "network/tcp_connection.h"
#include "network/tcp_session.h"
#include "mocks/mock_socket.h"

namespace mir2::gateway {

namespace {

std::string WriteTempConfig(const std::string& contents, const std::string& suffix) {
  const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("gateway_error_handling_" + suffix + "_" + std::to_string(timestamp) + ".yaml");
  std::ofstream output(path, std::ios::binary);
  output << contents;
  output.close();
  return path.string();
}

struct SessionBundle {
  std::shared_ptr<network::TcpSession> session;
  network::MockSocket* socket = nullptr;
};

SessionBundle CreateSession(asio::io_context& io_context, uint64_t connection_id) {
  auto mock_socket = std::make_unique<network::MockSocket>(io_context.get_executor());
  auto* socket_ptr = mock_socket.get();
  auto connection = std::make_shared<network::TcpConnection>(std::move(mock_socket), connection_id);
  auto session = std::make_shared<network::TcpSession>(connection);
  return {session, socket_ptr};
}

}  // namespace

// Config parsing and route validation.
TEST(GatewayErrorHandlingTest, LoadConfig_InvalidYaml_ReturnsFalse) {
  const auto path = WriteTempConfig("server: [invalid", "invalid_yaml");
  EXPECT_FALSE(config::ConfigManager::Instance().Load(path));
  std::filesystem::remove(path);
}

TEST(GatewayErrorHandlingTest, LoadConfig_MissingRequiredFields_ReturnsDefaults) {
  config::ServerConfig defaults;
  const auto path = WriteTempConfig("server: {}\n", "missing_fields");
  ASSERT_TRUE(config::ConfigManager::Instance().Load(path));

  const auto& config = config::ConfigManager::Instance().GetServerConfig();
  EXPECT_EQ(config.heartbeat_timeout_ms, defaults.heartbeat_timeout_ms);
  EXPECT_EQ(config.max_connections, defaults.max_connections);
  EXPECT_EQ(config.queue_full_fallback_non_best_effort_enabled,
            defaults.queue_full_fallback_non_best_effort_enabled);
  EXPECT_EQ(config.login_ip_rate_limit_capacity, defaults.login_ip_rate_limit_capacity);
  EXPECT_EQ(config.login_ip_rate_limit_refill_rate, defaults.login_ip_rate_limit_refill_rate);
  EXPECT_EQ(config.login_username_rate_limit_capacity,
            defaults.login_username_rate_limit_capacity);
  EXPECT_EQ(config.login_username_rate_limit_refill_rate,
            defaults.login_username_rate_limit_refill_rate);
  EXPECT_EQ(config.login_username_rate_limit_refill_interval_seconds,
            defaults.login_username_rate_limit_refill_interval_seconds);
  EXPECT_EQ(config.udp_send_fault_inject_every_n, defaults.udp_send_fault_inject_every_n);
  EXPECT_EQ(config.bind_ip, defaults.bind_ip);
  EXPECT_EQ(config.chat_batch_send_enabled, defaults.chat_batch_send_enabled);

  std::filesystem::remove(path);
}

TEST(GatewayErrorHandlingTest, ServerConfig_DefaultLoginIpRateLimitValues) {
  config::ServerConfig defaults;
  EXPECT_EQ(defaults.login_ip_rate_limit_capacity, 5);
  EXPECT_EQ(defaults.login_ip_rate_limit_refill_rate, 1);
  EXPECT_EQ(defaults.login_username_rate_limit_capacity, 5);
  EXPECT_EQ(defaults.login_username_rate_limit_refill_rate, 1);
  EXPECT_EQ(defaults.login_username_rate_limit_refill_interval_seconds, 12);
  EXPECT_EQ(defaults.udp_send_fault_inject_every_n, 0);
  EXPECT_TRUE(defaults.legacy_fallback_enabled);
  EXPECT_TRUE(defaults.legacy_fallback_allow_auth_whitelist);
  EXPECT_TRUE(defaults.legacy_fallback_allow_critical_msgs);
  EXPECT_FALSE(defaults.legacy_fallback_allow_normal_msgs);
  EXPECT_TRUE(defaults.queue_full_fallback_non_best_effort_enabled);
  EXPECT_TRUE(defaults.chat_batch_send_enabled);
  EXPECT_EQ(defaults.movement_speed_violation_severity, 10);
  EXPECT_EQ(defaults.movement_teleport_violation_severity, 5);
}

TEST(GatewayErrorHandlingTest, LoadConfig_LoginIpRateLimitFieldsParsed) {
  const auto path = WriteTempConfig(
      "server:\n"
      "  login_ip_rate_limit_capacity: 12\n"
      "  login_ip_rate_limit_refill_rate: 3\n"
      "  login_username_rate_limit_capacity: 22\n"
      "  login_username_rate_limit_refill_rate: 8\n"
      "  login_username_rate_limit_refill_interval_seconds: 2\n"
      "  udp_send_fault_inject_every_n: 77\n"
      "  legacy_fallback_enabled: false\n"
      "  legacy_fallback_allow_auth_whitelist: false\n"
      "  legacy_fallback_allow_critical_msgs: false\n"
      "  legacy_fallback_allow_normal_msgs: true\n"
      "  queue_full_fallback_non_best_effort_enabled: false\n"
      "  chat_batch_send_enabled: false\n"
      "  movement_speed_violation_severity: 20\n"
      "  movement_teleport_violation_severity: 9\n",
      "login_ip_limit");
  ASSERT_TRUE(config::ConfigManager::Instance().Load(path));

  const auto& config = config::ConfigManager::Instance().GetServerConfig();
  EXPECT_EQ(config.login_ip_rate_limit_capacity, 12);
  EXPECT_EQ(config.login_ip_rate_limit_refill_rate, 3);
  EXPECT_EQ(config.login_username_rate_limit_capacity, 22);
  EXPECT_EQ(config.login_username_rate_limit_refill_rate, 8);
  EXPECT_EQ(config.login_username_rate_limit_refill_interval_seconds, 2);
  EXPECT_EQ(config.udp_send_fault_inject_every_n, 77);
  EXPECT_FALSE(config.legacy_fallback_enabled);
  EXPECT_FALSE(config.legacy_fallback_allow_auth_whitelist);
  EXPECT_FALSE(config.legacy_fallback_allow_critical_msgs);
  EXPECT_TRUE(config.legacy_fallback_allow_normal_msgs);
  EXPECT_FALSE(config.queue_full_fallback_non_best_effort_enabled);
  EXPECT_FALSE(config.chat_batch_send_enabled);
  EXPECT_EQ(config.movement_speed_violation_severity, 20);
  EXPECT_EQ(config.movement_teleport_violation_severity, 9);

  std::filesystem::remove(path);
}

TEST(GatewayErrorHandlingTest, LoadConfig_LogicServiceTransportParsed) {
  const auto path = WriteTempConfig(
      "services:\n"
      "  logic:\n"
      "    host: \"localhost\"\n"
      "    port: 9123\n"
      "    transport: \"UDS\"\n"
      "    uds_path: \"/tmp/mir2_logic_test.sock\"\n",
      "logic_service_transport");
  ASSERT_TRUE(config::ConfigManager::Instance().Load(path));

  const auto& logic = config::ConfigManager::Instance().GetServiceConfig().logic;
  EXPECT_EQ(logic.host, "localhost");
  EXPECT_EQ(logic.port, 9123);
  EXPECT_EQ(logic.transport, "uds");
  EXPECT_EQ(logic.uds_path, "/tmp/mir2_logic_test.sock");

  std::filesystem::remove(path);
}

TEST(GatewayErrorHandlingTest, LoadConfig_InvalidLogicServiceTransportFallsBackToAuto) {
  const auto path = WriteTempConfig(
      "services:\n"
      "  logic:\n"
      "    transport: \"invalid_mode\"\n"
      "    uds_path: \"/tmp/invalid.sock\"\n",
      "logic_service_invalid_transport");
  ASSERT_TRUE(config::ConfigManager::Instance().Load(path));

  const auto& logic = config::ConfigManager::Instance().GetServiceConfig().logic;
  EXPECT_EQ(logic.transport, "auto");
  EXPECT_EQ(logic.uds_path, "/tmp/invalid.sock");

  std::filesystem::remove(path);
}

// Integration-only behaviors are skipped in unit tests.
TEST(GatewayErrorHandlingTest, ServiceConnection_NetworkFailure_Retries) {
  GTEST_SKIP() << "Reconnect retries require integration-level control of TcpClient connections.";
}

TEST(GatewayErrorHandlingTest, ServiceConnection_MaxRetriesExceeded_GivesUp) {
  GTEST_SKIP() << "Retry backoff behavior is time-based and not exposed for unit tests.";
}

TEST(GatewayErrorHandlingTest, ClientDisconnect_DuringForward_CleanupRoutes) {
  asio::io_context io_context;
  GatewayServer server;
  auto session_bundle = CreateSession(io_context, 4001);

  server.RegisterConnection(4001, session_bundle.session);

  server.UnregisterSession(session_bundle.session);

  EXPECT_EQ(server.GetConnectionCount(), 0u);
}

TEST(GatewayErrorHandlingTest, MaxConnectionsReached_RejectsNewConnections) {
  GTEST_SKIP() << "Connection acceptance limits are enforced in TcpServer integration tests.";
}

TEST(GatewayErrorHandlingTest, ExtremelyLargePayload_Rejected) {
  GTEST_SKIP() << "Payload size enforcement is validated in packet codec tests.";
}

TEST(GatewayErrorHandlingTest, RapidReconnections_RateLimited) {
  GTEST_SKIP() << "Reconnect rate limiting is not implemented in GatewayServer yet.";
}

}  // namespace mir2::gateway
