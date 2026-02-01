#include <gtest/gtest.h>

#include <asio/error.hpp>
#include <asio/io_context.hpp>

#include "mocks/mock_socket.h"
#include "network/packet_codec.h"
#include "network/tcp_connection.h"
#include "network/tcp_session.h"

namespace mir2::network {

namespace {

std::shared_ptr<TcpConnection> CreateConnection(asio::io_context& io_context,
                                                MockSocket** out_socket) {
  auto mock_socket = std::make_unique<MockSocket>(io_context.get_executor());
  if (out_socket) {
    *out_socket = mock_socket.get();
  }
  return std::make_shared<TcpConnection>(std::move(mock_socket), 1);
}

}  // namespace

TEST(TcpConnectionTest, ReadV1PacketTriggersHandler) {
  asio::io_context io_context;
  MockSocket* mock_socket = nullptr;
  auto connection = CreateConnection(io_context, &mock_socket);
  auto session = std::make_shared<TcpSession>(connection);
  std::weak_ptr<TcpSession> weak_session = session;

  connection->SetReadHandler([weak_session](const uint8_t* data, size_t size) {
    if (auto locked = weak_session.lock()) {
      locked->HandleBytes(data, size);
    }
  });

  Packet received{};
  bool called = false;
  session->SetMessageHandler([&](const std::shared_ptr<TcpSession>& active_session,
                                 const Packet& packet) {
    ASSERT_TRUE(active_session);
    EXPECT_EQ(active_session->GetSessionId(), 1u);
    received = packet;
    called = true;
  });

  session->Start();

  std::vector<uint8_t> payload{1, 2, 3, 4};
  auto encoded = PacketCodec::Encode(42, payload.data(), payload.size());
  mock_socket->PushReadData(std::move(encoded));

  io_context.run();

  EXPECT_TRUE(called);
  EXPECT_EQ(received.msg_id, 42u);
  EXPECT_EQ(received.payload, payload);
}

TEST(TcpConnectionTest, ReadV2PacketTriggersHandler) {
  asio::io_context io_context;
  MockSocket* mock_socket = nullptr;
  auto connection = CreateConnection(io_context, &mock_socket);
  auto session = std::make_shared<TcpSession>(connection);
  std::weak_ptr<TcpSession> weak_session = session;

  connection->SetReadHandler([weak_session](const uint8_t* data, size_t size) {
    if (auto locked = weak_session.lock()) {
      locked->HandleBytes(data, size);
    }
  });

  Packet received{};
  bool called = false;
  session->SetMessageHandler([&](const std::shared_ptr<TcpSession>& active_session,
                                 const Packet& packet) {
    ASSERT_TRUE(active_session);
    EXPECT_EQ(active_session->GetSessionId(), 1u);
    received = packet;
    called = true;
  });

  session->Start();

  std::vector<uint8_t> payload{9, 8, 7};
  auto encoded = PacketCodec::EncodeV2(77, payload.data(), payload.size(), 0);
  mock_socket->PushReadData(std::move(encoded));

  io_context.run();

  EXPECT_TRUE(called);
  EXPECT_EQ(received.msg_id, 77u);
  EXPECT_EQ(received.payload, payload);
}

TEST(TcpConnectionTest, SendWritesEncodedPacket) {
  asio::io_context io_context;
  MockSocket* mock_socket = nullptr;
  auto connection = CreateConnection(io_context, &mock_socket);
  auto session = std::make_shared<TcpSession>(connection);

  session->Start();

  std::vector<uint8_t> payload{5, 6, 7};
  session->Send(100, payload);
  io_context.run();

  const auto& writes = mock_socket->GetWrites();
  ASSERT_EQ(writes.size(), 1u);
  const auto expected = PacketCodec::Encode(100, payload.data(), payload.size());
  EXPECT_EQ(writes.front(), expected);
}

TEST(TcpConnectionTest, ReadErrorTriggersDisconnect) {
  asio::io_context io_context;
  MockSocket* mock_socket = nullptr;
  auto connection = CreateConnection(io_context, &mock_socket);

  bool disconnected = false;
  connection->SetDisconnectHandler([&](uint64_t id) {
    EXPECT_EQ(id, 1u);
    disconnected = true;
  });

  connection->Start();
  mock_socket->SetReadError(asio::error::connection_reset);
  io_context.run();

  EXPECT_TRUE(disconnected);
  EXPECT_TRUE(mock_socket->IsClosed());
}

TEST(TcpConnectionTest, RateLimitExceededMarksSessionLimited) {
  asio::io_context io_context;
  MockSocket* mock_socket = nullptr;
  auto connection = CreateConnection(io_context, &mock_socket);
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

  std::vector<uint8_t> payload{0x01};
  for (int i = 0; i < 51; ++i) {
    auto encoded = PacketCodec::Encode(1, payload.data(), payload.size());
    mock_socket->PushReadData(std::move(encoded));
  }

  io_context.run();

  EXPECT_TRUE(session->IsRateLimited());
  EXPECT_NE(session->GetState(), TcpSession::SessionState::kActive);
}

}  // namespace mir2::network
