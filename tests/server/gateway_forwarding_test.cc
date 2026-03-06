#include <gtest/gtest.h>

#include <asio/io_context.hpp>
#include <flatbuffers/flatbuffers.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/enums.h"
#include "common/internal_message_helper.h"
#include "common/protocol/message_codec.h"
#include "game_generated.h"
#include "guild_generated.h"
#include "network/message_dispatcher.h"
#include "network/packet_codec.h"
#include "network/tcp_connection.h"
#include "network/tcp_session.h"
#include "mocks/mock_socket.h"

#define private public
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

std::vector<uint8_t> BuildLoginReqPayload(const std::string& username) {
  common::LoginRequest request;
  request.username = username;
  request.password = "pw";
  request.version = "1.0";

  common::MessageCodecStatus status = common::MessageCodecStatus::kOk;
  auto payload = common::EncodeLoginRequest(request, &status);
  if (status != common::MessageCodecStatus::kOk) {
    return {};
  }
  return payload;
}

}  // namespace

// 验证所有注册消息在未认证状态下也能被无脑转发。
TEST(GatewayForwardingTest, ForwardAllMessagesRegardlessOfAuthState) {
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

  auto bundle = CreateSession(io_context, 101);
  bundle.session->SetAuthState(network::TcpSession::AuthState::kPending);
  test_manager_ptr->AddSession(bundle.session);
  server.RegisterConnection(101, bundle.session);

  const std::vector<uint16_t> forward_messages = {
      static_cast<uint16_t>(common::MsgId::kLoginReq),
      static_cast<uint16_t>(common::MsgId::kLogout),
      static_cast<uint16_t>(common::MsgId::kCreateRoleReq),
      static_cast<uint16_t>(common::MsgId::kSelectRoleReq),
      static_cast<uint16_t>(common::MsgId::kRoleListReq),
      static_cast<uint16_t>(common::MsgId::kMoveReq),
      static_cast<uint16_t>(common::MsgId::kAttackReq),
      static_cast<uint16_t>(common::MsgId::kSkillReq),
      static_cast<uint16_t>(common::MsgId::kChatReq),
      static_cast<uint16_t>(common::MsgId::kUseItemReq),
      static_cast<uint16_t>(common::MsgId::kDropItemReq),
      static_cast<uint16_t>(common::MsgId::kPickupItemReq),
      static_cast<uint16_t>(common::MsgId::kEquipReq),
      static_cast<uint16_t>(common::MsgId::kUnequipReq),
      static_cast<uint16_t>(common::MsgId::kNpcInteractReq),
      static_cast<uint16_t>(common::MsgId::kNpcMenuSelect),
      static_cast<uint16_t>(common::MsgId::kGuildChat),
      static_cast<uint16_t>(common::MsgId::kGuildCreateReq),
      static_cast<uint16_t>(common::MsgId::kGuildJoinReq),
      static_cast<uint16_t>(common::MsgId::kGuildLeaveReq),
      static_cast<uint16_t>(common::MsgId::kGuildKickReq),
      static_cast<uint16_t>(common::MsgId::kGuildDeclareWarReq),
      static_cast<uint16_t>(common::MsgId::kGuildCancelWarReq),
      static_cast<uint16_t>(common::MsgId::kGuildMakeAllyReq),
      static_cast<uint16_t>(common::MsgId::kGuildBreakAllyReq),
      static_cast<uint16_t>(common::MsgId::kGuildUpdateNoticeReq),
      static_cast<uint16_t>(common::MsgId::kGuildUpdateRankReq),
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
      static_cast<uint16_t>(common::MsgId::kTradeReq),
      static_cast<uint16_t>(common::MsgId::kTradeAddItemReq),
      static_cast<uint16_t>(common::MsgId::kTradeSetGoldReq),
      static_cast<uint16_t>(common::MsgId::kTradeConfirmReq),
      static_cast<uint16_t>(common::MsgId::kTradeCancelReq),
      static_cast<uint16_t>(common::MsgId::kPartyInviteReq),
      static_cast<uint16_t>(common::MsgId::kPartyJoinReq),
      static_cast<uint16_t>(common::MsgId::kPartyLeaveReq),
      static_cast<uint16_t>(common::MsgId::kPartyKickReq),
  };

  const std::vector<uint8_t> payload{9, 8, 7};
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
    EXPECT_EQ(routed.client_id, 101u);
    EXPECT_EQ(routed.payload, payload);
    forwarded_ids.insert(routed.msg_id);
  }

  EXPECT_EQ(forwarded_ids.size(), forward_messages.size());
  for (auto msg_id : forward_messages) {
    EXPECT_TRUE(forwarded_ids.count(msg_id) > 0);
  }
}

TEST(GatewayForwardingTest, ForwardMessageNoAuthRequired_AlwaysAllowed) {
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

  auto session = CreateSession(io_context, 102).session;
  session->SetAuthState(network::TcpSession::AuthState::kUnknown);

  test_manager_ptr->Dispatch(session,
                             static_cast<uint16_t>(common::MsgId::kLoginReq),
                             std::vector<uint8_t>{});
  DrainIoContext(io_context);

  EXPECT_EQ(logic_client.socket->GetWrites().size(), 1u);
}

TEST(GatewayForwardingTest, LoginRateLimitedPerIp) {
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

  auto bundle = CreateSession(io_context, 106);
  test_manager_ptr->AddSession(bundle.session);
  server.RegisterConnection(106, bundle.session);

  const auto payload = BuildLoginReqPayload("ip_limit_user");
  ASSERT_FALSE(payload.empty());

  for (int i = 0; i < 6; ++i) {
    test_manager_ptr->Dispatch(bundle.session,
                               static_cast<uint16_t>(common::MsgId::kLoginReq),
                               payload);
  }
  DrainIoContext(io_context);

  EXPECT_EQ(logic_socket->GetWrites().size(), 5u);
  EXPECT_GE(bundle.socket->GetWrites().size(), 1u);
}

TEST(GatewayForwardingTest, ForwardToUnknownMessageId_DropsMessage) {
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

  auto session = CreateSession(io_context, 103).session;
  session->SetAuthState(network::TcpSession::AuthState::kAuthed);

  test_manager_ptr->Dispatch(session, 9999, std::vector<uint8_t>{1});
  DrainIoContext(io_context);

  EXPECT_TRUE(logic_client.socket->GetWrites().empty());
}

TEST(GatewayForwardingTest, ForwardWithInvalidMsgId_DropsMessage) {
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

  auto session = CreateSession(io_context, 105).session;
  session->SetAuthState(network::TcpSession::AuthState::kAuthed);

  test_manager_ptr->Dispatch(session, 0, std::vector<uint8_t>{});
  DrainIoContext(io_context);

  EXPECT_TRUE(logic_client.socket->GetWrites().empty());
}

TEST(GatewayForwardingTest, IsLogicConnectedReturnsCorrectState) {
  asio::io_context io_context;
  GatewayServer server;

  auto logic_client = CreateMockClient(io_context);
  server.logic_client_ = std::move(logic_client.client);
  EXPECT_TRUE(server.IsLogicConnected());

  server.logic_client_->connected_.store(false);
  EXPECT_FALSE(server.IsLogicConnected());
}

TEST(GatewayForwardingTest, GetLogicClientReturnsCorrectClient) {
  asio::io_context io_context;
  GatewayServer server;

  auto logic_client = CreateMockClient(io_context);
  auto* raw_ptr = logic_client.client.get();
  server.logic_client_ = std::move(logic_client.client);

  EXPECT_EQ(server.GetLogicClient(), raw_ptr);
}

TEST(GatewayForwardingTest, OversizedPayloadKicksAndDoesNotForward) {
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

  auto bundle = CreateSession(io_context, 8001);
  test_manager_ptr->AddSession(bundle.session);
  server.RegisterConnection(8001, bundle.session);

  std::vector<uint8_t> payload(65 * 1024, 0x5A);
  test_manager_ptr->Dispatch(bundle.session,
                             static_cast<uint16_t>(common::MsgId::kAttackReq),
                             payload);
  DrainIoContext(io_context);

  EXPECT_TRUE(logic_socket->GetWrites().empty());
  const auto& client_writes = bundle.socket->GetWrites();
  ASSERT_FALSE(client_writes.empty());

  network::Packet packet{};
  ASSERT_TRUE(DecodeSinglePacket(client_writes.front(), &packet));
  EXPECT_EQ(packet.msg_id, static_cast<uint16_t>(common::MsgId::kKick));
}

TEST(GatewayForwardingTest, KcpMessagesDropDuringHoldingInsteadOfBuffering) {
  asio::io_context io_context;
  GatewayServer server;

  auto bundle = CreateSession(io_context, 8101);
  server.RegisterConnection(8101, bundle.session);

  const std::vector<uint8_t> payload{1, 2, 3, 4};
  server.HandleForwardMessage(bundle.session,
                              static_cast<uint16_t>(common::MsgId::kAttackReq),
                              common::ChannelType::kKcp,
                              payload);

  std::shared_lock<std::shared_mutex> lock(server.holder_lock_);
  ASSERT_EQ(server.holder_state_, ConnectionHolder::State::HOLDING);
  auto holder_it = server.connection_holders_.find(8101);
  ASSERT_NE(holder_it, server.connection_holders_.end());
  ASSERT_NE(holder_it->second, nullptr);
  EXPECT_FALSE(holder_it->second->HasBufferedMessages());
}

TEST(GatewayForwardingTest, DisconnectNotificationQueuedAndReplayedAfterReconnect) {
  asio::io_context io_context;
  GatewayServer server;

  auto logic_client = CreateMockClient(io_context);
  auto* logic_socket = logic_client.socket;
  server.logic_client_ = std::move(logic_client.client);
  server.logic_client_->connected_.store(false);

  auto bundle = CreateSession(io_context, 8201);
  server.RegisterConnection(8201, bundle.session);
  server.UnregisterSession(bundle.session);

  EXPECT_EQ(logic_socket->GetWrites().size(), 0u);
  {
    std::shared_lock<std::shared_mutex> lock(server.disconnect_queue_lock_);
    EXPECT_EQ(server.pending_disconnect_events_.size(), 1u);
  }

  server.logic_client_->connected_.store(true);
  server.ProcessDisconnectRetryQueue(network::TcpSession::NowMs());
  DrainIoContext(io_context);

  {
    std::shared_lock<std::shared_mutex> lock(server.disconnect_queue_lock_);
    EXPECT_TRUE(server.pending_disconnect_events_.empty());
  }
  ASSERT_EQ(logic_socket->GetWrites().size(), 1u);

  network::Packet packet{};
  ASSERT_TRUE(DecodeSinglePacket(logic_socket->GetWrites().front(), &packet));
  EXPECT_EQ(packet.msg_id, static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage));

  common::RoutedMessageData routed;
  ASSERT_TRUE(common::ParseRoutedMessage(packet.payload, &routed));
  EXPECT_EQ(routed.client_id, 8201u);
  EXPECT_EQ(routed.msg_id, static_cast<uint16_t>(common::MsgId::kLogout));
  EXPECT_TRUE(routed.payload.empty());
}

TEST(GatewayForwardingTest, EnterGameResponseUpdatesContextOnlyOnSuccess) {
  asio::io_context io_context;
  GatewayServer server;

  auto test_manager = std::make_unique<TestNetworkManager>(io_context);
  auto* test_manager_ptr = test_manager.get();
  auto kcp_server = std::make_unique<NullKcpServer>();
  server.network_ = std::make_unique<network::DualChannelManager>(
      io_context, std::move(test_manager), std::move(kcp_server));

  auto bundle = CreateSession(io_context, 8301);
  test_manager_ptr->AddSession(bundle.session);
  server.RegisterConnection(8301, bundle.session);

  flatbuffers::FlatBufferBuilder fail_builder;
  auto fail_player_name = fail_builder.CreateString("ignored_player");
  auto fail_player = mir2::proto::CreatePlayerInfo(
      fail_builder, 9876, fail_player_name, mir2::proto::Profession::WARRIOR,
      1, 100, 100, 50, 50, 1, 10, 10, 0);
  auto fail_rsp = mir2::proto::CreateEnterGameRsp(
      fail_builder, mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND, fail_player);
  fail_builder.Finish(fail_rsp);
  const uint8_t* fail_data = fail_builder.GetBufferPointer();
  std::vector<uint8_t> fail_payload(fail_data, fail_data + fail_builder.GetSize());

  network::Packet fail_packet{};
  fail_packet.msg_id = static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage);
  fail_packet.payload = common::BuildRoutedMessage(
      8301,
      static_cast<uint16_t>(common::MsgId::kEnterGameRsp),
      fail_payload);
  server.OnLogicPacket(fail_packet);
  EXPECT_EQ(bundle.session->GetUserId(), 0u);

  flatbuffers::FlatBufferBuilder ok_builder;
  auto ok_player_name = ok_builder.CreateString("ok_player");
  auto ok_player = mir2::proto::CreatePlayerInfo(
      ok_builder, 12345, ok_player_name, mir2::proto::Profession::WARRIOR,
      1, 100, 100, 50, 50, 1, 11, 12, 0);
  auto ok_rsp = mir2::proto::CreateEnterGameRsp(
      ok_builder, mir2::proto::ErrorCode::ERR_OK, ok_player);
  ok_builder.Finish(ok_rsp);
  const uint8_t* ok_data = ok_builder.GetBufferPointer();
  std::vector<uint8_t> ok_payload(ok_data, ok_data + ok_builder.GetSize());

  network::Packet ok_packet{};
  ok_packet.msg_id = static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage);
  ok_packet.payload = common::BuildRoutedMessage(
      8301,
      static_cast<uint16_t>(common::MsgId::kEnterGameRsp),
      ok_payload);
  server.OnLogicPacket(ok_packet);
  EXPECT_EQ(bundle.session->GetUserId(), 12345u);
}

}  // namespace mir2::gateway
