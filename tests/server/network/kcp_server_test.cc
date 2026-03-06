/**
 * @file kcp_server_test.cc
 * @brief KcpServer unit tests.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <asio.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/time_utils.h"
#include "common/enums.h"
#include "network/kcp_server.h"

namespace {

using mir2::network::ConvBlacklist;
using mir2::network::IpRateLimiter;
using mir2::network::KcpServer;
using mir2::network::KcpSession;

constexpr int64_t kHandshakeTimeoutMs = 30000;  // Matches kHandshakeTimeoutMs in kcp_server.cc.

std::array<uint8_t, KcpSession::kTokenSize> MakeToken(uint8_t seed) {
  std::array<uint8_t, KcpSession::kTokenSize> token{};
  for (size_t i = 0; i < token.size(); ++i) {
    token[i] = static_cast<uint8_t>(seed + i);
  }
  return token;
}

asio::ip::udp::endpoint MakeEndpoint(uint16_t port) {
  return asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), port);
}

// Simulate UDP receive by injecting raw bytes into the server buffer.
void InjectPacket(KcpServer& server,
                  const asio::ip::udp::endpoint& endpoint,
                  uint32_t conv,
                  const std::array<uint8_t, KcpSession::kTokenSize>& token,
                  const std::vector<uint8_t>& payload) {
  const size_t header_size = sizeof(uint32_t) + KcpSession::kTokenSize;
  const size_t total_size = header_size + payload.size();
  std::vector<uint8_t> raw(total_size);
  std::memcpy(raw.data(), &conv, sizeof(conv));
  std::memcpy(raw.data() + sizeof(conv), token.data(), token.size());
  if (!payload.empty()) {
    std::memcpy(raw.data() + header_size, payload.data(), payload.size());
  }
  server.HandleRawPacketForTesting(endpoint, raw.data(), raw.size());
}

void RunIoContextFor(asio::io_context& io_context, std::chrono::milliseconds duration) {
  asio::steady_timer timer(io_context);
  timer.expires_after(duration);
  timer.async_wait([&](const asio::error_code&) { io_context.stop(); });
  io_context.run();
  io_context.restart();
}

std::vector<uint8_t> BuildKcpPacket(uint32_t conv,
                                    const std::array<uint8_t, KcpSession::kTokenSize>& token,
                                    const asio::ip::udp::endpoint& endpoint,
                                    uint16_t msg_id,
                                    const std::vector<uint8_t>& payload) {
  auto client = std::make_shared<KcpSession>(conv, token);
  client->SetRemoteEndpoint(endpoint);

  std::vector<uint8_t> packet;
  client->SetOutputHandler(
      [&packet](const asio::ip::udp::endpoint&, std::vector<uint8_t>&& udp_packet) {
        packet = std::move(udp_packet);
      });

  client->Send(msg_id, payload);
  const uint32_t now_ms = static_cast<uint32_t>(mir2::common::now_ms());
  for (int i = 0; i < 3 && packet.empty(); ++i) {
    client->Update(now_ms + static_cast<uint32_t>(i * 10));
  }
  return packet;
}

class MockMessageHandler {
 public:
  MOCK_METHOD(void, OnMessage,
              (const std::shared_ptr<KcpSession>&, const KcpSession::Packet&));
};

}  // namespace

TEST(KcpServerTest, AddRemoveGetSession) {
  asio::io_context io_context;
  KcpServer server(io_context);
  auto token = MakeToken(1);
  auto session = std::make_shared<KcpSession>(1, token);

  EXPECT_TRUE(server.AddSession(session));
  EXPECT_EQ(server.GetSession(1), session);

  server.RemoveSession(1);
  EXPECT_EQ(server.GetSession(1), nullptr);
}

TEST(KcpServerTest, AddSessionRejectsInvalidConv) {
  asio::io_context io_context;
  KcpServer server(io_context);
  auto token = MakeToken(2);
  auto session = std::make_shared<KcpSession>(0, token);

  EXPECT_FALSE(server.AddSession(session));
  EXPECT_EQ(server.GetSession(0), nullptr);
}

TEST(KcpServerTest, AddSessionRejectsDuplicateConv) {
  asio::io_context io_context;
  KcpServer server(io_context);
  auto token = MakeToken(3);
  auto first = std::make_shared<KcpSession>(100, token);
  auto second = std::make_shared<KcpSession>(100, token);

  EXPECT_TRUE(server.AddSession(first));
  EXPECT_FALSE(server.AddSession(second));
}

TEST(KcpServerTest, UpdateTimerUsesConfiguredInterval) {
  asio::io_context io_context;
  mir2::common::KcpConfig config{};
  config.interval = 10;
  KcpServer server(io_context, config);

  const auto start = std::chrono::steady_clock::now();
  server.StartUpdateTimerForTesting();
  const auto expiry = server.GetUpdateTimerExpiryForTesting();
  const auto delta_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(expiry - start).count();

  EXPECT_GE(delta_ms, config.interval);
  EXPECT_LE(delta_ms, config.interval + 10);
  server.CancelUpdateTimerForTesting();
}

TEST(KcpServerTest, UpdateTimerRemovesExpiredHandshakeSessions) {
  asio::io_context io_context;
  KcpServer server(io_context);
  auto token = MakeToken(4);
  auto session = std::make_shared<KcpSession>(200, token);
  ASSERT_TRUE(server.AddSession(session));

  const int64_t now_ms = mir2::common::now_ms();
  session->SetHasRemoteEndpointForTesting(false);
  session->SetLastActiveMsForTesting(now_ms - (kHandshakeTimeoutMs + 1000));

  server.StartUpdateTimerForTesting();
  RunIoContextFor(io_context, std::chrono::milliseconds(20));
  server.CancelUpdateTimerForTesting();

  EXPECT_EQ(server.GetSession(200), nullptr);
}

TEST(KcpServerTest, HandshakeTimeoutUsesThirtySeconds) {
  asio::io_context io_context;
  KcpServer server(io_context);
  auto token = MakeToken(5);
  auto session = std::make_shared<KcpSession>(300, token);
  ASSERT_TRUE(server.AddSession(session));

  session->SetHasRemoteEndpointForTesting(false);

  const int64_t now_ms = mir2::common::now_ms();
  session->SetLastActiveMsForTesting(now_ms - (kHandshakeTimeoutMs - 1000));
  server.UpdateAllSessionsForTesting();
  EXPECT_NE(server.GetSession(300), nullptr);

  session->SetLastActiveMsForTesting(now_ms - (kHandshakeTimeoutMs + 1));
  server.UpdateAllSessionsForTesting();
  EXPECT_EQ(server.GetSession(300), nullptr);
}

TEST(KcpServerTest, RateLimiterBlocksExcessPackets) {
  asio::io_context io_context;
  KcpServer server(io_context);

  IpRateLimiter::Config config{};
  config.max_packets_per_sec = 1;
  config.window_ms = 1000;
  config.cleanup_interval_ms = 0;
  server.SetRateLimiterConfig(config);

  auto token = MakeToken(6);
  auto session = std::make_shared<KcpSession>(400, token);
  ASSERT_TRUE(server.AddSession(session));
  session->SetLastActiveMsForTesting(0);

  const auto endpoint = MakeEndpoint(9001);
  const std::vector<uint8_t> payload = {0x01};

  InjectPacket(server, endpoint, 400, token, payload);
  const int64_t first_active = session->GetLastActiveMs();
  EXPECT_GT(first_active, 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  InjectPacket(server, endpoint, 400, token, payload);

  const int64_t second_active = session->GetLastActiveMs();
  EXPECT_EQ(second_active, first_active);
}

TEST(KcpServerTest, BlacklistBlocksAfterInvalidToken) {
  asio::io_context io_context;
  KcpServer server(io_context);

  ConvBlacklist::Config config{};
  config.max_failures = 2;
  config.blacklist_ttl_ms = 60000;
  config.failure_ttl_ms = 60000;
  config.cleanup_interval_ms = 0;
  server.SetConvBlacklistConfig(config);

  auto token = MakeToken(7);
  auto session = std::make_shared<KcpSession>(500, token);
  ASSERT_TRUE(server.AddSession(session));
  session->SetLastActiveMsForTesting(0);

  const auto endpoint = MakeEndpoint(9002);
  const std::vector<uint8_t> payload = {0x02};
  const auto bad_token = MakeToken(99);

  InjectPacket(server, endpoint, 500, bad_token, payload);
  InjectPacket(server, endpoint, 500, bad_token, payload);

  EXPECT_EQ(session->GetLastActiveMs(), 0);
  const uint32_t endpoint_ip = endpoint.address().to_v4().to_uint();
  EXPECT_TRUE(server.IsConvBlacklisted(500, endpoint_ip, mir2::common::now_ms()));

  InjectPacket(server, endpoint, 500, token, payload);
  EXPECT_EQ(session->GetLastActiveMs(), 0);
}

TEST(KcpServerTest, BlacklistIsScopedByConvAndSourceIp) {
  asio::io_context io_context;
  KcpServer server(io_context);

  ConvBlacklist::Config config{};
  config.max_failures = 1;
  config.blacklist_ttl_ms = 60000;
  config.failure_ttl_ms = 60000;
  config.cleanup_interval_ms = 0;
  server.SetConvBlacklistConfig(config);

  auto token = MakeToken(17);
  auto session = std::make_shared<KcpSession>(517, token);
  ASSERT_TRUE(server.AddSession(session));
  session->SetLastActiveMsForTesting(0);

  const auto endpoint_a =
      asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), 9010);
  const auto endpoint_b =
      asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.2"), 9011);
  const auto bad_token = MakeToken(199);
  const std::vector<uint8_t> payload = {0xAA};

  InjectPacket(server, endpoint_a, 517, bad_token, payload);
  const uint32_t ip_a = endpoint_a.address().to_v4().to_uint();
  const uint32_t ip_b = endpoint_b.address().to_v4().to_uint();
  EXPECT_TRUE(server.IsConvBlacklisted(517, ip_a, mir2::common::now_ms()));
  EXPECT_FALSE(server.IsConvBlacklisted(517, ip_b, mir2::common::now_ms()));

  InjectPacket(server, endpoint_b, 517, token, payload);
  EXPECT_GT(session->GetLastActiveMs(), 0);
}

TEST(KcpServerTest, EndpointPinningRejectsMigrationAndSkipsEmptyPayloadBinding) {
  asio::io_context io_context;
  KcpServer server(io_context);

  ConvBlacklist::Config config{};
  config.max_failures = 1;
  config.blacklist_ttl_ms = 60000;
  config.failure_ttl_ms = 60000;
  config.cleanup_interval_ms = 0;
  server.SetConvBlacklistConfig(config);

  auto token = MakeToken(27);
  auto session = std::make_shared<KcpSession>(527, token);
  ASSERT_TRUE(server.AddSession(session));
  session->SetLastActiveMsForTesting(0);

  const auto endpoint_a = MakeEndpoint(9020);
  const auto endpoint_b = MakeEndpoint(9021);
  const std::vector<uint8_t> payload = {0x01, 0x02};

  // Empty KCP payload should not bind endpoint.
  InjectPacket(server, endpoint_a, 527, token, {});
  EXPECT_FALSE(session->HasRemoteEndpoint());

  InjectPacket(server, endpoint_a, 527, token, payload);
  EXPECT_TRUE(session->HasRemoteEndpoint());
  EXPECT_EQ(session->GetRemoteEndpoint(), endpoint_a);
  const int64_t first_active = session->GetLastActiveMs();
  EXPECT_GT(first_active, 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  InjectPacket(server, endpoint_b, 527, token, payload);
  EXPECT_EQ(session->GetRemoteEndpoint(), endpoint_a);
  EXPECT_EQ(session->GetLastActiveMs(), first_active);

  const uint32_t ip_b = endpoint_b.address().to_v4().to_uint();
  EXPECT_TRUE(server.IsConvBlacklisted(527, ip_b, mir2::common::now_ms()));
}

TEST(KcpServerTest, HandleReceiveDispatchesValidPacket) {
  asio::io_context io_context;
  KcpServer server(io_context);

  auto token = MakeToken(8);
  auto session = std::make_shared<KcpSession>(600, token);
  ASSERT_TRUE(server.AddSession(session));

  testing::StrictMock<MockMessageHandler> mock;
  server.SetMessageHandler(
      [&mock](const std::shared_ptr<KcpSession>& sess,
              const KcpSession::Packet& packet) {
        mock.OnMessage(sess, packet);
      });

  const uint16_t msg_id = static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat);
  const std::vector<uint8_t> payload{};
  const auto endpoint = MakeEndpoint(9003);

  auto packet = BuildKcpPacket(600, token, endpoint, msg_id, payload);
  ASSERT_FALSE(packet.empty());

  EXPECT_CALL(mock, OnMessage(testing::_, testing::_))
      .WillOnce([&](const std::shared_ptr<KcpSession>& sess,
                    const KcpSession::Packet& pkt) {
        EXPECT_EQ(sess, session);
        EXPECT_EQ(pkt.msg_id, msg_id);
        EXPECT_EQ(pkt.payload, payload);
      });

  server.HandleRawPacketForTesting(endpoint, packet.data(), packet.size());
}

TEST(KcpServerTest, SendRawRecordsUdpSendErrors) {
  asio::io_context io_context;
  KcpServer server(io_context);
  ASSERT_TRUE(server.Start("127.0.0.1", 0));

  const auto endpoint = MakeEndpoint(9004);
  std::vector<uint8_t> oversized_payload(70000, 0xAB);
  server.SendRawForTesting(endpoint, oversized_payload.data(), oversized_payload.size());

  RunIoContextFor(io_context, std::chrono::milliseconds(50));

  EXPECT_GE(server.GetUdpSendErrorCountForTesting(), 1u);
  server.Stop();
}

TEST(KcpServerTest, SendRawFaultInjectionIncrementsUdpSendErrors) {
  asio::io_context io_context;
  KcpServer server(io_context);
  ASSERT_TRUE(server.Start("127.0.0.1", 0));

  server.SetUdpSendFaultInjectEveryN(2);
  EXPECT_EQ(server.GetUdpSendFaultInjectEveryN(), 2u);

  const auto endpoint = MakeEndpoint(9005);
  std::vector<uint8_t> payload(64, 0x5A);
  server.SendRawForTesting(endpoint, payload.data(), payload.size());
  server.SendRawForTesting(endpoint, payload.data(), payload.size());
  RunIoContextFor(io_context, std::chrono::milliseconds(20));

  EXPECT_GE(server.GetUdpSendErrorCountForTesting(), 1u);
  server.Stop();
}
