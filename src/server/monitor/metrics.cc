#include "monitor/metrics.h"

#include <vector>

#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>

namespace mir2::monitor {

namespace {

std::vector<double> BuildLatencyBuckets() {
  return {50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000};
}

std::string NormalizeMetricName(const std::string& name) {
  std::string normalized = name;
  for (char& ch : normalized) {
    if (ch == '.') {
      ch = '_';
    }
  }
  return normalized;
}

}  // namespace

Metrics::~Metrics() = default;

void Metrics::Init(uint16_t port) {
  if (port == 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (registry_) {
    return;
  }

  registry_ = std::make_shared<prometheus::Registry>();
  exposer_ = std::make_unique<prometheus::Exposer>("0.0.0.0:" + std::to_string(port));
  exposer_->RegisterCollectable(registry_);

  auto& connections_family = prometheus::BuildGauge()
                                 .Name(kConnections)
                                 .Help("Active TCP connections.")
                                 .Register(*registry_);
  connections_ = &connections_family.Add({});

  auto& bytes_in_family = prometheus::BuildCounter()
                              .Name(kBytesIn)
                              .Help("Total bytes received.")
                              .Register(*registry_);
  bytes_in_ = &bytes_in_family.Add({});

  auto& bytes_out_family = prometheus::BuildCounter()
                               .Name(kBytesOut)
                               .Help("Total bytes sent.")
                               .Register(*registry_);
  bytes_out_ = &bytes_out_family.Add({});

  auto& heartbeat_family = prometheus::BuildCounter()
                               .Name(kHeartbeatTimeouts)
                               .Help("Total heartbeat timeouts.")
                               .Register(*registry_);
  heartbeat_timeouts_ = &heartbeat_family.Add({});

  auto& messages_sent_family = prometheus::BuildCounter()
                                   .Name(kMessagesSent)
                                   .Help("Total messages sent.")
                                   .Register(*registry_);
  messages_sent_ = &messages_sent_family.Add({});

  auto& messages_received_family = prometheus::BuildCounter()
                                       .Name(kMessagesReceived)
                                       .Help("Total messages received.")
                                       .Register(*registry_);
  messages_received_ = &messages_received_family.Add({});

  auto& latency_family = prometheus::BuildHistogram()
                             .Name(kDispatchLatency)
                             .Help("Message dispatch latency in microseconds.")
                             .Register(*registry_);
  dispatch_latency_ = &latency_family.Add({}, BuildLatencyBuckets());

  error_family_ = &prometheus::BuildCounter()
                       .Name(kErrors)
                       .Help("Total error count by reason.")
                       .Register(*registry_);
}

void Metrics::SetConnections(int64_t value) {
  if (!connections_) {
    return;
  }
  connections_->Set(static_cast<double>(value));
}

void Metrics::AddBytesIn(uint64_t bytes) {
  if (!bytes_in_) {
    return;
  }
  bytes_in_->Increment(static_cast<double>(bytes));
}

void Metrics::AddBytesOut(uint64_t bytes) {
  if (!bytes_out_) {
    return;
  }
  bytes_out_->Increment(static_cast<double>(bytes));
}

void Metrics::IncrementHeartbeatTimeouts() {
  if (!heartbeat_timeouts_) {
    return;
  }
  heartbeat_timeouts_->Increment();
}

void Metrics::IncrementMessagesSent() {
  if (!messages_sent_) {
    return;
  }
  messages_sent_->Increment();
}

void Metrics::IncrementMessagesReceived() {
  if (!messages_received_) {
    return;
  }
  messages_received_->Increment();
}

void Metrics::ObserveDispatchLatency(int64_t microseconds) {
  if (!dispatch_latency_) {
    return;
  }
  dispatch_latency_->Observe(static_cast<double>(microseconds));
}

void Metrics::IncrementError(const std::string& reason) {
  if (!error_family_) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = error_counters_.find(reason);
  if (it == error_counters_.end()) {
    auto& counter = error_family_->Add({{"reason", reason}});
    it = error_counters_.emplace(reason, &counter).first;
  }
  it->second->Increment();
}

void Metrics::IncrementCounter(const std::string& name) {
  if (!registry_) {
    return;
  }

  const std::string normalized = NormalizeMetricName(name);
  prometheus::Counter* counter = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = counters_.find(normalized);
    if (it == counters_.end()) {
      auto& family = prometheus::BuildCounter()
                         .Name(normalized)
                         .Help("Custom counter.")
                         .Register(*registry_);
      it = counters_.emplace(normalized, &family.Add({})).first;
    }
    counter = it->second;
  }

  if (counter) {
    counter->Increment();
  }
}

void Metrics::SetGauge(const std::string& name, int64_t value) {
  if (!registry_) {
    return;
  }

  const std::string normalized = NormalizeMetricName(name);
  prometheus::Gauge* gauge = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = gauges_.find(normalized);
    if (it == gauges_.end()) {
      auto& family = prometheus::BuildGauge()
                         .Name(normalized)
                         .Help("Custom gauge.")
                         .Register(*registry_);
      it = gauges_.emplace(normalized, &family.Add({})).first;
    }
    gauge = it->second;
  }

  if (gauge) {
    gauge->Set(static_cast<double>(value));
  }
}

}  // namespace mir2::monitor
