#include <gtest/gtest.h>

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#define private public
#include "client/network/dual_channel_client.h"
#undef private

#include "chat_generated.h"
#include "common/enums.h"
#include "common/network/fallback_controller.h"
#include "common/protocol/message_codec.h"
#include "integration/kcp_integration_test_base.h"
#include "integration/test_helpers.h"
#include "server/network/kcp_session.h"
#include "server/network/tcp_session.h"

namespace {

using namespace std::chrono_literals;

using mir2::client::DualChannelClient;
using mir2::common::ChannelType;
using mir2::common::FallbackController;
using mir2::common::KcpConfig;
using mir2::common::LoginRequest;
using mir2::common::LoginResponse;
using mir2::common::MessageCodecStatus;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::test::integration::KcpIntegrationTestBase;
using mir2::test::integration::PerformanceMonitor;

std::vector<uint8_t> BuildChatReqPayload(const std::string& content) {
  flatbuffers::FlatBufferBuilder builder;
  const auto content_offset = builder.CreateString(content);
  const auto req = mir2::proto::CreateChatReq(
      builder, mir2::proto::ChatChannel::WORLD, content_offset, 0);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

class KcpHandshakeIntegrationTest : public KcpIntegrationTestBase {
 protected:
  DualChannelClient* ExtraClient(DualChannelClient* target) const {
    return target == client_.get() ? nullptr : target;
  }

  template <typename Predicate>
  bool WaitForConditionWithExtra(Predicate predicate,
                                 std::chrono::milliseconds timeout,
                                 DualChannelClient* extra) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() <= deadline) {
      if (predicate()) {
        return true;
      }
      PumpOnce();
      if (extra) {
        extra->update();
      }
      std::this_thread::sleep_for(5ms);
    }
    return predicate();
  }

  std::optional<NetworkPacket> WaitForPacket(DualChannelClient* target,
                                             uint16_t msg_id,
                                             std::chrono::milliseconds timeout,
                                             DualChannelClient* extra) {
    std::optional<NetworkPacket> result;
    WaitForConditionWithExtra([&]() {
      auto packet = target->receive();
      if (packet && packet->msg_id == msg_id) {
        result = std::move(*packet);
        return true;
      }
      return false;
    }, timeout, extra);
    return result;
  }

  bool ConnectAndLogin(DualChannelClient* target) {
    DualChannelClient* extra = ExtraClient(target);
    if (!target->connect(kHost, kTcpPort)) {
      return false;
    }
    if (!WaitForConditionWithExtra([&]() { return target->is_connected(); }, 1s, extra)) {
      return false;
    }

    LoginRequest request;
    request.username = "testuser";
    request.password = "password123";
    request.version = "stage3";

    MessageCodecStatus status = MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeLoginRequest(request, &status);
    if (status != MessageCodecStatus::kOk || payload.empty()) {
      return false;
    }
    target->send(static_cast<uint16_t>(MsgId::kLoginReq), payload);

    auto packet = WaitForPacket(target, static_cast<uint16_t>(MsgId::kLoginRsp), 2s, extra);
    if (!packet) {
      return false;
    }

    LoginResponse response;
    status = mir2::common::DecodeLoginResponse(packet->msg_id, packet->payload, &response);
    if (status != MessageCodecStatus::kOk) {
      return false;
    }
    return response.code == mir2::proto::ErrorCode::ERR_OK;
  }

  bool WaitForKcpState(DualChannelClient* target,
                       DualChannelClient::KcpUpgradeState desired,
                       std::chrono::milliseconds timeout) {
    return WaitForConditionWithExtra(
        [&]() { return target->get_kcp_state() == desired; },
        timeout,
        ExtraClient(target));
  }
};

class KcpHandshakeFailureIntegrationTest : public KcpHandshakeIntegrationTest {
 protected:
  static KcpConfig FailureConfig() {
    KcpConfig config;
    config.timeout_ms = 15000;
    config.recovery_interval_ms = 30000;
    return config;
  }

  std::unique_ptr<mir2::test::integration::MockGameServer> CreateTestServer() override {
    mir2::test::integration::MockGameServer::Config config;
    config.bind_ip = kHost;
    config.tcp_port = kTcpPort;
    config.udp_port = kUdpPort;
    config.max_connections = kMaxConnections;
    config.kcp_config = FailureConfig();

    auto server = std::make_unique<mir2::test::integration::MockGameServer>(io_context_, config);
    server->Start();
    return server;
  }

  std::unique_ptr<DualChannelClient> CreateTestClient() override {
    return std::make_unique<DualChannelClient>(FailureConfig());
  }
};

TEST_F(KcpHandshakeIntegrationTest, FullHandshakeSuccess) {
  using mir2::network::TcpSession;

  auto session_id = std::make_shared<std::atomic<uint64_t>>(0);

  server_->SetMessageHook([session_id_weak = std::weak_ptr<std::atomic<uint64_t>>(session_id)](
                              ChannelType channel,
                              const std::shared_ptr<TcpSession>& session,
                              uint16_t msg_id,
                              const std::vector<uint8_t>& payload) {
    (void)channel;
    (void)msg_id;
    (void)payload;
    auto session_id_locked = session_id_weak.lock();
    if (!session_id_locked) {
      return;
    }
    if (session && session_id_locked->load(std::memory_order_relaxed) == 0) {
      session_id_locked->store(session->GetSessionId(), std::memory_order_relaxed);
    }
  });

  PerformanceMonitor monitor;

  // Step 1-2: TCP connect + login.
  ASSERT_TRUE(ConnectAndLogin(client_.get()));

  const auto handshake_start = std::chrono::steady_clock::now();

  // Step 3-7: KCP upgrade + confirm.
  ASSERT_TRUE(WaitForKcpState(client_.get(),
                              DualChannelClient::KcpUpgradeState::kConfirmed,
                              3s));

  const auto handshake_end = std::chrono::steady_clock::now();
  monitor.RecordLatency(std::chrono::duration_cast<std::chrono::nanoseconds>(
      handshake_end - handshake_start));
  const auto latency_stats = monitor.GetLatencyStats();
  EXPECT_LT(latency_stats.max_ms, 1000.0);

  ASSERT_TRUE(client_->kcp_info_.valid);
  const uint32_t conv_id = client_->kcp_info_.conv_id;
  ASSERT_NE(conv_id, 0u);
  EXPECT_EQ(client_->kcp_info_.udp_port, kUdpPort);
  EXPECT_EQ(client_->kcp_info_.token.size(), mir2::client::KcpChannel::kTokenSize);

  ASSERT_TRUE(WaitForConditionWithExtra(
      [session_id]() { return session_id->load(std::memory_order_relaxed) != 0; }, 1s, nullptr));

  auto tcp_session = server_->GetSession(session_id->load(std::memory_order_relaxed));
  ASSERT_NE(tcp_session, nullptr);

  auto kcp_session = server_->GetKcpSessionByConv(conv_id);
  ASSERT_NE(kcp_session, nullptr);
  EXPECT_EQ(kcp_session->GetConvId(), conv_id);
  EXPECT_EQ(kcp_session->GetTcpSession().get(), tcp_session.get());
  EXPECT_TRUE(kcp_session->HasRemoteEndpoint());

  ASSERT_NE(client_->kcp_channel_, nullptr);
  EXPECT_TRUE(client_->kcp_channel_->IsConnected());

  // Step 8: Verify routing (MoveReq -> KCP, ChatReq -> TCP).
  mir2::common::MoveRequest move_req;
  move_req.target_x = 100;
  move_req.target_y = 200;
  MessageCodecStatus move_status = MessageCodecStatus::kOk;
  auto move_payload = mir2::common::EncodeMoveRequest(move_req, &move_status);
  ASSERT_EQ(move_status, MessageCodecStatus::kOk);
  ASSERT_FALSE(move_payload.empty());

  const uint16_t move_id = static_cast<uint16_t>(MsgId::kMoveReq);
  const uint16_t chat_id = static_cast<uint16_t>(MsgId::kChatReq);
  server_->RegisterHandler(
      move_id,
      [](const std::shared_ptr<mir2::network::TcpSession>&,
         const std::vector<uint8_t>&) {});
  server_->RegisterHandler(
      chat_id,
      [](const std::shared_ptr<mir2::network::TcpSession>&,
         const std::vector<uint8_t>&) {});
  auto chat_payload = BuildChatReqPayload("route-check");

  client_->send(move_id, move_payload);
  client_->send(chat_id, chat_payload);

  ASSERT_TRUE(WaitForConditionWithExtra(
      [&]() { return server_->GetReceivedCount(ChannelType::kKcp, move_id) >= 1; }, 1s,
      nullptr));
  ASSERT_TRUE(WaitForConditionWithExtra(
      [&]() { return server_->GetReceivedCount(ChannelType::kTcp, chat_id) >= 1; }, 1s,
      nullptr));

  // Step 9: Heartbeat observation (5s interval).
  const uint16_t heartbeat_id = static_cast<uint16_t>(MsgId::kKcpHeartbeat);
  const size_t heartbeat_before =
      server_->GetReceivedCount(ChannelType::kKcp, heartbeat_id);
  PumpFor(6s);
  const size_t heartbeat_after =
      server_->GetReceivedCount(ChannelType::kKcp, heartbeat_id);
  EXPECT_GT(heartbeat_after, heartbeat_before);

  // Conv uniqueness check with a second client.
  auto second_client = std::make_unique<DualChannelClient>();
  ASSERT_TRUE(ConnectAndLogin(second_client.get()));
  ASSERT_TRUE(WaitForKcpState(second_client.get(),
                              DualChannelClient::KcpUpgradeState::kConfirmed,
                              3s));

  ASSERT_TRUE(second_client->kcp_info_.valid);
  const uint32_t second_conv = second_client->kcp_info_.conv_id;
  ASSERT_NE(second_conv, 0u);
  EXPECT_NE(second_conv, conv_id);
  second_client->disconnect();
}

TEST_F(KcpHandshakeFailureIntegrationTest, HandshakeWithUdpFailure) {
  struct UpgradeAttemptState {
    std::mutex mutex;
    std::vector<std::chrono::steady_clock::time_point> times;
  };

  auto upgrade_state = std::make_shared<UpgradeAttemptState>();

  const uint16_t upgrade_id = static_cast<uint16_t>(MsgId::kKcpUpgradeRequest);
  const uint16_t move_id = static_cast<uint16_t>(MsgId::kMoveReq);
  server_->RegisterHandler(
      move_id,
      [](const std::shared_ptr<mir2::network::TcpSession>&,
         const std::vector<uint8_t>&) {});
  server_->SetMessageHook(
      [upgrade_id, upgrade_state_weak = std::weak_ptr<UpgradeAttemptState>(upgrade_state)](
          ChannelType channel,
          const std::shared_ptr<mir2::network::TcpSession>& /*session*/,
          uint16_t msg_id,
          const std::vector<uint8_t>& payload) {
    (void)payload;
    (void)channel;
    if (msg_id == upgrade_id) {
      if (auto upgrade_state_locked = upgrade_state_weak.lock()) {
        std::lock_guard<std::mutex> lock(upgrade_state_locked->mutex);
        upgrade_state_locked->times.push_back(std::chrono::steady_clock::now());
      }
    }
  });

  // Simulate UDP unreachable by dropping all KCP responses.
  server_->StopKcpResponses();

  PerformanceMonitor monitor;

  ASSERT_TRUE(ConnectAndLogin(client_.get()));

  const auto handshake_start = std::chrono::steady_clock::now();

  ASSERT_TRUE(WaitForKcpState(client_.get(),
                              DualChannelClient::KcpUpgradeState::kFailed,
                              20s));

  const auto fallback_time = std::chrono::steady_clock::now();
  monitor.RecordLatency(std::chrono::duration_cast<std::chrono::nanoseconds>(
      fallback_time - handshake_start));
  const auto fallback_stats = monitor.GetLatencyStats();
  EXPECT_GE(fallback_stats.max_ms, 14000.0);
  EXPECT_LT(fallback_stats.max_ms, 20000.0);
  EXPECT_EQ(client_->fallback_.GetState(), FallbackController::State::kFallback);

  const uint16_t heartbeat_id = static_cast<uint16_t>(MsgId::kKcpHeartbeat);
  const size_t heartbeat_count =
      server_->GetReceivedCount(ChannelType::kKcp, heartbeat_id);
  EXPECT_GE(heartbeat_count, 3u);

  // Verify move requests route to TCP after fallback.
  mir2::common::MoveRequest move_req;
  move_req.target_x = 10;
  move_req.target_y = 20;
  MessageCodecStatus move_status = MessageCodecStatus::kOk;
  auto move_payload = mir2::common::EncodeMoveRequest(move_req, &move_status);
  ASSERT_EQ(move_status, MessageCodecStatus::kOk);
  client_->send(move_id, move_payload);

  ASSERT_TRUE(WaitForConditionWithExtra(
      [&]() {
        return server_->GetReceivedCount(
                   ChannelType::kTcp,
                   move_id) >= 1;
      },
      2s, nullptr));

  // Verify there is at least one recovery attempt after fallback and
  // that it starts around the configured 30s interval.
  std::optional<std::chrono::steady_clock::time_point> recovery_attempt;
  ASSERT_TRUE(WaitForConditionWithExtra(
      [&]() {
        std::lock_guard<std::mutex> lock(upgrade_state->mutex);
        for (const auto& t : upgrade_state->times) {
          if (t > fallback_time) {
            recovery_attempt = t;
            return true;
          }
        }
        return false;
      },
      40s, nullptr));

  ASSERT_TRUE(recovery_attempt.has_value());
  const auto recovery_delay = std::chrono::duration_cast<std::chrono::milliseconds>(
      *recovery_attempt - fallback_time);
  EXPECT_NEAR(static_cast<double>(recovery_delay.count()), 30000.0, 2000.0);
}

}  // namespace
