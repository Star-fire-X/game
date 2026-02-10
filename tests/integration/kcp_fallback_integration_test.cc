/**
 * @file kcp_fallback_integration_test.cc
 * @brief Scenario 3 integration tests: UDP fallback and recovery.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <asio.hpp>

#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "integration/test_helpers.h"

// Expose private members for integration verification.
#define private public
#define protected public
#include "integration/kcp_integration_test_base.h"
#undef private
#undef protected

namespace {

using namespace std::chrono_literals;

using mir2::client::DualChannelClient;
using mir2::common::ChannelType;
using mir2::common::FallbackController;
using mir2::common::MsgId;
using mir2::test::integration::NetworkSimulator;

constexpr uint16_t kMoveMsgId = static_cast<uint16_t>(MsgId::kMoveReq);
constexpr uint16_t kUpgradeMsgId = static_cast<uint16_t>(MsgId::kKcpUpgradeRequest);
constexpr uint32_t kHandshakeTimeoutMs = 1000;
constexpr uint32_t kRecoveryIntervalMs = 300;
constexpr int kAttemptToleranceMs = 1200;
constexpr int kFallbackMessageCount = 12;

std::vector<uint8_t> BuildMovePayload(int x, int y) {
  mir2::common::MoveRequest move_req;
  move_req.target_x = x;
  move_req.target_y = y;
  mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
  auto payload = mir2::common::EncodeMoveRequest(move_req, &status);
  if (status != mir2::common::MessageCodecStatus::kOk) {
    return {};
  }
  return payload;
}

class SimulatedUdpTransport final : public mir2::client::IUdpTransport {
 public:
  SimulatedUdpTransport(asio::io_context& io_context, NetworkSimulator* simulator)
      : io_context_(io_context),
        simulator_(simulator),
        transport_(std::make_unique<mir2::client::UdpTransport>(io_context)) {
  }

  bool Bind(uint16_t port) override {
    return transport_->Bind(port);
  }

  void Close() override {
    transport_->Close();
  }

  bool IsOpen() const override {
    return transport_->IsOpen();
  }

  void StartReceive(ReceiveHandler handler) override {
    receive_handler_ = std::move(handler);
    transport_->StartReceive(
        [this](const asio::ip::udp::endpoint& endpoint, const uint8_t* data, size_t size) {
          if (!receive_handler_ || !data || size == 0) {
            return;
          }

          auto payload = std::vector<uint8_t>(data, data + size);
          auto deliver = [handler = receive_handler_, endpoint,
                          payload = std::move(payload)]() mutable {
            handler(endpoint, payload.data(), payload.size());
          };

          if (simulator_) {
            simulator_->Send(std::move(deliver));
          } else {
            deliver();
          }
        });
  }

  void SendTo(const asio::ip::udp::endpoint& endpoint,
              const uint8_t* data,
              size_t size) override {
    if (!data || size == 0) {
      return;
    }

    auto payload = std::vector<uint8_t>(data, data + size);
    auto deliver = [this, endpoint, payload = std::move(payload)]() mutable {
      transport_->SendTo(endpoint, payload.data(), payload.size());
    };

    if (simulator_) {
      simulator_->Send(std::move(deliver));
    } else {
      deliver();
    }
  }

 private:
  asio::io_context& io_context_;
  NetworkSimulator* simulator_ = nullptr;
  std::unique_ptr<mir2::client::UdpTransport> transport_;
  ReceiveHandler receive_handler_;
};

class KcpFallbackIntegrationTest : public mir2::test::integration::KcpIntegrationTestBase {
 protected:
  std::unique_ptr<DualChannelClient> CreateTestClient() override {
    mir2::common::KcpConfig config{};
    config.timeout_ms = kHandshakeTimeoutMs;
    config.recovery_interval_ms = kRecoveryIntervalMs;

    auto client = std::make_unique<DualChannelClient>(config);

    NetworkSimulator::Config sim_config{};
    sim_config.loss_rate = 0.0;
    sim_config.seed = 42;
    udp_simulator_ = std::make_unique<NetworkSimulator>(client->io_context_, sim_config);

    auto transport = std::make_unique<SimulatedUdpTransport>(client->io_context_,
                                                             udp_simulator_.get());
    if (client->kcp_channel_) {
      client->kcp_channel_->transport_ = std::move(transport);
    }

    return client;
  }

  void SetUp() override {
    KcpIntegrationTestBase::SetUp();
    attempt_tracker_ = std::make_shared<AttemptTracker>();
    RegisterMoveHandler();
    InstallUpgradeHook();
  }

  void TearDown() override {
    if (udp_simulator_) {
      udp_simulator_->SetLossRate(0.0);
      udp_simulator_->Flush();
    }
    attempt_tracker_.reset();
    KcpIntegrationTestBase::TearDown();
  }

  bool EstablishDualChannel() {
    if (!client_) {
      return false;
    }
    if (!client_->connect(kHost, kTcpPort)) {
      return false;
    }
    if (!WaitForCondition([this]() { return client_->is_connected(); }, 2s)) {
      return false;
    }
    return WaitForCondition(
        [this]() {
          return client_->get_kcp_state() == DualChannelClient::KcpUpgradeState::kConfirmed;
        },
        5s);
  }

  DualChannelClient::KcpSessionInfo SnapshotKcpInfo() const {
    std::lock_guard<std::mutex> lock(client_->kcp_info_mutex_);
    return client_->kcp_info_;
  }

  bool IsKcpAvailable() const {
    if (!client_ || !client_->kcp_channel_) {
      return false;
    }
    if (!client_->kcp_channel_->IsConnected()) {
      return false;
    }
    if (client_->get_kcp_state() != DualChannelClient::KcpUpgradeState::kConfirmed) {
      return false;
    }
    return client_->fallback_.IsKcpAllowed();
  }

  void SetUdpLossRate(double rate) {
    if (udp_simulator_) {
      udp_simulator_->SetLossRate(rate);
      udp_simulator_->Flush();
    }
  }

  void StartTrackingUpgradeAttempts() {
    if (!attempt_tracker_) {
      return;
    }
    std::lock_guard<std::mutex> lock(attempt_tracker_->mutex);
    attempt_tracker_->attempts.clear();
    attempt_tracker_->track.store(true, std::memory_order_relaxed);
  }

  size_t GetUpgradeAttemptCount() const {
    if (!attempt_tracker_) {
      return 0;
    }
    std::lock_guard<std::mutex> lock(attempt_tracker_->mutex);
    return attempt_tracker_->attempts.size();
  }

  std::vector<std::chrono::steady_clock::time_point> SnapshotAttempts() const {
    if (!attempt_tracker_) {
      return {};
    }
    std::lock_guard<std::mutex> lock(attempt_tracker_->mutex);
    return attempt_tracker_->attempts;
  }

  std::optional<std::chrono::steady_clock::time_point> WaitForFallbackTime(
      std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() <= deadline) {
      PumpOnce();
      if (client_->fallback_.GetState() == FallbackController::State::kFallback) {
        return std::chrono::steady_clock::now();
      }
      std::this_thread::sleep_for(5ms);
    }
    return std::nullopt;
  }

 private:
  void RegisterMoveHandler() {
    server_->RegisterHandler(
        kMoveMsgId,
        [](const std::shared_ptr<mir2::network::TcpSession>&,
           const std::vector<uint8_t>&) {});
  }

  void InstallUpgradeHook() {
    const auto tracker_weak = std::weak_ptr<AttemptTracker>(attempt_tracker_);
    server_->SetMessageHook(
        [tracker_weak](ChannelType channel,
                       const std::shared_ptr<mir2::network::TcpSession>&,
                       uint16_t msg_id,
                       const std::vector<uint8_t>&) {
          if (channel != ChannelType::kTcp || msg_id != kUpgradeMsgId) {
            return;
          }
          auto tracker = tracker_weak.lock();
          if (!tracker) {
            return;
          }
          if (!tracker->track.load(std::memory_order_relaxed)) {
            return;
          }
          std::lock_guard<std::mutex> lock(tracker->mutex);
          tracker->attempts.push_back(std::chrono::steady_clock::now());
        });
  }

  struct AttemptTracker {
    std::mutex mutex;
    std::vector<std::chrono::steady_clock::time_point> attempts;
    std::atomic<bool> track{false};
  };

  std::unique_ptr<NetworkSimulator> udp_simulator_;
  std::shared_ptr<AttemptTracker> attempt_tracker_;
};

TEST_F(KcpFallbackIntegrationTest, FallbackOnUdpTimeout) {
  ASSERT_TRUE(EstablishDualChannel());
  EXPECT_EQ(client_->fallback_.GetState(), FallbackController::State::kNormal);
  EXPECT_TRUE(IsKcpAvailable());

  const auto session_info = SnapshotKcpInfo();
  ASSERT_TRUE(session_info.valid);

  const auto payload = BuildMovePayload(100, 200);
  ASSERT_FALSE(payload.empty());

  const size_t kcp_before = server_->GetReceivedCount(ChannelType::kKcp, kMoveMsgId);
  client_->send(kMoveMsgId, payload);
  PumpFor(200ms);
  EXPECT_GT(server_->GetReceivedCount(ChannelType::kKcp, kMoveMsgId), kcp_before);

  SetUdpLossRate(1.0);
  client_->set_kcp_session(session_info.conv_id, session_info.token, session_info.udp_port);

  const auto start = std::chrono::steady_clock::now();
  const auto deadline = start + 5s;
  std::optional<std::chrono::steady_clock::time_point> fallback_time;
  while (std::chrono::steady_clock::now() < deadline) {
    PumpOnce();
    if (!fallback_time &&
        client_->fallback_.GetState() == FallbackController::State::kFallback) {
      fallback_time = std::chrono::steady_clock::now();
    }
    std::this_thread::sleep_for(5ms);
  }

  ASSERT_TRUE(fallback_time.has_value()) << "Fallback not detected within 5s.";
  const auto detection_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(*fallback_time - start).count();
  EXPECT_LT(detection_ms, 5000);
  EXPECT_EQ(client_->fallback_.GetState(), FallbackController::State::kFallback);

  const size_t tcp_before = server_->GetReceivedCount(ChannelType::kTcp, kMoveMsgId);
  const size_t kcp_after_block = server_->GetReceivedCount(ChannelType::kKcp, kMoveMsgId);

  for (int i = 0; i < kFallbackMessageCount; ++i) {
    client_->send(kMoveMsgId, payload);
    PumpFor(20ms);
  }
  PumpFor(200ms);

  const size_t tcp_after = server_->GetReceivedCount(ChannelType::kTcp, kMoveMsgId);
  const size_t kcp_after = server_->GetReceivedCount(ChannelType::kKcp, kMoveMsgId);

  EXPECT_EQ(tcp_after - tcp_before, static_cast<size_t>(kFallbackMessageCount));
  EXPECT_EQ(kcp_after, kcp_after_block);
}

TEST_F(KcpFallbackIntegrationTest, RecoveryWithExponentialBackoff) {
  ASSERT_TRUE(EstablishDualChannel());

  const auto session_info = SnapshotKcpInfo();
  ASSERT_TRUE(session_info.valid);

  SetUdpLossRate(1.0);
  client_->set_kcp_session(session_info.conv_id, session_info.token, session_info.udp_port);

  const auto fallback_time = WaitForFallbackTime(5s);
  ASSERT_TRUE(fallback_time.has_value()) << "Fallback not detected within 5s.";

  StartTrackingUpgradeAttempts();

  ASSERT_TRUE(WaitForCondition(
      [this]() {
        return client_->fallback_.GetState() == FallbackController::State::kRecovering;
      },
      5s, 20ms));

  ASSERT_TRUE(WaitForCondition([this]() { return GetUpgradeAttemptCount() >= 1; }, 5s, 20ms));
  const auto attempts_after1 = SnapshotAttempts();
  ASSERT_GE(attempts_after1.size(), 1u);
  const auto attempt1 = attempts_after1[0];

  ASSERT_TRUE(WaitForCondition([this]() { return GetUpgradeAttemptCount() >= 2; }, 8s, 20ms));
  const auto attempts_after2 = SnapshotAttempts();
  ASSERT_GE(attempts_after2.size(), 2u);
  const auto attempt2 = attempts_after2[1];

  PumpFor(std::chrono::milliseconds(kHandshakeTimeoutMs + 200));
  ASSERT_TRUE(WaitForCondition(
      [this]() { return client_->fallback_.GetState() == FallbackController::State::kFallback; },
      5s));

  SetUdpLossRate(0.0);

  ASSERT_TRUE(WaitForCondition([this]() { return GetUpgradeAttemptCount() >= 3; }, 8s, 20ms));
  const auto attempts_after3 = SnapshotAttempts();
  ASSERT_GE(attempts_after3.size(), 3u);
  const auto attempt3 = attempts_after3[2];

  const auto delta1 =
      std::chrono::duration_cast<std::chrono::milliseconds>(attempt1 - *fallback_time).count();
  const auto delta2 =
      std::chrono::duration_cast<std::chrono::milliseconds>(attempt2 - attempt1).count();
  const auto delta3 =
      std::chrono::duration_cast<std::chrono::milliseconds>(attempt3 - attempt2).count();

  EXPECT_NEAR(static_cast<double>(delta1),
              static_cast<double>(kRecoveryIntervalMs),
              kAttemptToleranceMs);
  EXPECT_NEAR(static_cast<double>(delta2),
              static_cast<double>(kHandshakeTimeoutMs + kRecoveryIntervalMs * 2),
              kAttemptToleranceMs);
  EXPECT_NEAR(static_cast<double>(delta3),
              static_cast<double>(kHandshakeTimeoutMs + kRecoveryIntervalMs * 4),
              kAttemptToleranceMs);

  ASSERT_TRUE(WaitForCondition(
      [this]() {
        return client_->get_kcp_state() == DualChannelClient::KcpUpgradeState::kConfirmed &&
               client_->fallback_.GetState() == FallbackController::State::kNormal;
      },
      10s));

  const auto payload = BuildMovePayload(120, 220);
  ASSERT_FALSE(payload.empty());
  const size_t kcp_before = server_->GetReceivedCount(ChannelType::kKcp, kMoveMsgId);
  const size_t tcp_before = server_->GetReceivedCount(ChannelType::kTcp, kMoveMsgId);

  client_->send(kMoveMsgId, payload);
  PumpFor(200ms);

  const size_t kcp_after = server_->GetReceivedCount(ChannelType::kKcp, kMoveMsgId);
  const size_t tcp_after = server_->GetReceivedCount(ChannelType::kTcp, kMoveMsgId);

  EXPECT_GT(kcp_after, kcp_before);
  EXPECT_EQ(tcp_after, tcp_before);
}

}  // namespace
