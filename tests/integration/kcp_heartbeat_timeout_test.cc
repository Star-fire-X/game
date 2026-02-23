#include <gtest/gtest.h>

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>

#define private public
#include "client/network/dual_channel_client.h"
#undef private

#include "common/time_utils.h"
#include "integration/kcp_integration_test_base.h"

namespace mir2::test::integration {
namespace {

using mir2::client::DualChannelClient;
using namespace std::chrono_literals;

constexpr int64_t kHeartbeatIntervalMs = 5000;
constexpr int64_t kHeartbeatTimeoutMs = 15000;
constexpr int64_t kTimeoutToleranceMs = 1000;

}  // namespace

class KcpHeartbeatTimeoutTest : public KcpIntegrationTestBase {
 protected:
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
          return client_->kcp_channel_ && client_->kcp_channel_->IsConnected() &&
                 client_->kcp_state_.load(std::memory_order_relaxed) ==
                     DualChannelClient::KcpUpgradeState::kConfirmed;
        },
        6s);
  }

  bool WaitForHeartbeatAck(std::chrono::milliseconds timeout) {
    return WaitForConditionWithTimer(
        [this]() { return LastHeartbeatAckMs() > 0; }, timeout);
  }

  bool WaitForHeartbeatSendAdvance(int64_t previous_send_ms,
                                   std::chrono::milliseconds timeout) {
    return WaitForConditionWithTimer(
        [this, previous_send_ms]() {
          return LastHeartbeatSendMs() > previous_send_ms;
        },
        timeout);
  }

  bool WaitForConditionWithTimer(const std::function<bool()>& predicate,
                                 std::chrono::milliseconds timeout,
                                 std::chrono::milliseconds poll_interval =
                                     std::chrono::milliseconds(10)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (predicate()) {
        return true;
      }
      PumpOnce();
      std::this_thread::sleep_for(poll_interval);
    }
    return predicate();
  }

  void RunFor(std::chrono::milliseconds duration,
              std::chrono::milliseconds poll_interval =
                  std::chrono::milliseconds(10)) {
    WaitForConditionWithTimer([]() { return false; }, duration, poll_interval);
  }

  int64_t LastHeartbeatSendMs() const {
    return client_->kcp_upgrade_handler_.last_heartbeat_send_ms_;
  }

  int64_t LastHeartbeatAckMs() const {
    return client_->kcp_upgrade_handler_.last_heartbeat_ack_ms_;
  }

  bool HeartbeatTimedOut() const {
    return client_->kcp_upgrade_handler_.heartbeat_timed_out_;
  }

  void InstallDisconnectProbe(std::atomic<bool>* flag,
                              std::atomic<int64_t>* time_ms) {
    client_->kcp_upgrade_handler_.SetDisconnectCallback([this, flag, time_ms]() {
      if (time_ms) {
        time_ms->store(mir2::common::now_ms(), std::memory_order_relaxed);
      }
      if (flag) {
        flag->store(true, std::memory_order_relaxed);
      }
      client_->notify_kcp_disconnect();
    });
  }
};

TEST_F(KcpHeartbeatTimeoutTest, HeartbeatTimeoutDetection) {
  using namespace std::chrono_literals;

  ASSERT_TRUE(EstablishDualChannel());
  ASSERT_TRUE(WaitForHeartbeatAck(6s));

  std::atomic<bool> disconnect_triggered{false};
  std::atomic<int64_t> disconnect_time_ms{0};
  InstallDisconnectProbe(&disconnect_triggered, &disconnect_time_ms);

  const int64_t initial_send_ms = LastHeartbeatSendMs();

  server_->StopHeartbeatResponses();
  RunFor(1s);

  const int64_t initial_ack_ms = LastHeartbeatAckMs();

  ASSERT_TRUE(WaitForHeartbeatSendAdvance(initial_send_ms, 6s));
  RunFor(12s);

  EXPECT_FALSE(disconnect_triggered.load(std::memory_order_relaxed));
  EXPECT_EQ(LastHeartbeatAckMs(), initial_ack_ms);
  EXPECT_GT(LastHeartbeatSendMs(), initial_send_ms);
  EXPECT_EQ(client_->kcp_state_.load(std::memory_order_relaxed),
            DualChannelClient::KcpUpgradeState::kConfirmed);

  const int64_t last_send_before_block = LastHeartbeatSendMs();
  client_->kcp_channel_->Disconnect();

  ASSERT_TRUE(WaitForConditionWithTimer(
      [&disconnect_triggered]() {
        return disconnect_triggered.load(std::memory_order_relaxed);
      },
      20s));

  const int64_t elapsed_ms = disconnect_time_ms.load(std::memory_order_relaxed) -
                             last_send_before_block;
  EXPECT_GE(elapsed_ms, kHeartbeatTimeoutMs - kTimeoutToleranceMs);
  EXPECT_LE(elapsed_ms, kHeartbeatTimeoutMs + kTimeoutToleranceMs);

  EXPECT_EQ(client_->kcp_state_.load(std::memory_order_relaxed),
            DualChannelClient::KcpUpgradeState::kFailed);
}

TEST_F(KcpHeartbeatTimeoutTest, NoFalsePositiveWithoutAck) {
  using namespace std::chrono_literals;

  ASSERT_TRUE(EstablishDualChannel());
  ASSERT_TRUE(WaitForHeartbeatAck(6s));

  std::atomic<bool> disconnect_triggered{false};
  InstallDisconnectProbe(&disconnect_triggered, nullptr);

  server_->StopHeartbeatResponses();
  RunFor(1s);

  const int64_t ack_before = LastHeartbeatAckMs();

  RunFor(16s);

  EXPECT_FALSE(disconnect_triggered.load(std::memory_order_relaxed));
  EXPECT_TRUE(client_->kcp_channel_->IsConnected());
  EXPECT_EQ(client_->kcp_state_.load(std::memory_order_relaxed),
            DualChannelClient::KcpUpgradeState::kConfirmed);

  const int64_t now_ms = mir2::common::now_ms();
  const int64_t ack_age = now_ms - LastHeartbeatAckMs();
  const int64_t send_age = now_ms - LastHeartbeatSendMs();

  EXPECT_EQ(LastHeartbeatAckMs(), ack_before);
  // Pre-M-4 behavior (ack-based timeout) would have tripped here.
  EXPECT_GE(ack_age, kHeartbeatTimeoutMs);
  EXPECT_LT(send_age, kHeartbeatIntervalMs + kTimeoutToleranceMs);
  EXPECT_FALSE(HeartbeatTimedOut());
}

}  // namespace mir2::test::integration
