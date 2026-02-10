#include <gtest/gtest.h>

#include <asio.hpp>
#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#define private public
#include "gateway/gateway_server.h"
#include "logic/logic_server.h"
#undef private

#include "client/network/dual_channel_client.h"
#include "common/enums.h"
#include "common/internal_message_helper.h"
#include "chat_generated.h"
#include "combat_generated.h"
#include "game_generated.h"
#include "guild_generated.h"
#include "item_generated.h"
#include "login_generated.h"
#include "integration/test_helpers.h"
#include "network/tcp_connection.h"
#include "network/network_manager.h"
#include "network/tcp_session.h"
#include "mocks/mock_socket.h"

namespace {

using namespace std::chrono_literals;

using mir2::client::DualChannelClient;
using mir2::common::InternalMsgId;
using mir2::common::MsgId;
using mir2::gateway::GatewayServer;
using mir2::logic::LogicServer;
using mir2::network::TcpSession;
using mir2::test::integration::WaitForCondition;

constexpr const char* kHost = "127.0.0.1";

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
      << "  id: 110\n"
      << "  name: \"Test-Gateway\"\n"
      << "  bind_ip: \"" << kHost << "\"\n"
      << "  port: " << ports.gateway_tcp << "\n"
      << "  udp_port: " << ports.gateway_udp << "\n"
      << "  metrics_port: 0\n"
      << "  io_threads: 1\n"
      << "  max_connections: 6000\n"
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
      << "  id: 210\n"
      << "  name: \"Test-Logic\"\n"
      << "  bind_ip: \"" << kHost << "\"\n"
      << "  port: " << ports.logic_tcp << "\n"
      << "  metrics_port: 0\n"
      << "  io_threads: 1\n"
      << "  max_connections: 6000\n"
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

std::vector<uint8_t> BuildPayloadForMsgId(uint16_t msg_id) {
  flatbuffers::FlatBufferBuilder builder;

  switch (static_cast<MsgId>(msg_id)) {
    case MsgId::kLoginReq: {
      const auto username = builder.CreateString("integration_user");
      const auto password = builder.CreateString("integration_pw");
      const auto version = builder.CreateString(std::to_string(
          static_cast<uint32_t>(mir2::proto::SchemaVersion::kSchemaVersion)));
      builder.Finish(mir2::proto::CreateLoginReq(builder, username, password, version));
      break;
    }
    case MsgId::kLogout: {
      const auto token = builder.CreateString("session_token");
      builder.Finish(mir2::proto::CreateLogoutReq(builder, 1, token));
      break;
    }
    case MsgId::kCreateRoleReq: {
      const auto name = builder.CreateString("RoleA");
      builder.Finish(mir2::proto::CreateCreateRoleReq(
          builder, name, mir2::proto::Profession::WARRIOR, mir2::proto::Gender::MALE));
      break;
    }
    case MsgId::kSelectRoleReq:
      builder.Finish(mir2::proto::CreateSelectRoleReq(builder, 1));
      break;
    case MsgId::kRoleListReq: {
      const auto token = builder.CreateString("session_token");
      builder.Finish(mir2::proto::CreateRoleListReq(builder, 1, token));
      break;
    }
    case MsgId::kMoveReq:
      builder.Finish(mir2::proto::CreateMoveReq(builder, 10, 20));
      break;
    case MsgId::kAttackReq:
      builder.Finish(
          mir2::proto::CreateAttackReq(builder, 100, mir2::proto::EntityType::MONSTER));
      break;
    case MsgId::kSkillReq:
      builder.Finish(mir2::proto::CreateSkillReq(builder, 1, 100));
      break;
    case MsgId::kChatReq: {
      const auto content = builder.CreateString("hello");
      builder.Finish(
          mir2::proto::CreateChatReq(builder, mir2::proto::ChatChannel::WORLD, content, 0));
      break;
    }
    case MsgId::kUseItemReq:
      builder.Finish(mir2::proto::CreateUseItemReq(builder, 1, 1001));
      break;
    case MsgId::kDropItemReq:
      builder.Finish(mir2::proto::CreateDropItemReq(builder, 1, 1001, 1));
      break;
    case MsgId::kPickupItemReq:
      builder.Finish(mir2::proto::CreatePickupItemReq(builder, 1001));
      break;
    case MsgId::kEquipReq:
      builder.Finish(mir2::proto::CreateEquipReq(builder, 1, 1001));
      break;
    case MsgId::kUnequipReq:
      builder.Finish(mir2::proto::CreateUnequipReq(builder, 1));
      break;
    case MsgId::kNpcInteractReq:
    case MsgId::kNpcMenuSelect:
      return std::vector<uint8_t>{'{', '}'};
    case MsgId::kGuildChat: {
      const auto from_name = builder.CreateString("tester");
      const auto content = builder.CreateString("guild msg");
      builder.Finish(mir2::proto::CreateChatMessage(
          builder,
          mir2::proto::ChatChannel::GUILD,
          1,
          from_name,
          0,
          content,
          0xFFFFFFu,
          1));
      break;
    }
    default:
      break;
  }

  switch (static_cast<mir2::proto::GuildMessageType>(msg_id)) {
    case mir2::proto::GuildMessageType::CREATE: {
      const auto guild_name = builder.CreateString("GuildA");
      builder.Finish(mir2::proto::CreateCreateGuildRequest(builder, guild_name));
      break;
    }
    case mir2::proto::GuildMessageType::JOIN:
      builder.Finish(mir2::proto::CreateJoinGuildRequest(builder, 1));
      break;
    case mir2::proto::GuildMessageType::LEAVE:
      builder.Finish(mir2::proto::CreateLeaveGuildRequest(builder));
      break;
    case mir2::proto::GuildMessageType::DECLARE_WAR:
    case mir2::proto::GuildMessageType::CANCEL_WAR:
      builder.Finish(mir2::proto::CreateDeclareWarRequest(builder, 1));
      break;
    case mir2::proto::GuildMessageType::MAKE_ALLY:
    case mir2::proto::GuildMessageType::BREAK_ALLY:
      builder.Finish(mir2::proto::CreateMakeAllianceRequest(builder, 1));
      break;
    case mir2::proto::GuildMessageType::KICK:
    case mir2::proto::GuildMessageType::UPDATE_NOTICE:
    case mir2::proto::GuildMessageType::UPDATE_RANK:
      return std::vector<uint8_t>{1};
    default:
      break;
  }

  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

struct RoutedCapture {
  void Push(uint16_t msg_id, uint64_t client_id) {
    std::lock_guard<std::mutex> lock(mutex);
    msg_ids.push_back(msg_id);
    client_ids.push_back(client_id);
    cv.notify_all();
  }

  size_t Size() const {
    std::lock_guard<std::mutex> lock(mutex);
    return msg_ids.size();
  }

  bool Contains(uint16_t msg_id) const {
    std::lock_guard<std::mutex> lock(mutex);
    return std::find(msg_ids.begin(), msg_ids.end(), msg_id) != msg_ids.end();
  }

  bool ContainsAll(const std::vector<uint16_t>& expected) const {
    std::lock_guard<std::mutex> lock(mutex);
    std::unordered_set<uint16_t> seen(msg_ids.begin(), msg_ids.end());
    for (uint16_t msg_id : expected) {
      if (seen.count(msg_id) == 0) {
        return false;
      }
    }
    return true;
  }

  std::vector<uint16_t> DrainMsgIds() {
    std::lock_guard<std::mutex> lock(mutex);
    return msg_ids;
  }

  std::vector<uint64_t> DrainClientIds() {
    std::lock_guard<std::mutex> lock(mutex);
    return client_ids;
  }

  mutable std::mutex mutex;
  std::condition_variable cv;
  std::vector<uint16_t> msg_ids;
  std::vector<uint64_t> client_ids;
};

std::shared_ptr<TcpSession> CreateMockSession(asio::io_context& io_context,
                                              uint64_t connection_id) {
  auto mock_socket = std::make_unique<mir2::network::MockSocket>(io_context.get_executor());
  auto connection = std::make_shared<mir2::network::TcpConnection>(
      std::move(mock_socket), connection_id);
  return std::make_shared<TcpSession>(connection);
}

class GatewayLogicUniversalForwardTest : public ::testing::Test {
 protected:
  struct RoutedHandlerState {
    std::mutex mutex;
    std::function<void(const mir2::common::RoutedMessageData&)> handler;
  };

  void SetUp() override {
    ports_ = AllocateTestPorts();
    temp_dir_ = CreateTempDir("mir2_gateway_logic_universal");
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

    logic_ = std::make_unique<LogicServer>();
    ASSERT_TRUE(logic_->Initialize(logic_config_path_));
    routed_handler_state_ = std::make_shared<RoutedHandlerState>();
    InstallLogicHandler();

    logic_thread_ = std::thread([this]() { logic_->Run(); });
    gateway_->Run();

    client_ = std::make_unique<DualChannelClient>();
  }

  void TearDown() override {
    if (routed_handler_state_) {
      std::lock_guard<std::mutex> lock(routed_handler_state_->mutex);
      routed_handler_state_->handler = {};
    }
    routed_handler_state_.reset();

    if (client_) {
      client_->disconnect();
    }
    if (logic_) {
      logic_->Shutdown();
    }
    if (logic_thread_.joinable()) {
      logic_thread_.join();
    }
    if (gateway_) {
      gateway_->Shutdown();
    }
    gateway_.reset();
    logic_.reset();
    client_.reset();
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
  }

  void InstallLogicHandler() {
    ASSERT_NE(logic_, nullptr);
    ASSERT_NE(logic_->network_, nullptr);
    ASSERT_NE(routed_handler_state_, nullptr);

    logic_->network_->RegisterHandler(
        static_cast<uint16_t>(InternalMsgId::kRoutedMessage),
        [state_weak = std::weak_ptr<RoutedHandlerState>(routed_handler_state_)](
            const std::shared_ptr<TcpSession>&, const std::vector<uint8_t>& payload) {
          mir2::common::RoutedMessageData routed;
          if (!mir2::common::ParseRoutedMessage(payload, &routed)) {
            return;
          }

          auto state = state_weak.lock();
          if (!state) {
            return;
          }

          std::function<void(const mir2::common::RoutedMessageData&)> handler_copy;
          {
            std::lock_guard<std::mutex> lock(state->mutex);
            handler_copy = state->handler;
          }
          if (handler_copy) {
            handler_copy(routed);
          }
        });
  }

  void SetRoutedHandler(std::function<void(const mir2::common::RoutedMessageData&)> handler) {
    ASSERT_NE(routed_handler_state_, nullptr);
    std::lock_guard<std::mutex> lock(routed_handler_state_->mutex);
    routed_handler_state_->handler = std::move(handler);
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

  bool WaitForGatewayForwarding(std::chrono::milliseconds timeout) {
    return WaitForCondition(
        [this]() {
          return gateway_->holder_state_ ==
                 mir2::gateway::ConnectionHolder::State::FORWARDING;
        },
        timeout,
        10ms,
        [this]() { client_->update(); });
  }

  void PumpClientFor(std::chrono::milliseconds duration) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
      if (client_) {
        client_->update();
      }
      std::this_thread::sleep_for(2ms);
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

  std::shared_ptr<RoutedHandlerState> routed_handler_state_;
};

// 验证扩展消息矩阵都会通过Gateway转发到LogicServer。
TEST_F(GatewayLogicUniversalForwardTest, AllMessageTypesForwarded) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));
  // 等待 KcpReset/握手余波处理完成，减少首批消息丢失抖动。
  PumpClientFor(200ms);

  auto capture = std::make_shared<RoutedCapture>();
  SetRoutedHandler([capture_weak = std::weak_ptr<RoutedCapture>(capture)](
                       const mir2::common::RoutedMessageData& routed) {
    if (auto capture_locked = capture_weak.lock()) {
      capture_locked->Push(routed.msg_id, routed.client_id);
    }
  });

  const std::vector<uint16_t> msg_ids = {
      static_cast<uint16_t>(MsgId::kLoginReq),
      static_cast<uint16_t>(MsgId::kLogout),
      static_cast<uint16_t>(MsgId::kCreateRoleReq),
      static_cast<uint16_t>(MsgId::kSelectRoleReq),
      static_cast<uint16_t>(MsgId::kRoleListReq),
      static_cast<uint16_t>(MsgId::kMoveReq),
      static_cast<uint16_t>(MsgId::kAttackReq),
      static_cast<uint16_t>(MsgId::kSkillReq),
      static_cast<uint16_t>(MsgId::kChatReq),
      static_cast<uint16_t>(MsgId::kUseItemReq),
      static_cast<uint16_t>(MsgId::kDropItemReq),
      static_cast<uint16_t>(MsgId::kPickupItemReq),
      static_cast<uint16_t>(MsgId::kEquipReq),
      static_cast<uint16_t>(MsgId::kUnequipReq),
      static_cast<uint16_t>(MsgId::kNpcInteractReq),
      static_cast<uint16_t>(MsgId::kNpcMenuSelect),
      static_cast<uint16_t>(MsgId::kGuildChat),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::CREATE),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::JOIN),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::LEAVE),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::KICK),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::DECLARE_WAR),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::CANCEL_WAR),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::MAKE_ALLY),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::BREAK_ALLY),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::UPDATE_NOTICE),
      static_cast<uint16_t>(mir2::proto::GuildMessageType::UPDATE_RANK),
  };

  auto send_messages = [&msg_ids, capture, this](bool only_missing) {
    for (auto msg_id : msg_ids) {
      if (only_missing && capture->Contains(msg_id)) {
        continue;
      }
      auto payload = BuildPayloadForMsgId(msg_id);
      ASSERT_FALSE(payload.empty());
      client_->send(msg_id, payload);
      client_->update();
    }
  };

  auto wait_for_all = [&msg_ids, capture, this]() {
    return WaitForCondition(
        [capture, &msg_ids]() { return capture->ContainsAll(msg_ids); },
        6s,
        5ms,
        [this]() { client_->update(); });
  };

  send_messages(/*only_missing=*/false);
  if (!wait_for_all()) {
    // 仅补发未观测到的消息类型，提升偶发网络抖动下稳定性。
    send_messages(/*only_missing=*/true);
  }
  ASSERT_TRUE(wait_for_all());

  auto forwarded_ids = capture->DrainMsgIds();
  EXPECT_GE(forwarded_ids.size(), msg_ids.size());

  std::unordered_set<uint16_t> forwarded_set(forwarded_ids.begin(), forwarded_ids.end());
  for (auto msg_id : msg_ids) {
    EXPECT_TRUE(forwarded_set.count(msg_id) > 0);
  }

  auto client_ids = capture->DrainClientIds();
  ASSERT_FALSE(client_ids.empty());
  const uint64_t first_client = client_ids.front();
  EXPECT_NE(first_client, 0u);
  for (auto client_id : client_ids) {
    EXPECT_EQ(client_id, first_client);
  }

  // 给异步转发链路一个短暂排空窗口，避免带着在途任务进入 teardown。
  PumpClientFor(100ms);
}

// 验证Gateway不做认证检查，由LogicServer自行判断。
TEST_F(GatewayLogicUniversalForwardTest, NoAuthCheckAtGateway) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));
  // 等待 KcpReset/握手余波处理完成，减少首批消息抖动。
  PumpClientFor(200ms);

  auto authed = std::make_shared<std::atomic<bool>>(false);
  auto unauth_seen = std::make_shared<std::atomic<int>>(0);
  auto authed_seen = std::make_shared<std::atomic<int>>(0);

  SetRoutedHandler([authed, unauth_seen, authed_seen](
                       const mir2::common::RoutedMessageData& routed) {
    if (routed.msg_id == static_cast<uint16_t>(MsgId::kLoginReq)) {
      authed->store(true, std::memory_order_release);
      return;
    }
    if (!authed->load(std::memory_order_acquire)) {
      unauth_seen->fetch_add(1, std::memory_order_relaxed);
    } else {
      authed_seen->fetch_add(1, std::memory_order_relaxed);
    }
  });

  // 未登录直接发送MoveReq，Logic侧应收到并标记未认证。
  client_->send(static_cast<uint16_t>(MsgId::kMoveReq),
                BuildPayloadForMsgId(static_cast<uint16_t>(MsgId::kMoveReq)));
  ASSERT_TRUE(WaitForCondition(
      [unauth_seen]() { return unauth_seen->load() > 0; }, 3s, [this]() { client_->update(); }));

  // 发送登录请求后，再发送MoveReq应被视为已认证。
  client_->send(static_cast<uint16_t>(MsgId::kLoginReq),
                BuildPayloadForMsgId(static_cast<uint16_t>(MsgId::kLoginReq)));
  ASSERT_TRUE(
      WaitForCondition([authed]() { return authed->load(); }, 3s, [this]() { client_->update(); }));

  client_->send(static_cast<uint16_t>(MsgId::kMoveReq),
                BuildPayloadForMsgId(static_cast<uint16_t>(MsgId::kMoveReq)));
  ASSERT_TRUE(WaitForCondition(
      [authed_seen]() { return authed_seen->load() > 0; }, 3s, [this]() { client_->update(); }));

  EXPECT_GE(unauth_seen->load(), 1);
  EXPECT_GE(authed_seen->load(), 1);
}

// 验证5000并发连接场景下，转发耗时仍在可接受范围内。
TEST_F(GatewayLogicUniversalForwardTest, PerformanceUnder5000Connections) {
  ASSERT_TRUE(WaitForLogicConnected(3s));
  ASSERT_TRUE(WaitForGatewayForwarding(3s));

  constexpr int kConnectionCount = 5000;
  constexpr int kThreadCount = 8;

  auto forwarded_count = std::make_shared<std::atomic<int>>(0);
  SetRoutedHandler([forwarded_count](const mir2::common::RoutedMessageData&) {
    forwarded_count->fetch_add(1, std::memory_order_relaxed);
  });

  asio::io_context io_context;
  std::vector<std::shared_ptr<TcpSession>> sessions;
  sessions.reserve(kConnectionCount);
  for (int i = 0; i < kConnectionCount; ++i) {
    auto session = CreateMockSession(io_context, 900000 + i);
    sessions.push_back(session);
    gateway_->RegisterConnection(900000 + i, session);
  }

  const auto payload = BuildPayloadForMsgId(static_cast<uint16_t>(MsgId::kMoveReq));
  ASSERT_FALSE(payload.empty());
  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);

  const auto start = std::chrono::steady_clock::now();
  for (int t = 0; t < kThreadCount; ++t) {
    threads.emplace_back([&, t]() {
      const int chunk = kConnectionCount / kThreadCount;
      const int begin = t * chunk;
      const int end = (t == kThreadCount - 1) ? kConnectionCount : begin + chunk;
      for (int i = begin; i < end; ++i) {
        gateway_->HandleForwardMessage(
            sessions[static_cast<size_t>(i)],
            static_cast<uint16_t>(MsgId::kMoveReq),
            payload);
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_LT(elapsed, std::chrono::seconds(2));

  ASSERT_TRUE(WaitForCondition(
      [forwarded_count]() { return forwarded_count->load() >= kConnectionCount; },
      5s));
  EXPECT_EQ(forwarded_count->load(), kConnectionCount);
}

}  // namespace
