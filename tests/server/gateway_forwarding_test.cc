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

}  // namespace

// Auth checks and forwarding behavior.
TEST(GatewayForwardingTest, ForwardMessageRequiresAuth_Unauthenticated_Rejected) {
  asio::io_context io_context;
  GatewayServer server;
  server.network_ = std::make_unique<network::NetworkManager>(io_context);

  auto game_client = CreateMockClient(io_context);
  server.game_client_ = std::move(game_client.client);
  server.message_router_.RegisterRoute(static_cast<uint16_t>(common::MsgId::kMoveReq),
                                       common::ServiceType::kGame, true);
  server.RegisterHandlers();

  auto session = CreateSession(io_context, 100).session;
  session->SetAuthState(network::TcpSession::AuthState::kPending);

  server.network_->dispatcher_.Dispatch(session,
                                        static_cast<uint16_t>(common::MsgId::kMoveReq),
                                        std::vector<uint8_t>{1, 2});
  DrainIoContext(io_context);

  EXPECT_TRUE(game_client.socket->GetWrites().empty());
}

TEST(GatewayForwardingTest, ForwardMessageRequiresAuth_Authenticated_Allowed) {
  asio::io_context io_context;
  GatewayServer server;
  server.network_ = std::make_unique<network::NetworkManager>(io_context);

  auto game_client = CreateMockClient(io_context);
  server.game_client_ = std::move(game_client.client);
  server.message_router_.RegisterRoute(static_cast<uint16_t>(common::MsgId::kMoveReq),
                                       common::ServiceType::kGame, true);
  server.RegisterHandlers();

  auto session = CreateSession(io_context, 101).session;
  session->SetAuthState(network::TcpSession::AuthState::kAuthed);

  const std::vector<uint8_t> payload{9, 8, 7};
  server.network_->dispatcher_.Dispatch(session,
                                        static_cast<uint16_t>(common::MsgId::kMoveReq),
                                        payload);
  DrainIoContext(io_context);

  const auto& writes = game_client.socket->GetWrites();
  ASSERT_EQ(writes.size(), 1u);
  network::Packet packet{};
  ASSERT_TRUE(DecodeSinglePacket(writes.front(), &packet));
  EXPECT_EQ(packet.msg_id, static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage));

  common::RoutedMessageData routed;
  ASSERT_TRUE(common::ParseRoutedMessage(packet.payload, &routed));
  EXPECT_EQ(routed.client_id, 101u);
  EXPECT_EQ(routed.msg_id, static_cast<uint16_t>(common::MsgId::kMoveReq));
  EXPECT_EQ(routed.payload, payload);
}

TEST(GatewayForwardingTest, ForwardMessageNoAuthRequired_AlwaysAllowed) {
  asio::io_context io_context;
  GatewayServer server;
  server.network_ = std::make_unique<network::NetworkManager>(io_context);

  auto db_client = CreateMockClient(io_context);
  server.db_client_ = std::move(db_client.client);
  server.message_router_.RegisterRoute(static_cast<uint16_t>(common::MsgId::kLoginReq),
                                       common::ServiceType::kDb, false);
  server.RegisterHandlers();

  auto session = CreateSession(io_context, 102).session;
  session->SetAuthState(network::TcpSession::AuthState::kUnknown);

  server.network_->dispatcher_.Dispatch(session,
                                        static_cast<uint16_t>(common::MsgId::kLoginReq),
                                        std::vector<uint8_t>{});
  DrainIoContext(io_context);

  EXPECT_EQ(db_client.socket->GetWrites().size(), 1u);
}

// Error paths: missing routes or disconnected services drop messages.
TEST(GatewayForwardingTest, ForwardToUnknownService_DropsMessage) {
  asio::io_context io_context;
  GatewayServer server;
  server.network_ = std::make_unique<network::NetworkManager>(io_context);

  auto game_client = CreateMockClient(io_context);
  server.game_client_ = std::move(game_client.client);
  server.RegisterHandlers();

  auto session = CreateSession(io_context, 103).session;
  session->SetAuthState(network::TcpSession::AuthState::kAuthed);

  server.network_->dispatcher_.Dispatch(session, 9999, std::vector<uint8_t>{1});
  DrainIoContext(io_context);

  EXPECT_TRUE(game_client.socket->GetWrites().empty());
}

TEST(GatewayForwardingTest, ForwardToDisconnectedService_DropsMessage) {
  asio::io_context io_context;
  GatewayServer server;
  server.network_ = std::make_unique<network::NetworkManager>(io_context);

  auto world_client = CreateMockClient(io_context);
  world_client.client->connected_.store(false);
  server.world_client_ = std::move(world_client.client);
  server.message_router_.RegisterRoute(static_cast<uint16_t>(common::MsgId::kCreateRoleReq),
                                       common::ServiceType::kWorld, true);
  server.RegisterHandlers();

  auto session = CreateSession(io_context, 104).session;
  session->SetAuthState(network::TcpSession::AuthState::kAuthed);

  server.network_->dispatcher_.Dispatch(session,
                                        static_cast<uint16_t>(common::MsgId::kCreateRoleReq),
                                        std::vector<uint8_t>{});
  DrainIoContext(io_context);

  EXPECT_TRUE(world_client.socket->GetWrites().empty());
}

TEST(GatewayForwardingTest, ForwardWithInvalidMsgId_DropsMessage) {
  asio::io_context io_context;
  GatewayServer server;
  server.network_ = std::make_unique<network::NetworkManager>(io_context);

  auto db_client = CreateMockClient(io_context);
  server.db_client_ = std::move(db_client.client);
  server.RegisterHandlers();

  auto session = CreateSession(io_context, 105).session;
  session->SetAuthState(network::TcpSession::AuthState::kAuthed);

  server.network_->dispatcher_.Dispatch(session, 0, std::vector<uint8_t>{});
  DrainIoContext(io_context);

  EXPECT_TRUE(db_client.socket->GetWrites().empty());
}

// Service state accessors.
TEST(GatewayForwardingTest, IsServiceConnectedReturnsCorrectState) {
  asio::io_context io_context;
  GatewayServer server;

  auto world_client = CreateMockClient(io_context);
  server.world_client_ = std::move(world_client.client);
  EXPECT_TRUE(server.IsServiceConnected(common::ServiceType::kWorld));

  server.world_client_->connected_.store(false);
  EXPECT_FALSE(server.IsServiceConnected(common::ServiceType::kWorld));
}

TEST(GatewayForwardingTest, GetServiceClientReturnsCorrectClient) {
  asio::io_context io_context;
  GatewayServer server;

  auto db_client = CreateMockClient(io_context);
  auto* raw_ptr = db_client.client.get();
  server.db_client_ = std::move(db_client.client);

  EXPECT_EQ(server.GetServiceClient(common::ServiceType::kDb), raw_ptr);
  EXPECT_EQ(server.GetServiceClient(common::ServiceType::kWorld), nullptr);
}

struct RouteParam {
  uint16_t msg_id;
  common::ServiceType target;
};

class GatewayRouteParamTest : public ::testing::TestWithParam<RouteParam> {};

TEST_P(GatewayRouteParamTest, ForwardRoutesToExpectedService) {
  asio::io_context io_context;
  GatewayServer server;
  server.network_ = std::make_unique<network::NetworkManager>(io_context);

  auto world_client = CreateMockClient(io_context);
  auto game_client = CreateMockClient(io_context);
  auto db_client = CreateMockClient(io_context);
  server.world_client_ = std::move(world_client.client);
  server.game_client_ = std::move(game_client.client);
  server.db_client_ = std::move(db_client.client);

  const auto param = GetParam();
  server.message_router_.RegisterRoute(param.msg_id, param.target, false);
  server.RegisterHandlers();

  auto session = CreateSession(io_context, 200).session;
  session->SetAuthState(network::TcpSession::AuthState::kAuthed);

  server.network_->dispatcher_.Dispatch(session, param.msg_id, std::vector<uint8_t>{});
  DrainIoContext(io_context);

  const auto world_writes = world_client.socket->GetWrites().size();
  const auto game_writes = game_client.socket->GetWrites().size();
  const auto db_writes = db_client.socket->GetWrites().size();

  switch (param.target) {
    case common::ServiceType::kWorld:
      EXPECT_EQ(world_writes, 1u);
      EXPECT_EQ(game_writes, 0u);
      EXPECT_EQ(db_writes, 0u);
      break;
    case common::ServiceType::kGame:
      EXPECT_EQ(world_writes, 0u);
      EXPECT_EQ(game_writes, 1u);
      EXPECT_EQ(db_writes, 0u);
      break;
    case common::ServiceType::kDb:
      EXPECT_EQ(world_writes, 0u);
      EXPECT_EQ(game_writes, 0u);
      EXPECT_EQ(db_writes, 1u);
      break;
    default:
      FAIL() << "Unexpected service type";
  }
}

INSTANTIATE_TEST_SUITE_P(
    GatewayForwardingTest,
    GatewayRouteParamTest,
    ::testing::Values(RouteParam{static_cast<uint16_t>(common::MsgId::kCreateRoleReq),
                                 common::ServiceType::kWorld},
                      RouteParam{static_cast<uint16_t>(common::MsgId::kMoveReq),
                                 common::ServiceType::kGame},
                      RouteParam{static_cast<uint16_t>(common::MsgId::kLoginReq),
                                 common::ServiceType::kDb}));

}  // namespace mir2::gateway
