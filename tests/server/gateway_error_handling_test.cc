#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

#include "config/config_manager.h"
#include "network/packet_codec.h"
#include "network/tcp_connection.h"
#include "network/tcp_session.h"
#include "mocks/mock_socket.h"

#define private public
#include "gateway/gateway_server.h"
#undef private

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

void DrainIoContext(asio::io_context& io_context) {
  while (io_context.poll_one() > 0) {
  }
  io_context.restart();
}

bool DecodeSinglePacket(const std::vector<uint8_t>& bytes, network::Packet* out_packet) {
  if (bytes.empty() || !out_packet) {
    return false;
  }
  if (network::PacketCodec::Decode(bytes.data(), bytes.size(), out_packet) ==
      network::DecodeStatus::kOk) {
    return true;
  }
  return network::PacketCodec::DecodeV2(bytes.data(), bytes.size(), out_packet) ==
         network::DecodeStatus::kOk;
}

template <typename Predicate>
bool WaitUntil(Predicate&& predicate, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

uint16_t FindFreeTcpPort() {
  asio::io_context io_context;
  asio::ip::tcp::acceptor acceptor(
      io_context, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0));
  return acceptor.local_endpoint().port();
}

std::string BuildLogicServiceConfig(uint16_t logic_port) {
  return std::string(
             "services:\n"
             "  logic:\n"
             "    host: \"127.0.0.1\"\n"
             "    port: ") +
         std::to_string(logic_port) + "\n";
}

class TestTcpListener {
 public:
  explicit TestTcpListener(uint16_t port)
      : acceptor_(io_context_) {
    asio::error_code ec;
    acceptor_.open(asio::ip::tcp::v4(), ec);
    if (ec) {
      return;
    }
    acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
    if (ec) {
      return;
    }
    acceptor_.bind(
        asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), port), ec);
    if (ec) {
      return;
    }
    acceptor_.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
      return;
    }
    ready_ = true;
  }

  ~TestTcpListener() { Stop(); }

  bool IsReady() const { return ready_; }

  void Start() {
    if (!ready_ || started_) {
      return;
    }
    started_ = true;
    socket_ = std::make_unique<asio::ip::tcp::socket>(io_context_);
    acceptor_.async_accept(*socket_, [this](const asio::error_code& ec) {
      if (!ec) {
        accepted_.store(true, std::memory_order_release);
      }
    });
    thread_ = std::thread([this]() { io_context_.run(); });
  }

  bool Accepted() const {
    return accepted_.load(std::memory_order_acquire);
  }

  void Stop() {
    if (!started_) {
      return;
    }
    started_ = false;
    asio::error_code ec;
    acceptor_.cancel(ec);
    acceptor_.close(ec);
    if (socket_ && socket_->is_open()) {
      socket_->close(ec);
    }
    io_context_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  asio::io_context io_context_;
  asio::ip::tcp::acceptor acceptor_;
  std::unique_ptr<asio::ip::tcp::socket> socket_;
  std::atomic<bool> accepted_{false};
  std::thread thread_;
  bool ready_ = false;
  bool started_ = false;
};

void ShutdownReconnectTestServer(GatewayServer* server) {
  if (!server) {
    return;
  }
  server->shutting_down_.store(true, std::memory_order_release);
  server->logic_reconnecting_.store(false, std::memory_order_release);
  if (server->logic_client_) {
    server->logic_client_->Close();
  }
  server->app_.Shutdown();
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
  EXPECT_EQ(config.service_link_write_queue_size, defaults.service_link_write_queue_size);
  EXPECT_EQ(config.network_session_idle_check_interval_ms,
            defaults.network_session_idle_check_interval_ms);
  EXPECT_EQ(config.network_session_idle_timeout_ms,
            defaults.network_session_idle_timeout_ms);
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
  EXPECT_FALSE(defaults.legacy_fallback_enabled);
  EXPECT_FALSE(defaults.legacy_fallback_allow_auth_whitelist);
  EXPECT_FALSE(defaults.legacy_fallback_allow_critical_msgs);
  EXPECT_FALSE(defaults.legacy_fallback_allow_normal_msgs);
  EXPECT_FALSE(defaults.queue_full_fallback_non_best_effort_enabled);
  EXPECT_TRUE(defaults.chat_batch_send_enabled);
  EXPECT_EQ(defaults.movement_speed_violation_severity, 10);
  EXPECT_EQ(defaults.movement_teleport_violation_severity, 5);
  EXPECT_EQ(defaults.service_link_write_queue_size, 8192);
  EXPECT_EQ(defaults.network_session_idle_check_interval_ms, 30000);
  EXPECT_EQ(defaults.network_session_idle_timeout_ms, 90000);
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
      "  chat_batch_send_enabled: false\n"
      "  movement_speed_violation_severity: 20\n"
      "  movement_teleport_violation_severity: 9\n"
      "  service_link_write_queue_size: 777\n"
      "  network_session_idle_check_interval_ms: 4567\n"
      "  network_session_idle_timeout_ms: 12345\n",
      "login_ip_limit");
  ASSERT_TRUE(config::ConfigManager::Instance().Load(path));

  const auto& config = config::ConfigManager::Instance().GetServerConfig();
  EXPECT_EQ(config.login_ip_rate_limit_capacity, 12);
  EXPECT_EQ(config.login_ip_rate_limit_refill_rate, 3);
  EXPECT_EQ(config.login_username_rate_limit_capacity, 22);
  EXPECT_EQ(config.login_username_rate_limit_refill_rate, 8);
  EXPECT_EQ(config.login_username_rate_limit_refill_interval_seconds, 2);
  EXPECT_EQ(config.udp_send_fault_inject_every_n, 77);
  EXPECT_FALSE(config.chat_batch_send_enabled);
  EXPECT_EQ(config.movement_speed_violation_severity, 20);
  EXPECT_EQ(config.movement_teleport_violation_severity, 9);
  EXPECT_EQ(config.service_link_write_queue_size, 777);
  EXPECT_EQ(config.network_session_idle_check_interval_ms, 4567);
  EXPECT_EQ(config.network_session_idle_timeout_ms, 12345);

  std::filesystem::remove(path);
}

TEST(GatewayErrorHandlingTest, LoadConfig_RemovedLegacyFallbackKeyFails) {
  const auto path = WriteTempConfig(
      "server:\n"
      "  legacy_fallback_enabled: false\n",
      "removed_legacy_key");
  EXPECT_FALSE(config::ConfigManager::Instance().Load(path));
  std::filesystem::remove(path);
}

TEST(GatewayErrorHandlingTest, LoadConfig_RemovedMessageRoutesFails) {
  const auto path = WriteTempConfig(
      "message_routes:\n"
      "  - msg_id: 1001\n"
      "    service: logic\n"
      "    require_auth: false\n",
      "removed_message_routes");
  EXPECT_FALSE(config::ConfigManager::Instance().Load(path));
  std::filesystem::remove(path);
}

TEST(GatewayErrorHandlingTest, LoadConfig_InvalidGatewayThresholdsFail) {
  const auto path = WriteTempConfig(
      "server:\n"
      "  gateway_backpressure_default_pause_ms: 200\n"
      "  gateway_backpressure_max_pause_ms: 100\n",
      "invalid_gateway_threshold");
  EXPECT_FALSE(config::ConfigManager::Instance().Load(path));
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

TEST(GatewayErrorHandlingTest, ServiceConnection_NetworkFailure_Retries) {
  const uint16_t logic_port = FindFreeTcpPort();
  const auto path = WriteTempConfig(BuildLogicServiceConfig(logic_port), "logic_retry");
  ASSERT_TRUE(config::ConfigManager::Instance().Load(path));
  std::filesystem::remove(path);

  GatewayServer server;
  config::ServerConfig app_config;
  app_config.io_threads = 1;
  app_config.tick_interval_ms = 10;
  ASSERT_TRUE(server.app_.Initialize(app_config));
  server.logic_reconnect_initial_backoff_ms_ = 100;
  server.logic_reconnect_max_backoff_ms_ = 100;
  server.logic_reconnect_max_retries_ = 5;

  server.ScheduleReconnect(0);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  TestTcpListener listener(logic_port);
  ASSERT_TRUE(listener.IsReady());
  listener.Start();

  EXPECT_TRUE(WaitUntil([&listener]() { return listener.Accepted(); },
                        std::chrono::milliseconds(1500)));

  listener.Stop();
  ShutdownReconnectTestServer(&server);
}

TEST(GatewayErrorHandlingTest, ServiceConnection_MaxRetriesExceeded_GivesUp) {
  const uint16_t logic_port = FindFreeTcpPort();
  const auto path = WriteTempConfig(BuildLogicServiceConfig(logic_port), "logic_retry_limit");
  ASSERT_TRUE(config::ConfigManager::Instance().Load(path));
  std::filesystem::remove(path);

  GatewayServer server;
  config::ServerConfig app_config;
  app_config.io_threads = 1;
  app_config.tick_interval_ms = 10;
  ASSERT_TRUE(server.app_.Initialize(app_config));
  server.logic_reconnect_initial_backoff_ms_ = 50;
  server.logic_reconnect_max_backoff_ms_ = 50;
  server.logic_reconnect_max_retries_ = 1;

  server.ScheduleReconnect(0);

  EXPECT_TRUE(WaitUntil([&server]() {
    return !server.logic_reconnecting_.load(std::memory_order_acquire);
  }, std::chrono::milliseconds(1000)));

  TestTcpListener listener(logic_port);
  ASSERT_TRUE(listener.IsReady());
  listener.Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(350));
  EXPECT_FALSE(listener.Accepted());

  listener.Stop();
  ShutdownReconnectTestServer(&server);
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
  asio::io_context io_context;
  GatewayServer server;
  server.max_connections_limit_ = 1;

  auto session_a = CreateSession(io_context, 5001);
  auto session_b = CreateSession(io_context, 5002);
  session_a.session->Start();
  session_b.session->Start();

  server.RegisterConnection(5001, session_a.session);
  server.RegisterConnection(5002, session_b.session);
  DrainIoContext(io_context);

  EXPECT_EQ(server.GetConnectionCount(), 1u);
  EXPECT_NE(server.GetConnectionSession(5001), nullptr);
  EXPECT_EQ(server.GetConnectionSession(5002), nullptr);

  const auto& writes = session_b.socket->GetWrites();
  ASSERT_FALSE(writes.empty());
  network::Packet packet{};
  ASSERT_TRUE(DecodeSinglePacket(writes.front(), &packet));
  EXPECT_EQ(packet.msg_id, static_cast<uint16_t>(common::MsgId::kKick));
}

TEST(GatewayErrorHandlingTest, ExtremelyLargePayload_Rejected) {
  asio::io_context io_context;
  GatewayServer server;
  server.max_forward_payload_bytes_ = 32;

  auto session_bundle = CreateSession(io_context, 6001);
  session_bundle.session->Start();
  server.RegisterConnection(6001, session_bundle.session);

  const std::vector<uint8_t> payload(1024, 0xAB);
  server.HandleForwardMessage(session_bundle.session,
                              static_cast<uint16_t>(common::MsgId::kMoveReq),
                              mir2::common::ChannelType::kTcp,
                              payload);
  DrainIoContext(io_context);

  const auto& writes = session_bundle.socket->GetWrites();
  ASSERT_FALSE(writes.empty());
  network::Packet packet{};
  ASSERT_TRUE(DecodeSinglePacket(writes.front(), &packet));
  EXPECT_EQ(packet.msg_id, static_cast<uint16_t>(common::MsgId::kKick));
}

TEST(GatewayErrorHandlingTest, RapidReconnections_RateLimited) {
  const uint16_t logic_port = FindFreeTcpPort();
  const auto path = WriteTempConfig(BuildLogicServiceConfig(logic_port), "logic_retry_rate_limit");
  ASSERT_TRUE(config::ConfigManager::Instance().Load(path));
  std::filesystem::remove(path);

  GatewayServer server;
  config::ServerConfig app_config;
  app_config.io_threads = 1;
  app_config.tick_interval_ms = 10;
  ASSERT_TRUE(server.app_.Initialize(app_config));
  server.logic_reconnect_initial_backoff_ms_ = 200;
  server.logic_reconnect_max_backoff_ms_ = 200;
  server.logic_reconnect_max_retries_ = 3;

  TestTcpListener listener(logic_port);
  ASSERT_TRUE(listener.IsReady());
  listener.Start();

  const auto start = std::chrono::steady_clock::now();
  server.ScheduleReconnect(0);
  server.ScheduleReconnect(0);
  server.ScheduleReconnect(0);

  ASSERT_TRUE(WaitUntil([&listener]() { return listener.Accepted(); },
                        std::chrono::milliseconds(1500)));
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start).count();
  EXPECT_GE(elapsed_ms, 120);

  listener.Stop();
  ShutdownReconnectTestServer(&server);
}

}  // namespace mir2::gateway
