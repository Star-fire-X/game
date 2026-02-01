/**
 * @file metrics.h
 * @brief 监控指标
 */

#ifndef MIR2_MONITOR_METRICS_H
#define MIR2_MONITOR_METRICS_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "core/singleton.h"

#if defined(LEGEND2_ENABLE_PROMETHEUS)
namespace prometheus {
class Counter;
class Exposer;
class Gauge;
class Histogram;
class Registry;

template <typename T>
class Family;
}  // namespace prometheus
#endif

namespace mir2::monitor {

/**
 * @brief 监控指标管理器
 */
class Metrics : public core::Singleton<Metrics> {
 friend class core::Singleton<Metrics>;

 public:
  ~Metrics();
  void Init(uint16_t port);

  void SetConnections(int64_t value);
  void AddBytesIn(uint64_t bytes);
  void AddBytesOut(uint64_t bytes);
  void IncrementHeartbeatTimeouts();
  void IncrementMessagesSent();
  void IncrementMessagesReceived();
  void ObserveDispatchLatency(int64_t microseconds);
  void IncrementError(const std::string& reason);
  void IncrementCounter(const std::string& name);
  void SetGauge(const std::string& name, int64_t value);

  static constexpr const char* kConnections = "mir2_connections";
  static constexpr const char* kBytesIn = "mir2_bytes_in_total";
  static constexpr const char* kBytesOut = "mir2_bytes_out_total";
  static constexpr const char* kHeartbeatTimeouts = "mir2_heartbeat_timeouts_total";
  static constexpr const char* kMessagesSent = "mir2_messages_sent_total";
  static constexpr const char* kMessagesReceived = "mir2_messages_received_total";
  static constexpr const char* kDispatchLatency = "mir2_dispatch_latency_us";
  static constexpr const char* kErrors = "mir2_errors_total";

 private:
  Metrics() = default;

  mutable std::mutex mutex_;
#if defined(LEGEND2_ENABLE_PROMETHEUS)
  std::unique_ptr<prometheus::Exposer> exposer_;
  std::shared_ptr<prometheus::Registry> registry_;
  prometheus::Gauge* connections_ = nullptr;
  prometheus::Counter* bytes_in_ = nullptr;
  prometheus::Counter* bytes_out_ = nullptr;
  prometheus::Counter* heartbeat_timeouts_ = nullptr;
  prometheus::Counter* messages_sent_ = nullptr;
  prometheus::Counter* messages_received_ = nullptr;
  prometheus::Histogram* dispatch_latency_ = nullptr;
  prometheus::Family<prometheus::Counter>* error_family_ = nullptr;
  std::unordered_map<std::string, prometheus::Counter*> error_counters_;
  std::unordered_map<std::string, prometheus::Counter*> counters_;
  std::unordered_map<std::string, prometheus::Gauge*> gauges_;
#endif
};

}  // namespace mir2::monitor

#endif  // MIR2_MONITOR_METRICS_H
