#include <gtest/gtest.h>

#include <asio.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
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
#include "guild_generated.h"
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
      << "    port: " << ports.logic_tcp << "\n";
  return out.str();
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
    InstallLogicHandler();

    logic_thread_ = std::thread([this]() { logic_->Run(); });
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
    if (logic_) {
      logic_->Shutdown();
    }
    if (logic_thread_.joinable()) {
      logic_thread_.join();
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

    logic_->network_->RegisterHandler(
        static_cast<uint16_t>(InternalMsgId::kRoutedMessage),
        [this](const std::shared_ptr<TcpSession>&,
               const std::vector<uint8_t>& payload) {
          mir2::common::RoutedMessageData routed;
          if (!mir2::common::ParseRoutedMessage(payload, &routed)) {
            return;
          }

          std::function<void(const mir2::common::RoutedMessageData&)> handler_copy;
          {
            std::lock_guard<std::mutex> lock(handler_mutex_);
            handler_copy = routed_handler_;
          }
          if (handler_copy) {
            handler_copy(routed);
          }
        });
  }

  void SetRoutedHandler(std::function<void(const mir2::common::RoutedMessageData&)> handler) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    routed_handler_ = std::move(handler);
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

  TestPorts ports_{};
  std::filesystem::path temp_dir_;
  std::string gateway_config_path_;
  std::string logic_config_path_;
  std::unique_ptr<GatewayServer> gateway_;
  std::unique_ptr<LogicServer> logic_;
  std::thread logic_thread_;
  std::unique_ptr<DualChannelClient> client_;

  std::mutex handler_mutex_;
  std::function<void(const mir2::common::RoutedMessageData&)> routed_handler_;
};

// 验证扩展消息矩阵都会通过Gateway转发到LogicServer。
TEST_F(GatewayLogicUniversalForwardTest, AllMessageTypesForwarded) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));

  RoutedCapture capture;
  SetRoutedHandler([&capture](const mir2::common::RoutedMessageData& routed) {
    capture.Push(routed.msg_id, routed.client_id);
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

  const std::vector<uint8_t> payload{1, 2, 3};
  for (auto msg_id : msg_ids) {
    client_->send(msg_id, payload);
  }

  ASSERT_TRUE(WaitForCondition(
      [&capture, &msg_ids]() { return capture.Size() >= msg_ids.size(); },
      3s));

  auto forwarded_ids = capture.DrainMsgIds();
  ASSERT_EQ(forwarded_ids.size(), msg_ids.size());

  std::unordered_set<uint16_t> forwarded_set(forwarded_ids.begin(), forwarded_ids.end());
  for (auto msg_id : msg_ids) {
    EXPECT_TRUE(forwarded_set.count(msg_id) > 0);
  }

  auto client_ids = capture.DrainClientIds();
  ASSERT_EQ(client_ids.size(), msg_ids.size());
  const uint64_t first_client = client_ids.front();
  EXPECT_NE(first_client, 0u);
  for (auto client_id : client_ids) {
    EXPECT_EQ(client_id, first_client);
  }
}

// 验证Gateway不做认证检查，由LogicServer自行判断。
TEST_F(GatewayLogicUniversalForwardTest, NoAuthCheckAtGateway) {
  ASSERT_TRUE(ConnectClient());
  ASSERT_TRUE(WaitForLogicConnected(3s));

  std::atomic<bool> authed{false};
  std::atomic<int> unauth_seen{0};
  std::atomic<int> authed_seen{0};

  SetRoutedHandler([&](const mir2::common::RoutedMessageData& routed) {
    if (routed.msg_id == static_cast<uint16_t>(MsgId::kLoginReq)) {
      authed.store(true, std::memory_order_release);
      return;
    }
    if (!authed.load(std::memory_order_acquire)) {
      unauth_seen.fetch_add(1, std::memory_order_relaxed);
    } else {
      authed_seen.fetch_add(1, std::memory_order_relaxed);
    }
  });

  // 未登录直接发送MoveReq，Logic侧应收到并标记未认证。
  client_->send(static_cast<uint16_t>(MsgId::kMoveReq), std::vector<uint8_t>{9});
  ASSERT_TRUE(WaitForCondition([&]() { return unauth_seen.load() > 0; }, 3s));

  // 发送登录请求后，再发送MoveReq应被视为已认证。
  client_->send(static_cast<uint16_t>(MsgId::kLoginReq), std::vector<uint8_t>{1});
  ASSERT_TRUE(WaitForCondition([&]() { return authed.load(); }, 3s));

  client_->send(static_cast<uint16_t>(MsgId::kMoveReq), std::vector<uint8_t>{2});
  ASSERT_TRUE(WaitForCondition([&]() { return authed_seen.load() > 0; }, 3s));

  EXPECT_EQ(unauth_seen.load(), 1);
  EXPECT_EQ(authed_seen.load(), 1);
}

// 验证5000并发连接场景下，转发耗时仍在可接受范围内。
TEST_F(GatewayLogicUniversalForwardTest, PerformanceUnder5000Connections) {
  ASSERT_TRUE(WaitForLogicConnected(3s));

  constexpr int kConnectionCount = 5000;
  constexpr int kThreadCount = 8;

  std::atomic<int> forwarded_count{0};
  SetRoutedHandler([&forwarded_count](const mir2::common::RoutedMessageData&) {
    forwarded_count.fetch_add(1, std::memory_order_relaxed);
  });

  asio::io_context io_context;
  std::vector<std::shared_ptr<TcpSession>> sessions;
  sessions.reserve(kConnectionCount);
  for (int i = 0; i < kConnectionCount; ++i) {
    auto session = CreateMockSession(io_context, 900000 + i);
    sessions.push_back(session);
    gateway_->RegisterConnection(900000 + i, session);
  }

  const std::vector<uint8_t> payload{1, 2};
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
      [&]() { return forwarded_count.load() >= kConnectionCount; },
      5s));
  EXPECT_EQ(forwarded_count.load(), kConnectionCount);
}

}  // namespace
