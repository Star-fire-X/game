#ifndef MIR2_STORAGE_ENGINE_UTILS_PHASE0_GATE_H_
#define MIR2_STORAGE_ENGINE_UTILS_PHASE0_GATE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "storage_engine/storage_engine.h"

namespace mir2::storage_engine::utils {

struct Phase0BaselineSample {
  double p50_latency_ms = 0.0;
  double p95_latency_ms = 0.0;
  double p99_latency_ms = 0.0;
  double reject_rate = 0.0;
  size_t queue_depth = 0;
  uint64_t recovery_time_ms = 0;
};

struct Phase0GateThresholds {
  double max_p50_latency_ms = 5.0;
  double max_p95_latency_ms = 10.0;
  double max_p99_latency_ms = 20.0;
  double max_reject_rate = 0.01;
  size_t max_queue_depth = 1000;
  uint64_t max_recovery_time_ms = 30000;
};

struct Phase0GateResult {
  bool passed = false;
  std::vector<std::string> failure_reasons;
};

size_t ComputePhase0QueueDepth(const StorageEngine::HealthMetrics& metrics);

Phase0BaselineSample BuildPhase0BaselineSample(
    const StorageEngine::HealthMetrics& metrics,
    double p50_latency_ms,
    double p95_latency_ms,
    double p99_latency_ms,
    double reject_rate,
    uint64_t recovery_time_ms);

Phase0GateResult EvaluatePhase0Gate(const Phase0BaselineSample& sample,
                                    const Phase0GateThresholds& thresholds);

}  // namespace mir2::storage_engine::utils

#endif  // MIR2_STORAGE_ENGINE_UTILS_PHASE0_GATE_H_
