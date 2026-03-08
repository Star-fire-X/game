/**
 * @file metrics_sink.h
 * @brief Metrics sink abstraction to decouple metrics backend from callers.
 */

#ifndef MIR2_MONITOR_METRICS_SINK_H_
#define MIR2_MONITOR_METRICS_SINK_H_

#include <cstdint>
#include <memory>
#include <string>

#include "monitor/metrics.h"

namespace mir2::monitor {

class IMetricsSink {
 public:
  virtual ~IMetricsSink() = default;

  virtual void IncrementCounter(const std::string& name) = 0;
  virtual void IncrementCounter(const std::string& name, uint64_t delta) = 0;
  virtual void SetGauge(const std::string& name, double value) = 0;
};

class DefaultMetricsSink final : public IMetricsSink {
 public:
  void IncrementCounter(const std::string& name) override {
    Metrics::Instance().IncrementCounter(name);
  }

  void IncrementCounter(const std::string& name, uint64_t delta) override {
    Metrics::Instance().IncrementCounter(name, delta);
  }

  void SetGauge(const std::string& name, double value) override {
    Metrics::Instance().SetGauge(name, value);
  }
};

inline std::shared_ptr<IMetricsSink> CreateDefaultMetricsSink() {
  static std::shared_ptr<IMetricsSink> sink = std::make_shared<DefaultMetricsSink>();
  return sink;
}

}  // namespace mir2::monitor

#endif  // MIR2_MONITOR_METRICS_SINK_H_
