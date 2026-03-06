#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include "network/network_manager.h"

#include "mocks/mock_socket.h"

namespace mir2::network {
namespace {

std::shared_ptr<TcpConnection> MakeConnection(asio::io_context& io_context,
                                              uint64_t connection_id,
                                              MockSocket** out_socket) {
  auto socket = std::make_unique<MockSocket>(io_context.get_executor());
  if (out_socket) {
    *out_socket = socket.get();
  }
  return std::make_shared<TcpConnection>(std::move(socket), connection_id);
}

}  // namespace

TEST(NetworkManagerTest, AddConnectionRejectsWhenConnectionLimitReached) {
  asio::io_context io_context;
  NetworkManager manager(io_context);
  manager.SetMaxConnectionsForTest(1);

  MockSocket* first_socket = nullptr;
  auto first = MakeConnection(io_context, 1, &first_socket);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(first_socket, nullptr);

  manager.AddConnectionForTest(first);
  io_context.poll();
  io_context.restart();

  EXPECT_EQ(manager.GetTrackedConnectionCountForTest(), 1u);
  EXPECT_EQ(manager.GetConnectionCount(), 1u);

  MockSocket* second_socket = nullptr;
  auto second = MakeConnection(io_context, 2, &second_socket);
  ASSERT_NE(second, nullptr);
  ASSERT_NE(second_socket, nullptr);

  manager.AddConnectionForTest(second);
  io_context.poll();
  io_context.restart();

  EXPECT_EQ(manager.GetTrackedConnectionCountForTest(), 1u);
  EXPECT_EQ(manager.GetConnectionCount(), 1u);
  EXPECT_TRUE(second_socket->IsClosed());
}

TEST(NetworkManagerTest, TcpServerStartRejectsInvalidBindIp) {
  asio::io_context io_context;
  TcpServer server(io_context);
  EXPECT_FALSE(server.Start("not-a-valid-ip", 7000, 64));
}

}  // namespace mir2::network
