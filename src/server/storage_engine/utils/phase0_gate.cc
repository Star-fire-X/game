#include "storage_engine/utils/phase0_gate.h"

namespace mir2::storage_engine::utils {

size_t ComputePhase0QueueDepth(const StorageEngine::HealthMetrics& metrics) {
  return metrics.high_priority_queue_depth + metrics.normal_priority_queue_depth +
         metrics.outbox_depth + metrics.dead_letter_depth;
}

Phase0BaselineSample BuildPhase0BaselineSample(
    const StorageEngine::HealthMetrics& metrics,
    double p50_latency_ms,
    double p95_latency_ms,
    double p99_latency_ms,
    double reject_rate,
    uint64_t recovery_time_ms) {
  return Phase0BaselineSample{
      .p50_latency_ms = p50_latency_ms,
      .p95_latency_ms = p95_latency_ms,
      .p99_latency_ms = p99_latency_ms,
      .reject_rate = reject_rate,
      .queue_depth = ComputePhase0QueueDepth(metrics),
      .recovery_time_ms = recovery_time_ms,
  };
}

Phase0GateResult EvaluatePhase0Gate(const Phase0BaselineSample& sample,
                                    const Phase0GateThresholds& thresholds) {
  Phase0GateResult result{};

  auto add_failure = [&result](const std::string& reason) {
    result.failure_reasons.push_back(reason);
  };

  if (sample.p50_latency_ms < 0.0 || sample.p95_latency_ms < 0.0 ||
      sample.p99_latency_ms < 0.0) {
    add_failure("latency metrics must be non-negative");
  }
  if (sample.reject_rate < 0.0 || sample.reject_rate > 1.0) {
    add_failure("reject_rate must be within [0, 1]");
  }

  if (sample.p50_latency_ms > thresholds.max_p50_latency_ms) {
    add_failure("p50 latency exceeded threshold");
  }
  if (sample.p95_latency_ms > thresholds.max_p95_latency_ms) {
    add_failure("p95 latency exceeded threshold");
  }
  if (sample.p99_latency_ms > thresholds.max_p99_latency_ms) {
    add_failure("p99 latency exceeded threshold");
  }
  if (sample.reject_rate > thresholds.max_reject_rate) {
    add_failure("reject rate exceeded threshold");
  }
  if (sample.queue_depth > thresholds.max_queue_depth) {
    add_failure("queue depth exceeded threshold");
  }
  if (sample.recovery_time_ms > thresholds.max_recovery_time_ms) {
    add_failure("recovery time exceeded threshold");
  }

  result.passed = result.failure_reasons.empty();
  return result;
}

}  // namespace mir2::storage_engine::utils
