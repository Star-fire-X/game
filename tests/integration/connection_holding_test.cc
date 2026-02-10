#include <gtest/gtest.h>

#include <asio.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#define private public
#include "gateway/connection_holder.h"
#include "gateway/gateway_server.h"
#include "logic/logic_server.h"
#undef private

#include "client/network/dual_channel_client.h"
#include "common/enums.h"
#include "common/internal_message_helper.h"
#include "common/protocol/message_codec.h"
#include "common/protocol/packet_codec.h"
#include "common/time_utils.h"
#include "config/config_manager.h"
#include "integration/test_helpers.h"
#include "network/network_manager.h"
#include "network/tcp_session.h"
#include "system_generated.h"

namespace {

using namespace std::chrono_literals;

using mir2::client::DualChannelClient;
using mir2::common::InternalMsgId;
using mir2::common::LoginRequest;
using mir2::common::LoginResponse;
using mir2::common::MessageCodecStatus;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::gateway::ConnectionHolder;
using mir2::gateway::GatewayServer;
using mir2::logic::LogicServer;
using mir2::network::TcpSession;
using mir2::test::integration::WaitForCondition;

constexpr const char* kHost = "127.0.0.1";
constexpr uint16_t kLoginReqMsgId = static_cast<uint16_t>(MsgId::kLoginReq);
constexpr uint16_t kLoginRspMsgId = static_cast<uint16_t>(MsgId::kLoginRsp);
constexpr uint16_t kKcpResetMsgId =
    static_cast<uint16_t>(mir2::common::InternalMsgId::kKcpReset);

struct TestPorts {
  uint16_t gateway_tcp = 0;
  uint16_t gateway_udp = 0;
  uint16_t logic_tcp = 0;
};

uint16_t AllocateTcpPort() {
  asio::io_context io_context;
  asio::ip::tcp::acceptor acceptor(
      io_context,
      asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0));
  return acceptor.local_endpoint().port();
}

uint16_t AllocateUdpPort() {
  asio::io_context io_context;
  asio::ip::udp::socket socket(
      io_context,
      asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0));
  return socket.local_endpoint().port();
}

TestPorts AllocateTestPorts() {
  TestPorts ports;
  do {
    ports.gateway_tcp = AllocateTcpPort();
    ports.logic_tcp = AllocateTcpPort();
  } while (ports.gateway_tcp == ports.logic_tcp);

  do {
    ports.gateway_udp = AllocateUdpPort();
  } while (ports.gateway_udp == ports.gateway_tcp || ports.gateway_udp == ports.logic_tcp);

  return ports;
}

std::filesystem::path CreateTempDir(const std::string& prefix) {
  const auto timestamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  auto dir = std::filesystem::temp_directory_path() /
             (prefix + "_" + std::to_string(timestamp));
  std::filesystem::create_directories(dir);
  return dir;
}

std::string WriteTempConfig(const std::filesystem::path& dir,
                            const std::string& name,
                            const std::string& contents) {
  const auto path = dir / name;
  std::ofstream output(path, std::ios::binary);
  output << contents;
  output.close();
  return path.string();
}

std::string EnvOrDefault(const char* key, const char* default_value) {
  const char* value = std::getenv(key);
  if (value != nullptr && value[0] != '\0') {
    return value;
  }
  return default_value;
}

std::string BuildGatewayConfig(const TestPorts& ports,
                               const std::filesystem::path& log_dir) {
  std::ostringstream out;
  out << "server:\n"
      << "  id: 120\n"
      << "  name: \"Test-Gateway\"\n"
      << "  bind_ip: \"" << kHost << "\"\n"
      << "  port: " << ports.gateway_tcp << "\n"
      << "  udp_port: " << ports.gateway_udp << "\n"
      << "  metrics_port: 0\n"
      << "  io_threads: 1\n"
      << "  max_connections: 128\n"
      << "  tick_interval_ms: 20\n"
      << "  heartbeat_timeout_ms: 0\n"
      << "log:\n"
      << "  level: \"info\"\n"
      << "  path: \"" << log_dir.string() << "\"\n"
      << "services:\n"
      << "  logic:\n"
      << "    host: \"" << kHost << "\"\n"
      << "    port: " << ports.logic_tcp << "\n";
  return out.str();
}

std::string BuildLogicConfig(const TestPorts& ports,
                             const std::filesystem::path& log_dir) {
  const std::string postgres_host = EnvOrDefault("POSTGRES_HOST", kHost);
  const std::string postgres_port = EnvOrDefault("POSTGRES_PORT", "5432");
  const std::string postgres_user = EnvOrDefault("POSTGRES_USER", "mir2");
  const std::string postgres_password = EnvOrDefault("POSTGRES_PASSWORD", "mir2_password");
  const std::string postgres_db = EnvOrDefault("POSTGRES_DB", "mir2_game");

  const std::string db_host = EnvOrDefault("MIR2_DB_HOST", postgres_host.c_str());
  const std::string db_port = EnvOrDefault("MIR2_DB_PORT", postgres_port.c_str());
  const std::string db_user = EnvOrDefault("MIR2_DB_USER", postgres_user.c_str());
  const std::string db_password =
      EnvOrDefault("MIR2_DB_PASSWORD", postgres_password.c_str());
  const std::string db_name = EnvOrDefault("MIR2_DB_NAME", postgres_db.c_str());

  std::ostringstream out;
  out << "server:\n"
      << "  id: 220\n"
      << "  name: \"Test-Logic\"\n"
      << "  bind_ip: \"" << kHost << "\"\n"
      << "  port: " << ports.logic_tcp << "\n"
      << "  metrics_port: 0\n"
      << "  io_threads: 1\n"
      << "  max_connections: 128\n"
      << "  tick_interval_ms: 20\n"
      << "log:\n"
      << "  level: \"info\"\n"
      << "  path: \"" << log_dir.string() << "\"\n"
      << "services:\n"
      << "  logic:\n"
      << "    host: \"" << kHost << "\"\n"
      << "    port: " << ports.logic_tcp << "\n"
      << "database:\n"
      << "  host: \"" << db_host << "\"\n"
      << "  port: " << db_port << "\n"
      << "  user: \"" << db_user << "\"\n"
      << "  password: \"" << db_password << "\"\n"
      << "  database: \"" << db_name << "\"\n";
  return out.str();
}

class ConnectionHoldingIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ports_ = AllocateTestPorts();
    temp_dir_ = CreateTempDir("mir2_connection_holding");
    gateway_config_path_ = WriteTempConfig(
        temp_dir_,
        "gateway.yaml",
        BuildGatewayConfig(ports_, temp_dir_ / "gateway_logs"));
    logic_config_path_ = WriteTempConfig(
        temp_dir_,
        "logic.yaml",
        BuildLogicConfig(ports_, temp_dir_ / "logic_logs"));

    gateway_ = std::make_unique<GatewayServer>();
    ASSERT_TRUE(gateway_->Initialize(gateway_config_path_));

    ASSERT_TRUE(StartLogicServer());
    gateway_->Run();

    client_ = std::make_unique<DualChannelClient>();
  }

  void TearDown() override {
    if (client_) {
      client_->disconnect();
    }
    if (gateway_) {
      gateway_->Shutdown();
    }
    StopLogicServer();
    gateway_.reset();
    client_.reset();
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
  }

  bool StartLogicServer() {
    if (logic_) {
      return true;
    }
    logic_ = std::make_unique<LogicServer>();
    if (!logic_->Initialize(logic_config_path_)) {
      logic_.reset();
      return false;
    }
    InstallLogicHandlers();
    logic_thread_ = std::thread([this]() { logic_->Run(); });
    return true;
  }

  void StopLogicServer() {
    if (logic_) {
      logic_->Shutdown();
    }
    if (logic_thread_.joinable()) {
      logic_thread_.join();
    }
    logic_.reset();
  }

  void InstallLogicHandlers() {
    ASSERT_NE(logic_, nullptr);
    ASSERT_NE(logic_->network_, nullptr);

    logic_->network_->RegisterHandler(
        static_cast<uint16_t>(InternalMsgId::kRoutedMessage),
        [](const std::shared_ptr<TcpSession>& session,
           const std::vector<uint8_t>& payload) {
          if (!session) {
            return;
          }
          mir2::common::RoutedMessageData routed;
          if (!mir2::common::ParseRoutedMessage(payload, &routed)) {
            return;
          }
          if (routed.msg_id != kLoginReqMsgId) {
            return;
          }

          LoginResponse response;
          response.code = mir2::proto::ErrorCode::ERR_OK;
          response.account_id = 7;
          response.session_token = "holding_token";

          MessageCodecStatus status = MessageCodecStatus::kOk;
          auto rsp_payload = mir2::common::EncodeLoginResponse(response, &status);
          if (status != MessageCodecStatus::kOk || rsp_payload.empty()) {
            return;
          }

          const auto routed_rsp = mir2::common::BuildRoutedMessage(
              routed.client_id,
              kLoginRspMsgId,
              rsp_payload);
          session->Send(static_cast<uint16_t>(InternalMsgId::kRoutedMessage), routed_rsp);
        });
  }

  bool ConnectClient() {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
      if (client_->connect(kHost, ports_.gateway_tcp)) {
        break;
      }
      std::this_thread::sleep_for(50ms);
    }

    return WaitForCondition(
        [&]() { return client_->is_connected(); },
        2s,
        10ms,
        [this]() { client_->update(); });
  }

  bool WaitForLogicConnected(std::chrono::milliseconds timeout) {
    return WaitForCondition(
        [this]() { return gateway_->IsLogicConnected(); },
        timeout,
        10ms,
        [this]() { client_->update(); });
  }

  uint64_t WaitForClientConnectionId(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (gateway_ && gateway_->network_) {
        auto sessions = gateway_->network_->GetAllSessions();
        for (const auto& session : sessions) {
          if (session) {
            return session->GetSessionId();
          }
        }
      }
      std::this_thread::sleep_for(10ms);
    }
    return 0;
  }

  ConnectionHolder::State GetHolderState() const {
    std::shared_lock<std::shared_mutex> lock(gateway_->session_map_mutex_);
    return gateway_->holder_state_;
  }

  bool HolderHasBufferedMessages(uint64_t connection_id) const {
    std::shared_lock<std::shared_mutex> lock(gateway_->session_map_mutex_);
    auto it = gateway_->connection_holders_.find(connection_id);
    if (it == gateway_->connection_holders_.end() || !it->second) {
      return false;
    }
    return it->second->HasBufferedMessages();
  }

  std::optional<NetworkPacket> WaitForPacket(uint16_t msg_id,
                                             std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      PumpClient();
      auto it = std::find_if(
          pending_packets_.begin(),
          pending_packets_.end(),
          [msg_id](const NetworkPacket& packet) { return packet.msg_id == msg_id; });
      if (it != pending_packets_.end()) {
        NetworkPacket packet = std::move(*it);
        pending_packets_.erase(it);
        return packet;
      }
      std::this_thread::sleep_for(2ms);
    }
    PumpClient();
    auto it = std::find_if(
        pending_packets_.begin(),
        pending_packets_.end(),
        [msg_id](const NetworkPacket& packet) { return packet.msg_id == msg_id; });
    if (it != pending_packets_.end()) {
      NetworkPacket packet = std::move(*it);
      pending_packets_.erase(it);
      return packet;
    }
    return std::nullopt;
  }

  std::vector<uint8_t> BuildLoginPayload() const {
    LoginRequest request;
    request.username = "holding_user";
    request.password = "holding_pw";
    request.version = std::to_string(
        static_cast<uint32_t>(mir2::proto::SchemaVersion::kSchemaVersion));

    MessageCodecStatus status = MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeLoginRequest(request, &status);
    EXPECT_EQ(status, MessageCodecStatus::kOk);
    return payload;
  }

  void PumpClient() {
    if (!client_) {
      return;
    }
    client_->update();
    while (true) {
      auto packet = client_->receive();
      if (!packet) {
        break;
      }
      pending_packets_.push_back(std::move(*packet));
    }
  }

  TestPorts ports_{};
  std::filesystem::path temp_dir_;
  std::string gateway_config_path_;
  std::string logic_config_path_;
  std::unique_ptr<GatewayServer> gateway_;
  std::unique_ptr<LogicServer> logic_;
  std::thread logic_thread_;
  std::unique_ptr<DualChannelClient> client_;
  std::deque<NetworkPacket> pending_packets_;
};

TEST_F(ConnectionHoldingIntegrationTest, BuffersAndFlushesOnLogicRestart) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));

  const uint64_t connection_id = WaitForClientConnectionId(2s);
  ASSERT_NE(connection_id, 0u);

  StopLogicServer();
  ASSERT_TRUE(WaitForCondition(
      [this]() { return GetHolderState() == ConnectionHolder::State::HOLDING; },
      2s,
      10ms,
      [this]() { client_->update(); }));

  const auto login_payload = BuildLoginPayload();
  ASSERT_FALSE(login_payload.empty());
  client_->send(kLoginReqMsgId, login_payload);

  auto immediate_rsp = WaitForPacket(kLoginRspMsgId, 200ms);
  EXPECT_FALSE(immediate_rsp.has_value());
  EXPECT_TRUE(HolderHasBufferedMessages(connection_id));

  std::this_thread::sleep_for(100ms);
  ASSERT_TRUE(StartLogicServer());

  ASSERT_TRUE(WaitForLogicConnected(5s));
  ASSERT_TRUE(WaitForCondition(
      [this]() {
        return logic_ &&
               logic_->last_context_request_id_.load(std::memory_order_relaxed) > 0;
      },
      5s,
      10ms,
      [this]() { client_->update(); }));

  auto reset_packet = WaitForPacket(kKcpResetMsgId, 5s);
  ASSERT_TRUE(reset_packet.has_value());
  flatbuffers::Verifier verifier(reset_packet->payload.data(),
                                 reset_packet->payload.size());
  EXPECT_TRUE(verifier.VerifyBuffer<mir2::proto::KcpReset>(nullptr));

  auto login_rsp = WaitForPacket(kLoginRspMsgId, 5s);
  ASSERT_TRUE(login_rsp.has_value());

  LoginResponse response;
  MessageCodecStatus status =
      mir2::common::DecodeLoginResponse(login_rsp->msg_id,
                                        login_rsp->payload,
                                        &response);
  EXPECT_EQ(status, MessageCodecStatus::kOk);
  EXPECT_EQ(response.code, mir2::proto::ErrorCode::ERR_OK);

  ASSERT_TRUE(WaitForCondition(
      [this]() { return GetHolderState() == ConnectionHolder::State::FORWARDING; },
      5s,
      10ms,
      [this]() { client_->update(); }));
  EXPECT_FALSE(HolderHasBufferedMessages(connection_id));
}

}  // namespace
