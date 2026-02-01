#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include <cstdint>
#include <vector>

#include "common/enums.h"
#include "common/internal_message_helper.h"
#include "network/packet_codec.h"
#include "network/tcp_connection.h"
#include "network/tcp_session.h"
#include "tests/mocks/mock_socket.h"

#define private public
#include "gateway/gateway_server.h"
#include "network/network_manager.h"
#include "network/tcp_client.h"
#undef private

namespace mir2::gateway {

namespace {

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
  return network::PacketCodec::Decode(bytes.data(), bytes.size(), out_packet) ==
         network::DecodeStatus::kOk;
}

void RegisterSession(network::NetworkManager* manager,
                     const std::shared_ptr<network::TcpSession>& session) {
  if (!manager || !session) {
    return;
  }
  std::lock_guard<std::mutex> lock(manager->mutex_);
  manager->sessions_[session->GetSessionId()] = session;
}

}  // namespace

// End-to-end forwarding: client -> service -> client response.
TEST(GatewayFullFlowTest, ClientConnect_Login_RouteToDb_ReceiveResponse) {
  asio::io_context io_context;
  GatewayServer server;
  server.network_ = std::make_unique<network::NetworkManager>(io_context);

  auto db_client = CreateMockClient(io_context);
  server.db_client_ = std::move(db_client.client);
  server.message_router_.RegisterRoute(static_cast<uint16_t>(common::MsgId::kLoginReq),
                                       common::ServiceType::kDb, false);
  server.RegisterHandlers();

  auto session_bundle = CreateSession(io_context, 1001);
  RegisterSession(server.network_.get(), session_bundle.session);

  const std::vector<uint8_t> login_payload{1, 2, 3};
  server.network_->dispatcher_.Dispatch(session_bundle.session,
                                        static_cast<uint16_t>(common::MsgId::kLoginReq),
                                        login_payload);
  DrainIoContext(io_context);

  const auto& service_writes = db_client.socket->GetWrites();
  ASSERT_EQ(service_writes.size(), 1u);
  network::Packet routed_packet{};
  ASSERT_TRUE(DecodeSinglePacket(service_writes.front(), &routed_packet));
  EXPECT_EQ(routed_packet.msg_id, static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage));

  common::RoutedMessageData routed;
  ASSERT_TRUE(common::ParseRoutedMessage(routed_packet.payload, &routed));
  EXPECT_EQ(routed.client_id, 1001u);
  EXPECT_EQ(routed.msg_id, static_cast<uint16_t>(common::MsgId::kLoginReq));

  const std::vector<uint8_t> login_rsp_payload{9, 9};
  const auto response_payload =
      common::BuildRoutedMessage(1001u, static_cast<uint16_t>(common::MsgId::kLoginRsp),
                                 login_rsp_payload);
  network::Packet service_packet{
      static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage), response_payload};

  server.OnServicePacket(common::ServiceType::kDb, service_packet);
  DrainIoContext(io_context);

  const auto& client_writes = session_bundle.socket->GetWrites();
  ASSERT_EQ(client_writes.size(), 1u);
  network::Packet client_packet{};
  ASSERT_TRUE(DecodeSinglePacket(client_writes.front(), &client_packet));
  EXPECT_EQ(client_packet.msg_id, static_cast<uint16_t>(common::MsgId::kLoginRsp));
  EXPECT_EQ(client_packet.payload, login_rsp_payload);
}

TEST(GatewayFullFlowTest, AuthenticatedClient_MoveRequest_ForwardsToGame) {
  asio::io_context io_context;
  GatewayServer server;
  server.network_ = std::make_unique<network::NetworkManager>(io_context);

  auto game_client = CreateMockClient(io_context);
  server.game_client_ = std::move(game_client.client);
  server.message_router_.RegisterRoute(static_cast<uint16_t>(common::MsgId::kMoveReq),
                                       common::ServiceType::kGame, true);
  server.RegisterHandlers();

  auto session = CreateSession(io_context, 2001).session;
  session->SetAuthState(network::TcpSession::AuthState::kAuthed);

  server.network_->dispatcher_.Dispatch(session,
                                        static_cast<uint16_t>(common::MsgId::kMoveReq),
                                        std::vector<uint8_t>{4, 5});
  DrainIoContext(io_context);

  EXPECT_EQ(game_client.socket->GetWrites().size(), 1u);
}

TEST(GatewayFullFlowTest, UnauthenticatedClient_MoveRequest_Rejected) {
  asio::io_context io_context;
  GatewayServer server;
  server.network_ = std::make_unique<network::NetworkManager>(io_context);

  auto game_client = CreateMockClient(io_context);
  server.game_client_ = std::move(game_client.client);
  server.message_router_.RegisterRoute(static_cast<uint16_t>(common::MsgId::kMoveReq),
                                       common::ServiceType::kGame, true);
  server.RegisterHandlers();

  auto session = CreateSession(io_context, 2002).session;
  session->SetAuthState(network::TcpSession::AuthState::kPending);

  server.network_->dispatcher_.Dispatch(session,
                                        static_cast<uint16_t>(common::MsgId::kMoveReq),
                                        std::vector<uint8_t>{4, 5});
  DrainIoContext(io_context);

  EXPECT_TRUE(game_client.socket->GetWrites().empty());
}

// Verify routed responses target the correct client session.
TEST(GatewayFullFlowTest, MultipleClients_IndependentRouting) {
  asio::io_context io_context;
  GatewayServer server;
  server.network_ = std::make_unique<network::NetworkManager>(io_context);

  auto world_client = CreateMockClient(io_context);
  server.world_client_ = std::move(world_client.client);

  auto session_a = CreateSession(io_context, 3001);
  auto session_b = CreateSession(io_context, 3002);
  RegisterSession(server.network_.get(), session_a.session);
  RegisterSession(server.network_.get(), session_b.session);

  const std::vector<uint8_t> payload_a{1};
  const std::vector<uint8_t> payload_b{2};
  auto response_a =
      common::BuildRoutedMessage(3001u, static_cast<uint16_t>(common::MsgId::kChatRsp),
                                 payload_a);
  auto response_b =
      common::BuildRoutedMessage(3002u, static_cast<uint16_t>(common::MsgId::kChatRsp),
                                 payload_b);

  server.OnServicePacket(common::ServiceType::kWorld,
                         network::Packet{
                             static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage),
                             response_a});
  server.OnServicePacket(common::ServiceType::kWorld,
                         network::Packet{
                             static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage),
                             response_b});
  DrainIoContext(io_context);

  ASSERT_EQ(session_a.socket->GetWrites().size(), 1u);
  ASSERT_EQ(session_b.socket->GetWrites().size(), 1u);

  network::Packet packet_a{};
  network::Packet packet_b{};
  ASSERT_TRUE(DecodeSinglePacket(session_a.socket->GetWrites().front(), &packet_a));
  ASSERT_TRUE(DecodeSinglePacket(session_b.socket->GetWrites().front(), &packet_b));
  EXPECT_EQ(packet_a.payload, payload_a);
  EXPECT_EQ(packet_b.payload, payload_b);
}

}  // namespace mir2::gateway
