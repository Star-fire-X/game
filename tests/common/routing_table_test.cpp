#include <gtest/gtest.h>

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

#include "gateway/gateway_server.h"
#include "network/tcp_connection.h"
#include "network/tcp_session.h"

namespace {

std::shared_ptr<mir2::network::TcpSession> CreateSession(asio::io_context& io_context,
                                                         uint64_t connection_id) {
  asio::ip::tcp::acceptor acceptor(
      io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
  const uint16_t port = acceptor.local_endpoint().port();

  asio::ip::tcp::socket client_socket(io_context);
  client_socket.connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), port));

  asio::ip::tcp::socket server_socket = acceptor.accept();

  auto connection = std::make_shared<mir2::network::TcpConnection>(
      std::make_unique<mir2::network::AsioSocketAdapter>(std::move(server_socket)),
      connection_id);
  auto session = std::make_shared<mir2::network::TcpSession>(connection);
  std::weak_ptr<mir2::network::TcpSession> weak_session = session;
  connection->SetReadHandler([weak_session](const uint8_t* data, size_t size) {
    if (auto locked = weak_session.lock()) {
      locked->HandleBytes(data, size);
    }
  });
  connection->SetDisconnectHandler([weak_session](uint64_t id) {
    if (auto locked = weak_session.lock()) {
      locked->HandleDisconnect(id);
    }
  });
  session->Start();
  return session;
}

}  // namespace

TEST(routing_table_test, RegisterConnectionStoresSession) {
  asio::io_context io_context;
  auto session = CreateSession(io_context, 101);
  mir2::gateway::GatewayServer server;

  server.RegisterConnection(101, session);

  EXPECT_EQ(server.GetConnectionCount(), 1u);
  EXPECT_EQ(server.GetConnectionSession(101), session);
}

TEST(routing_table_test, RegisterConnectionReplacesSession) {
  asio::io_context io_context;
  auto session_a = CreateSession(io_context, 201);
  auto session_b = CreateSession(io_context, 201);
  mir2::gateway::GatewayServer server;

  server.RegisterConnection(201, session_a);
  server.RegisterConnection(201, session_b);

  EXPECT_EQ(server.GetConnectionCount(), 1u);
  EXPECT_EQ(server.GetConnectionSession(201), session_b);
}

TEST(routing_table_test, UnregisterSessionRemovesSession) {
  asio::io_context io_context;
  auto session = CreateSession(io_context, 301);
  mir2::gateway::GatewayServer server;

  server.RegisterConnection(301, session);
  server.UnregisterSession(session);

  EXPECT_EQ(server.GetConnectionCount(), 0u);
  EXPECT_EQ(server.GetConnectionSession(301), nullptr);
}

TEST(routing_table_test, CleanupStaleRoutesRemovesClosedSessions) {
  asio::io_context io_context;
  auto session = CreateSession(io_context, 401);
  mir2::gateway::GatewayServer server;

  server.RegisterConnection(401, session);

  session->SetDisconnectedHandler(nullptr);
  session->HandleDisconnect(401);

  server.CleanupStaleRoutes();

  EXPECT_EQ(server.GetConnectionCount(), 0u);
}
