#include <gtest/gtest.h>

#include "storage_engine/utils/phase0_gate.h"

namespace mir2::storage_engine::utils {
namespace {

TEST(StoragePhase0GateTest, ComputeQueueDepthAggregatesAllQueueLayers) {
  StorageEngine::HealthMetrics metrics{};
  metrics.high_priority_queue_depth = 3;
  metrics.normal_priority_queue_depth = 5;
  metrics.outbox_depth = 7;
  metrics.dead_letter_depth = 11;

  EXPECT_EQ(ComputePhase0QueueDepth(metrics), 26U);
}

TEST(StoragePhase0GateTest, BuildBaselineSampleUsesAggregatedQueueDepth) {
  StorageEngine::HealthMetrics metrics{};
  metrics.high_priority_queue_depth = 10;
  metrics.normal_priority_queue_depth = 20;
  metrics.outbox_depth = 1;
  metrics.dead_letter_depth = 2;

  const auto sample =
      BuildPhase0BaselineSample(metrics, 1.0, 2.0, 3.0, 0.005, 1200);

  EXPECT_DOUBLE_EQ(sample.p50_latency_ms, 1.0);
  EXPECT_DOUBLE_EQ(sample.p95_latency_ms, 2.0);
  EXPECT_DOUBLE_EQ(sample.p99_latency_ms, 3.0);
  EXPECT_DOUBLE_EQ(sample.reject_rate, 0.005);
  EXPECT_EQ(sample.queue_depth, 33U);
  EXPECT_EQ(sample.recovery_time_ms, 1200U);
}

TEST(StoragePhase0GateTest, EvaluatePassesWhenAllSignalsMeetThresholds) {
  Phase0BaselineSample sample{};
  sample.p50_latency_ms = 1.0;
  sample.p95_latency_ms = 2.0;
  sample.p99_latency_ms = 4.0;
  sample.reject_rate = 0.001;
  sample.queue_depth = 100;
  sample.recovery_time_ms = 500;

  Phase0GateThresholds thresholds{};
  thresholds.max_p50_latency_ms = 5.0;
  thresholds.max_p95_latency_ms = 10.0;
  thresholds.max_p99_latency_ms = 20.0;
  thresholds.max_reject_rate = 0.01;
  thresholds.max_queue_depth = 500;
  thresholds.max_recovery_time_ms = 1000;

  const auto result = EvaluatePhase0Gate(sample, thresholds);
  EXPECT_TRUE(result.passed);
  EXPECT_TRUE(result.failure_reasons.empty());
}

TEST(StoragePhase0GateTest, EvaluateReturnsFailureReasonsForBreachedSignals) {
  Phase0BaselineSample sample{};
  sample.p50_latency_ms = 10.0;
  sample.p95_latency_ms = 20.0;
  sample.p99_latency_ms = 40.0;
  sample.reject_rate = 0.02;
  sample.queue_depth = 1000;
  sample.recovery_time_ms = 60000;

  Phase0GateThresholds thresholds{};
  thresholds.max_p50_latency_ms = 5.0;
  thresholds.max_p95_latency_ms = 15.0;
  thresholds.max_p99_latency_ms = 30.0;
  thresholds.max_reject_rate = 0.01;
  thresholds.max_queue_depth = 500;
  thresholds.max_recovery_time_ms = 30000;

  const auto result = EvaluatePhase0Gate(sample, thresholds);

  EXPECT_FALSE(result.passed);
  EXPECT_EQ(result.failure_reasons.size(), 6U);
}

TEST(StoragePhase0GateTest, EvaluateRejectsInvalidInputDomain) {
  Phase0BaselineSample sample{};
  sample.p50_latency_ms = -1.0;
  sample.p95_latency_ms = 2.0;
  sample.p99_latency_ms = 3.0;
  sample.reject_rate = -0.1;
  sample.queue_depth = 1;
  sample.recovery_time_ms = 1;

  const auto result = EvaluatePhase0Gate(sample, Phase0GateThresholds{});

  EXPECT_FALSE(result.passed);
  ASSERT_FALSE(result.failure_reasons.empty());
}

}  // namespace
}  // namespace mir2::storage_engine::utils
