#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <utility>

#include <asio/io_context.hpp>
#include <asio/ip/udp.hpp>

#include "common/enums.h"
#include "common/network/i_channel.h"
#include "common/time_utils.h"
#include "network/dual_channel_manager.h"
#include "network/packet_codec.h"
#include "network/tcp_connection.h"
#include "network/tcp_session.h"
#include "mocks/mock_socket.h"

namespace mir2::network {

namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

using KcpToken = std::array<uint8_t, KcpSession::kTokenSize>;

class MockNetworkManager : public INetworkManager {
 public:
  MOCK_METHOD(bool, Start, (const std::string& bind_ip, uint16_t port, int max_connections),
              (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(void, RegisterHandler, (uint16_t msg_id, MessageHandler handler), (override));
  MOCK_METHOD(void, Send,
              (uint64_t connection_id, uint16_t msg_id, const std::vector<uint8_t>& payload),
              (override));
  MOCK_METHOD(std::shared_ptr<TcpSession>, GetSession, (uint64_t session_id),
              (const, override));
  MOCK_METHOD(std::vector<std::shared_ptr<TcpSession>>, GetAllSessions, (), (const, override));
  MOCK_METHOD(size_t, GetConnectionCount, (), (const, override));
  MOCK_METHOD(void, Tick, (), (override));
};

class MockKcpServer : public IKcpServer {
 public:
  MOCK_METHOD(bool, Start, (const std::string& bind_ip, uint16_t port), (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(bool, IsRunning, (), (const, override));
  MOCK_METHOD(uint32_t, AllocateConvId, (), (override));
  MOCK_METHOD(std::shared_ptr<KcpSession>, CreateSession,
              (uint32_t conv_id, const KcpToken& token),
              (override));
  MOCK_METHOD(bool, AddSession, (const std::shared_ptr<KcpSession>& session), (override));
  MOCK_METHOD(void, RemoveSession, (uint32_t conv_id), (override));
  MOCK_METHOD(std::shared_ptr<KcpSession>, GetSession, (uint32_t conv_id), (const, override));
  MOCK_METHOD(void, SetMessageHandler, (KcpSession::MessageHandler handler), (override));
};

std::shared_ptr<TcpSession> MakeTcpSession(asio::io_context& io_context, uint64_t session_id) {
  auto socket = std::make_unique<MockSocket>(io_context.get_executor());
  auto connection = std::make_shared<TcpConnection>(std::move(socket), session_id);
  return std::make_shared<TcpSession>(connection);
}

std::shared_ptr<KcpSession> MakeKcpSession(uint32_t conv_id) {
  KcpToken token{};
  return std::make_shared<KcpSession>(conv_id, token);
}

class DualChannelManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto mock_network = std::make_unique<NiceMock<MockNetworkManager>>();
    auto mock_kcp = std::make_unique<NiceMock<MockKcpServer>>();
    mock_network_ptr_ = mock_network.get();
    mock_kcp_ptr_ = mock_kcp.get();

    EXPECT_CALL(*mock_kcp_ptr_, SetMessageHandler(_))
        .WillOnce(Invoke([this](KcpSession::MessageHandler handler) {
          kcp_handler_ = std::move(handler);
        }));

    manager_ = std::make_unique<DualChannelManager>(
        io_context_, std::move(mock_network), std::move(mock_kcp));
  }

  asio::io_context io_context_;
  MockNetworkManager* mock_network_ptr_ = nullptr;
  MockKcpServer* mock_kcp_ptr_ = nullptr;
  KcpSession::MessageHandler kcp_handler_;
  std::unique_ptr<DualChannelManager> manager_;
};

TEST_F(DualChannelManagerTest, SendRoutesToKcpWhenAvailable) {
  const uint64_t session_id = 1001;
  const uint16_t msg_id = 5500;
  const std::vector<uint8_t> payload{1, 2, 3};

  auto tcp_session = MakeTcpSession(io_context_, session_id);
  auto kcp_session = MakeKcpSession(42);
  kcp_session->SetRemoteEndpoint(
      asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 9000));

  bool output_called = false;
  kcp_session->SetOutputHandler(
      [&output_called](const asio::ip::udp::endpoint&, const uint8_t*, size_t) {
        output_called = true;
      });

  EXPECT_CALL(*mock_network_ptr_, GetSession(session_id))
      .WillRepeatedly(Return(tcp_session));
  EXPECT_CALL(*mock_kcp_ptr_, AddSession(kcp_session)).WillOnce(Return(true));
  EXPECT_CALL(*mock_network_ptr_, Send(_, _, _)).Times(0);

  ASSERT_TRUE(manager_->BindKcpSession(session_id, kcp_session));
  manager_->SetRoute(msg_id, mir2::common::ChannelType::kKcp);

  manager_->Send(session_id, msg_id, payload);
  kcp_session->Update(static_cast<uint32_t>(mir2::common::now_ms()));

  EXPECT_TRUE(output_called);
}

TEST_F(DualChannelManagerTest, SendFallsBackToTcpWhenKcpUnavailable) {
  const uint64_t session_id = 2002;
  const uint16_t msg_id = 5600;
  const std::vector<uint8_t> payload{4, 5};

  manager_->SetRoute(msg_id, mir2::common::ChannelType::kKcp);

  EXPECT_CALL(*mock_network_ptr_, Send(session_id, msg_id, payload)).Times(1);

  manager_->Send(session_id, msg_id, payload);
}

TEST_F(DualChannelManagerTest, BindAndUnbindKcpSessionTracksAndRemoves) {
  const uint64_t session_id = 3003;
  auto tcp_session = MakeTcpSession(io_context_, session_id);
  auto kcp_session = MakeKcpSession(77);

  EXPECT_CALL(*mock_network_ptr_, GetSession(session_id))
      .WillRepeatedly(Return(tcp_session));
  EXPECT_CALL(*mock_kcp_ptr_, AddSession(kcp_session)).WillOnce(Return(true));

  ASSERT_TRUE(manager_->BindKcpSession(session_id, kcp_session));
  EXPECT_EQ(manager_->GetKcpSession(session_id), kcp_session);
  EXPECT_EQ(kcp_session->GetTcpSession(), tcp_session);

  EXPECT_CALL(*mock_kcp_ptr_, RemoveSession(kcp_session->GetConvId())).Times(1);
  manager_->UnbindKcpSession(session_id);
  EXPECT_EQ(manager_->GetKcpSession(session_id), nullptr);
}

TEST_F(DualChannelManagerTest, BroadcastSendsToAllSessions) {
  const uint16_t msg_id = static_cast<uint16_t>(mir2::common::MsgId::kLoginReq);
  const std::vector<uint8_t> payload{9};

  auto session_a = MakeTcpSession(io_context_, 1);
  auto session_b = MakeTcpSession(io_context_, 2);
  std::vector<std::shared_ptr<TcpSession>> sessions{session_a, session_b};

  EXPECT_CALL(*mock_network_ptr_, GetAllSessions()).WillOnce(Return(sessions));
  EXPECT_CALL(*mock_network_ptr_, Send(1, msg_id, payload)).Times(1);
  EXPECT_CALL(*mock_network_ptr_, Send(2, msg_id, payload)).Times(1);

  manager_->Broadcast(msg_id, payload);
}

TEST_F(DualChannelManagerTest, BroadcastIfRespectsFilter) {
  const uint16_t msg_id = static_cast<uint16_t>(mir2::common::MsgId::kLoginReq);
  const std::vector<uint8_t> payload{7, 8};

  auto session_a = MakeTcpSession(io_context_, 10);
  auto session_b = MakeTcpSession(io_context_, 11);
  auto session_c = MakeTcpSession(io_context_, 12);
  std::vector<std::shared_ptr<TcpSession>> sessions{session_a, session_b, session_c};

  EXPECT_CALL(*mock_network_ptr_, GetAllSessions()).WillOnce(Return(sessions));
  EXPECT_CALL(*mock_network_ptr_, Send(11, msg_id, payload)).Times(1);
  EXPECT_CALL(*mock_network_ptr_, Send(10, msg_id, payload)).Times(0);
  EXPECT_CALL(*mock_network_ptr_, Send(12, msg_id, payload)).Times(0);

  manager_->BroadcastIf(msg_id, payload,
                        [](const std::shared_ptr<TcpSession>& session) {
                          return session->GetSessionId() == 11;
                        });
}

TEST_F(DualChannelManagerTest, TickCleansUpKcpWhenTcpMissing) {
  const uint64_t session_id = 4004;
  auto tcp_session = MakeTcpSession(io_context_, session_id);
  auto kcp_session = MakeKcpSession(88);

  EXPECT_CALL(*mock_network_ptr_, GetSession(session_id))
      .WillOnce(Return(tcp_session))
      .WillRepeatedly(Return(nullptr));
  EXPECT_CALL(*mock_kcp_ptr_, AddSession(kcp_session)).WillOnce(Return(true));
  EXPECT_CALL(*mock_kcp_ptr_, RemoveSession(kcp_session->GetConvId())).Times(1);
  EXPECT_CALL(*mock_network_ptr_, Tick()).Times(1);

  ASSERT_TRUE(manager_->BindKcpSession(session_id, kcp_session));
  manager_->Tick();

  EXPECT_EQ(manager_->GetKcpSession(session_id), nullptr);
}

TEST_F(DualChannelManagerTest, KcpHeartbeatSendsAck) {
  ASSERT_TRUE(static_cast<bool>(kcp_handler_));

  auto kcp_session = MakeKcpSession(123);
  kcp_session->SetRemoteEndpoint(
      asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 7777));

  bool output_called = false;
  kcp_session->SetOutputHandler(
      [&output_called](const asio::ip::udp::endpoint&, const uint8_t*, size_t) {
        output_called = true;
      });

  Packet packet{};
  packet.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kKcpHeartbeat);
  packet.payload.clear();

  kcp_handler_(kcp_session, packet);
  kcp_session->Update(static_cast<uint32_t>(mir2::common::now_ms()));

  EXPECT_TRUE(output_called);
}

TEST_F(DualChannelManagerTest, KcpHeartbeatAckDispatchesToRegisteredHandler) {
  ASSERT_TRUE(static_cast<bool>(kcp_handler_));

  const uint16_t msg_id = static_cast<uint16_t>(mir2::common::MsgId::kKcpHeartbeatAck);
  EXPECT_CALL(*mock_network_ptr_, RegisterHandler(msg_id, _)).Times(1);

  bool called = false;
  auto tcp_session = MakeTcpSession(io_context_, 5005);

  manager_->RegisterHandler(msg_id,
                            [&called, &tcp_session](const std::shared_ptr<TcpSession>& session,
                                                    const std::vector<uint8_t>& payload) {
                              called = true;
                              EXPECT_EQ(session, tcp_session);
                              EXPECT_EQ(payload, std::vector<uint8_t>({0xAA, 0xBB}));
                            });

  auto kcp_session = MakeKcpSession(321);
  kcp_session->BindTcpSession(tcp_session);

  Packet packet{};
  packet.msg_id = msg_id;
  packet.payload = {0xAA, 0xBB};

  kcp_handler_(kcp_session, packet);

  EXPECT_TRUE(called);
}

}  // namespace

}  // namespace mir2::network
