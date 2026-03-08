#include "monitor/metrics.h"

namespace mir2::monitor {

Metrics::~Metrics() = default;

void Metrics::Init(uint16_t) {}

void Metrics::SetConnections(int64_t) {}
void Metrics::AddBytesIn(uint64_t) {}
void Metrics::AddBytesOut(uint64_t) {}
void Metrics::IncrementHeartbeatTimeouts() {}
void Metrics::IncrementMessagesSent() {}
void Metrics::IncrementMessagesReceived() {}
void Metrics::ObserveDispatchLatency(int64_t) {}
void Metrics::ObserveHandlerLatency(double) {}
void Metrics::IncrementError(const std::string&) {}
void Metrics::IncrementCounter(const std::string&) {}
void Metrics::IncrementCounter(const std::string&, uint64_t) {}
void Metrics::SetGauge(const std::string&, double) {}

}  // namespace mir2::monitor
