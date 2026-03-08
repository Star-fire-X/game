#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include <cstring>
#include <memory>
#include <vector>

#include "common/enums.h"
#include "common/protocol/packet_codec.h"
#include "network/tcp_connection.h"
#include "network/tcp_session.h"
#include "mocks/mock_socket.h"

namespace mir2::network::test {
namespace {

std::shared_ptr<TcpSession> CreateSession(asio::io_context& io_context,
                                          MockSocket** out_socket) {
  auto mock_socket = std::make_unique<MockSocket>(io_context.get_executor());
  auto* socket_ptr = mock_socket.get();
  auto connection = std::make_shared<network::TcpConnection>(std::move(mock_socket), 9001);
  auto session = std::make_shared<network::TcpSession>(connection);
  session->Start();
  if (out_socket != nullptr) {
    *out_socket = socket_ptr;
  }
  return session;
}

std::vector<uint8_t> BuildV1HeartbeatPacket() {
  constexpr uint32_t kLegacyV1Magic = 0x4D495232;  // "MIR2"
  constexpr uint16_t kMsgId = static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat);
  constexpr uint32_t kPayloadSize = 0;
  std::vector<uint8_t> bytes(10, 0);
  std::memcpy(bytes.data(), &kLegacyV1Magic, sizeof(kLegacyV1Magic));
  std::memcpy(bytes.data() + sizeof(kLegacyV1Magic), &kMsgId, sizeof(kMsgId));
  std::memcpy(bytes.data() + sizeof(kLegacyV1Magic) + sizeof(kMsgId),
              &kPayloadSize,
              sizeof(kPayloadSize));
  return bytes;
}

class TcpSessionProtocolGateTest : public ::testing::Test {
};

TEST_F(TcpSessionProtocolGateTest, RejectsV1AfterAuth) {
  asio::io_context io_context;
  MockSocket* socket = nullptr;
  auto session = CreateSession(io_context, &socket);

  ASSERT_NE(session, nullptr);
  ASSERT_NE(socket, nullptr);
  session->SetAccountId(1001);

  const auto packet = BuildV1HeartbeatPacket();
  session->HandleBytes(packet.data(), packet.size());
  io_context.poll();

  EXPECT_NE(session->GetState(), TcpSession::SessionState::kActive);
  EXPECT_TRUE(socket->IsClosed());
}

TEST_F(TcpSessionProtocolGateTest, RejectsV1BeforeAuth) {
  asio::io_context io_context;
  MockSocket* socket = nullptr;
  auto session = CreateSession(io_context, &socket);

  ASSERT_NE(session, nullptr);
  ASSERT_NE(socket, nullptr);

  const auto packet = BuildV1HeartbeatPacket();
  session->HandleBytes(packet.data(), packet.size());
  io_context.poll();

  EXPECT_NE(session->GetState(), TcpSession::SessionState::kActive);
  EXPECT_TRUE(socket->IsClosed());
}

}  // namespace
}  // namespace mir2::network::test
