#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/enums.h"
#include "common/internal_message_helper.h"
#include "common/protocol/universal_forward_msg_ids.h"
#include "network/message_dispatcher.h"
#include "network/packet_codec.h"
#include "network/tcp_connection.h"
#include "network/tcp_session.h"
#include "system_generated.h"
#include "mocks/mock_socket.h"

#define private public
#include "gateway/connection_holder.h"
#include "gateway/gateway_server.h"
#include "network/dual_channel_manager.h"
#include "network/kcp_server.h"
#include "network/tcp_client.h"
#undef private

namespace mir2::gateway {

namespace {

class NullKcpServer : public network::IKcpServer {
 public:
  bool Start(const std::string&, uint16_t) override { return false; }
  void Stop() override {}
  bool IsRunning() const override { return false; }
  uint32_t AllocateConvId() override { return 0; }
  std::shared_ptr<network::KcpSession> CreateSession(
      uint32_t,
      const std::array<uint8_t, network::KcpSession::kTokenSize>&) override {
    return nullptr;
  }
  bool AddSession(const std::shared_ptr<network::KcpSession>&) override { return false; }
  void RemoveSession(uint32_t) override {}
  std::shared_ptr<network::KcpSession> GetSession(uint32_t) const override { return nullptr; }
  void SetMessageHandler(network::KcpSession::MessageHandler handler) override {
    handler_ = std::move(handler);
  }

 private:
  network::KcpSession::MessageHandler handler_;
};

class TestNetworkManager : public network::INetworkManager {
 public:
  explicit TestNetworkManager(asio::io_context& io_context)
      : io_context_(io_context) {}

  bool Start(const std::string&, uint16_t, int) override { return true; }
  void Stop() override {}

  void RegisterHandler(uint16_t msg_id, network::MessageHandler handler) override {
    dispatcher_.RegisterHandler(msg_id, std::move(handler));
  }

  void Send(uint64_t connection_id,
            uint16_t msg_id,
            const std::vector<uint8_t>& payload) override {
    auto session = GetSession(connection_id);
    if (session) {
      session->Send(msg_id, payload);
    }
  }

  std::shared_ptr<network::TcpSession> GetSession(uint64_t session_id) const override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      return it->second;
    }
    return nullptr;
  }

  std::vector<std::shared_ptr<network::TcpSession>> GetAllSessions() const override {
    std::vector<std::shared_ptr<network::TcpSession>> result;
    std::lock_guard<std::mutex> lock(mutex_);
    result.reserve(sessions_.size());
    for (const auto& [_, session] : sessions_) {
      if (session) {
        result.push_back(session);
      }
    }
    return result;
  }

  size_t GetConnectionCount() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
  }

  void Tick() override {}

  void AddSession(const std::shared_ptr<network::TcpSession>& session) {
    if (!session) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[session->GetSessionId()] = session;
  }

  void Dispatch(const std::shared_ptr<network::TcpSession>& session,
                uint16_t msg_id,
                const std::vector<uint8_t>& payload) {
    dispatcher_.Dispatch(session, msg_id, payload);
  }

 private:
  asio::io_context& io_context_;
  mutable std::mutex mutex_;
  std::unordered_map<uint64_t, std::shared_ptr<network::TcpSession>> sessions_;
  network::MessageDispatcher dispatcher_;
};

struct SessionBundle {
  std::shared_ptr<network::TcpSession> session;
  network::MockSocket* socket = nullptr;
};

SessionBundle CreateSession(asio::io_context& io_context, uint64_t connection_id) {
  auto mock_socket = std::make_unique<network::MockSocket>(io_context.get_executor());
  auto* socket_ptr = mock_socket.get();
  auto connection = std::make_shared<network::TcpConnection>(std::move(mock_socket), connection_id);
  auto session = std::make_shared<network::TcpSession>(connection);
  session->Start();
  return {session, socket_ptr};
}

struct ClientBundle {
  std::unique_ptr<network::TcpClient> client;
  network::MockSocket* socket = nullptr;
};

ClientBundle CreateMockClient(asio::io_context& io_context) {
  auto client = std::make_unique<network::TcpClient>(io_context);
  auto mock_socket = std::make_unique<network::MockSocket>(io_context.get_executor());
  auto* socket_ptr = mock_socket.get();
  auto connection = std::make_shared<network::TcpConnection>(std::move(mock_socket), 1);
  client->connection_ = connection;
  client->connected_.store(true);
  return {std::move(client), socket_ptr};
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

std::vector<uint8_t> BuildHeartbeatPayload(uint32_t seq) {
  flatbuffers::FlatBufferBuilder builder;
  const auto heartbeat = mir2::proto::CreateHeartbeat(
      builder, seq, static_cast<uint32_t>(network::TcpSession::NowMs()));
  builder.Finish(heartbeat);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

template <typename T, typename = void>
struct HasUserRouteTable : std::false_type {};

template <typename T>
struct HasUserRouteTable<T, std::void_t<decltype(&T::user_route_table_)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasSessionMap : std::false_type {};

template <typename T>
struct HasSessionMap<T, std::void_t<decltype(&T::session_map_)>> : std::true_type {};

template <typename T, typename = void>
struct HasRegisterUser : std::false_type {};

template <typename T>
struct HasRegisterUser<T, std::void_t<decltype(&T::RegisterUser)>> : std::true_type {};

}  // namespace

// 验证扩展消息矩阵在未认证状态下仍会被转发到LogicServer。
TEST(GatewayUniversalForwardTest, ForwardAllMessagesWithoutAuthCheck) {
  asio::io_context io_context;
  GatewayServer server;

  auto test_manager = std::make_unique<TestNetworkManager>(io_context);
  auto* test_manager_ptr = test_manager.get();
  auto kcp_server = std::make_unique<NullKcpServer>();
  server.network_ = std::make_unique<network::DualChannelManager>(
      io_context, std::move(test_manager), std::move(kcp_server));

  auto logic_client = CreateMockClient(io_context);
  auto* logic_socket = logic_client.socket;
  server.logic_client_ = std::move(logic_client.client);
  server.RegisterHandlers();

  auto bundle = CreateSession(io_context, 1101);
  bundle.session->SetAuthState(network::TcpSession::AuthState::kPending);
  test_manager_ptr->AddSession(bundle.session);
  server.RegisterConnection(1101, bundle.session);

  const auto& forward_messages = common::protocol::kUniversalForwardMsgIds;

  const std::vector<uint8_t> payload{1, 2, 3};
  for (auto msg_id : forward_messages) {
    test_manager_ptr->Dispatch(bundle.session, msg_id, payload);
  }
  DrainIoContext(io_context);

  const auto& writes = logic_socket->GetWrites();
  ASSERT_EQ(writes.size(), forward_messages.size());

  std::unordered_set<uint16_t> forwarded_ids;
  for (const auto& bytes : writes) {
    network::Packet packet{};
    ASSERT_TRUE(DecodeSinglePacket(bytes, &packet));
    EXPECT_EQ(packet.msg_id, static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage));

    common::RoutedMessageData routed;
    ASSERT_TRUE(common::ParseRoutedMessage(packet.payload, &routed));
    EXPECT_EQ(routed.client_id, 1101u);
    forwarded_ids.insert(routed.msg_id);
  }

  EXPECT_EQ(forwarded_ids.size(), forward_messages.size());
  for (auto msg_id : forward_messages) {
    EXPECT_TRUE(forwarded_ids.count(msg_id) > 0);
  }
}

// 验证心跳消息由Gateway本地响应，不会转发到LogicServer。
TEST(GatewayUniversalForwardTest, HeartbeatHandledLocally) {
  asio::io_context io_context;
  GatewayServer server;

  auto test_manager = std::make_unique<TestNetworkManager>(io_context);
  auto* test_manager_ptr = test_manager.get();
  auto kcp_server = std::make_unique<NullKcpServer>();
  server.network_ = std::make_unique<network::DualChannelManager>(
      io_context, std::move(test_manager), std::move(kcp_server));

  auto logic_client = CreateMockClient(io_context);
  auto* logic_socket = logic_client.socket;
  server.logic_client_ = std::move(logic_client.client);
  server.RegisterHandlers();

  auto bundle = CreateSession(io_context, 1201);
  test_manager_ptr->AddSession(bundle.session);

  const auto payload = BuildHeartbeatPayload(42);
  test_manager_ptr->Dispatch(bundle.session,
                             static_cast<uint16_t>(common::MsgId::kHeartbeat),
                             payload);
  DrainIoContext(io_context);

  EXPECT_TRUE(logic_socket->GetWrites().empty());

  const auto& client_writes = bundle.socket->GetWrites();
  ASSERT_EQ(client_writes.size(), 1u);
  network::Packet packet{};
  ASSERT_TRUE(DecodeSinglePacket(client_writes.front(), &packet));
  EXPECT_EQ(packet.msg_id, static_cast<uint16_t>(common::MsgId::kHeartbeatRsp));
}

// 验证Logic下发背压控制后，Gateway会暂停并在到期后恢复客户端读。
TEST(GatewayUniversalForwardTest, LogicBackpressureControlPausesAndResumesRead) {
  asio::io_context io_context;
  GatewayServer server;

  auto test_manager = std::make_unique<TestNetworkManager>(io_context);
  auto* test_manager_ptr = test_manager.get();
  auto kcp_server = std::make_unique<NullKcpServer>();
  server.network_ = std::make_unique<network::DualChannelManager>(
      io_context, std::move(test_manager), std::move(kcp_server));

  auto logic_client = CreateMockClient(io_context);
  server.logic_client_ = std::move(logic_client.client);
  server.RegisterHandlers();

  auto bundle = CreateSession(io_context, 1251);
  test_manager_ptr->AddSession(bundle.session);
  server.RegisterConnection(1251, bundle.session);

  flatbuffers::FlatBufferBuilder builder;
  const auto control = mir2::proto::CreateBackpressureControl(
      builder,
      1251,
      mir2::proto::BackpressureAction::PAUSE_READ,
      2);
  builder.Finish(control);
  const uint8_t* data = builder.GetBufferPointer();
  network::Packet packet{};
  packet.msg_id = static_cast<uint16_t>(common::InternalMsgId::kBackpressureControl);
  packet.payload.assign(data, data + builder.GetSize());

  server.OnLogicPacket(packet);
  DrainIoContext(io_context);
  EXPECT_TRUE(bundle.session->IsReadPaused());

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  server.ResumeBackpressuredSessions(network::TcpSession::NowMs());
  DrainIoContext(io_context);
  EXPECT_FALSE(bundle.session->IsReadPaused());
}

// 验证未注册消息不会被转发。
TEST(GatewayUniversalForwardTest, UnregisteredMessageDropped) {
  asio::io_context io_context;
  GatewayServer server;

  auto test_manager = std::make_unique<TestNetworkManager>(io_context);
  auto* test_manager_ptr = test_manager.get();
  auto kcp_server = std::make_unique<NullKcpServer>();
  server.network_ = std::make_unique<network::DualChannelManager>(
      io_context, std::move(test_manager), std::move(kcp_server));

  auto logic_client = CreateMockClient(io_context);
  auto* logic_socket = logic_client.socket;
  server.logic_client_ = std::move(logic_client.client);
  server.RegisterHandlers();

  auto bundle = CreateSession(io_context, 1301);
  test_manager_ptr->AddSession(bundle.session);

  test_manager_ptr->Dispatch(bundle.session, 9999, std::vector<uint8_t>{9});
  DrainIoContext(io_context);

  EXPECT_TRUE(logic_socket->GetWrites().empty());
  EXPECT_TRUE(bundle.socket->GetWrites().empty());
}

// 验证Gateway只保留session_map_，并移除了user_route_table_与RegisterUser接口。
TEST(GatewayUniversalForwardTest, SessionMapOnlyNoUserTable) {
  EXPECT_TRUE((HasSessionMap<GatewayServer>::value));
  EXPECT_FALSE((HasUserRouteTable<GatewayServer>::value));
  EXPECT_FALSE((HasRegisterUser<GatewayServer>::value));
}

// 验证session_map_在并发读写下仍然安全。
TEST(GatewayUniversalForwardTest, SessionMapConcurrentAccess) {
  asio::io_context io_context;
  GatewayServer server;
  auto bundle = CreateSession(io_context, 1401);

  std::atomic<bool> stop{false};
  std::thread writer([&]() {
    while (!stop.load()) {
      server.RegisterConnection(1401, bundle.session);
      server.UnregisterSession(bundle.session);
    }
  });
  std::thread reader([&]() {
    while (!stop.load()) {
      server.GetConnectionSession(1401);
      server.GetConnectionCount();
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  stop.store(true);
  writer.join();
  reader.join();

  EXPECT_LE(server.GetConnectionCount(), 1u);
}

// 验证LogicServer断线时，消息被ConnectionHolder缓冲而非直接丢弃。
TEST(GatewayUniversalForwardTest, ConnectionHolderBuffersWhenLogicDisconnected) {
  asio::io_context io_context;
  GatewayServer server;

  auto test_manager = std::make_unique<TestNetworkManager>(io_context);
  auto* test_manager_ptr = test_manager.get();
  auto kcp_server = std::make_unique<NullKcpServer>();
  server.network_ = std::make_unique<network::DualChannelManager>(
      io_context, std::move(test_manager), std::move(kcp_server));

  auto logic_client = CreateMockClient(io_context);
  logic_client.client->connected_.store(false);
  server.logic_client_ = std::move(logic_client.client);
  server.RegisterHandlers();

  auto bundle = CreateSession(io_context, 1501);
  server.RegisterConnection(1501, bundle.session);

  test_manager_ptr->Dispatch(bundle.session,
                             static_cast<uint16_t>(common::MsgId::kAttackReq),
                             std::vector<uint8_t>{7, 7});
  DrainIoContext(io_context);

  auto holder_it = server.connection_holders_.find(1501);
  ASSERT_NE(holder_it, server.connection_holders_.end());
  ASSERT_NE(holder_it->second, nullptr);
  EXPECT_TRUE(holder_it->second->HasBufferedMessages());
  EXPECT_EQ(server.holder_state_, ConnectionHolder::State::HOLDING);
}

// 验证session_id为0的消息直接被丢弃。
TEST(GatewayUniversalForwardTest, HandleForwardMessageValidatesSessionId) {
  asio::io_context io_context;
  GatewayServer server;

  auto test_manager = std::make_unique<TestNetworkManager>(io_context);
  auto* test_manager_ptr = test_manager.get();
  auto kcp_server = std::make_unique<NullKcpServer>();
  server.network_ = std::make_unique<network::DualChannelManager>(
      io_context, std::move(test_manager), std::move(kcp_server));

  auto logic_client = CreateMockClient(io_context);
  auto* logic_socket = logic_client.socket;
  server.logic_client_ = std::move(logic_client.client);
  server.RegisterHandlers();

  auto bundle = CreateSession(io_context, 0);
  test_manager_ptr->Dispatch(bundle.session,
                             static_cast<uint16_t>(common::MsgId::kMoveReq),
                             std::vector<uint8_t>{1});
  DrainIoContext(io_context);

  EXPECT_TRUE(logic_socket->GetWrites().empty());
  EXPECT_TRUE(server.connection_holders_.find(0) == server.connection_holders_.end());
}

// 验证高并发转发时锁竞争可控：1000次并发转发应小于1秒。
TEST(GatewayUniversalForwardTest, PerformanceLockContentionReduced) {
  asio::io_context io_context;
  GatewayServer server;

  auto test_manager = std::make_unique<TestNetworkManager>(io_context);
  auto* test_manager_ptr = test_manager.get();
  auto kcp_server = std::make_unique<NullKcpServer>();
  server.network_ = std::make_unique<network::DualChannelManager>(
      io_context, std::move(test_manager), std::move(kcp_server));

  auto logic_client = CreateMockClient(io_context);
  server.logic_client_ = std::move(logic_client.client);
  server.RegisterHandlers();

  auto bundle = CreateSession(io_context, 1601);
  server.RegisterConnection(1601, bundle.session);
  test_manager_ptr->AddSession(bundle.session);

  constexpr int kThreadCount = 20;
  constexpr int kMessagesPerThread = 50;
  constexpr int kTotalMessages = kThreadCount * kMessagesPerThread;

  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);

  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < kThreadCount; ++i) {
    threads.emplace_back([&]() {
      for (int j = 0; j < kMessagesPerThread; ++j) {
        test_manager_ptr->Dispatch(bundle.session,
                                   static_cast<uint16_t>(common::MsgId::kMoveReq),
                                   std::vector<uint8_t>{1});
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_EQ(kTotalMessages, kThreadCount * kMessagesPerThread);
  EXPECT_LT(elapsed, std::chrono::seconds(1));
  DrainIoContext(io_context);
}

}  // namespace mir2::gateway
