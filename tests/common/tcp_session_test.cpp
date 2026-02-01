#include <gtest/gtest.h>

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

#include "network/tcp_connection.h"
#include "network/tcp_session.h"

namespace mir2::network {

namespace {

std::shared_ptr<TcpSession> CreateSession(asio::io_context& io_context) {
  asio::ip::tcp::acceptor acceptor(
      io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
  const uint16_t port = acceptor.local_endpoint().port();

  asio::ip::tcp::socket client_socket(io_context);
  client_socket.connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), port));

  asio::ip::tcp::socket server_socket = acceptor.accept();

  auto connection = std::make_shared<TcpConnection>(
      std::make_unique<AsioSocketAdapter>(std::move(server_socket)),
      1);
  auto session = std::make_shared<TcpSession>(connection);
  std::weak_ptr<TcpSession> weak_session = session;
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

TEST(TcpSessionTest, RateLimitExceededMarksLimited) {
  asio::io_context io_context;
  auto session = CreateSession(io_context);

  Packet packet{1, std::vector<uint8_t>(1, 0)};
  for (int i = 0; i < 50; ++i) {
    session->HandlePacket(1, packet);
    EXPECT_FALSE(session->IsRateLimited());
  }

  session->HandlePacket(1, packet);
  EXPECT_TRUE(session->IsRateLimited());
  EXPECT_NE(session->GetState(), TcpSession::SessionState::kActive);
}

}  // namespace mir2::network
