#include <gtest/gtest.h>

#include "common/network/fallback_controller.h"

namespace {

using mir2::common::FallbackController;

}  // namespace

TEST(FallbackControllerTest, HandshakeTimeoutTransitionsToFallback) {
  FallbackController::Config config{};
  config.handshake_timeout_ms = 100;
  config.recovery_interval_ms = 500;
  config.max_recovery_interval_ms = 2000;

  FallbackController controller(config);
  controller.StartHandshake(0);

  EXPECT_FALSE(controller.IsHandshakeTimedOut(50));
  EXPECT_TRUE(controller.IsHandshakeTimedOut(100));

  controller.OnKcpHandshakeTimeout(100);
  EXPECT_EQ(controller.GetState(), FallbackController::State::kFallback);
  EXPECT_FALSE(controller.ShouldAttemptRecovery(400));
  EXPECT_TRUE(controller.ShouldAttemptRecovery(600));
}

TEST(FallbackControllerTest, RecoveryBackoffDoublesAndCaps) {
  FallbackController::Config config{};
  config.handshake_timeout_ms = 100;
  config.recovery_interval_ms = 200;
  config.max_recovery_interval_ms = 500;

  FallbackController controller(config);
  controller.OnKcpDisconnect(0);
  EXPECT_EQ(controller.GetState(), FallbackController::State::kFallback);
  EXPECT_TRUE(controller.ShouldAttemptRecovery(200));

  controller.OnRecoveryAttempt(200);
  controller.OnKcpHandshakeTimeout(250);
  EXPECT_EQ(controller.GetState(), FallbackController::State::kFallback);
  EXPECT_FALSE(controller.ShouldAttemptRecovery(400));
  EXPECT_TRUE(controller.ShouldAttemptRecovery(700));

  controller.OnRecoveryAttempt(700);
  controller.OnKcpHandshakeTimeout(750);
  EXPECT_TRUE(controller.ShouldAttemptRecovery(1250));
}

TEST(FallbackControllerTest, HandshakeSuccessResetsState) {
  FallbackController controller;
  controller.StartHandshake(0);
  controller.OnKcpHandshakeSuccess();
  EXPECT_EQ(controller.GetState(), FallbackController::State::kNormal);
  EXPECT_FALSE(controller.IsHandshakeTimedOut(1000));
}
