#include "gateway/connection_holder.h"

#include "common/enums.h"
#include "monitor/metrics.h"

namespace mir2::gateway {

namespace {
constexpr const char* kBufferSizeMetric = "gateway.holding.buffer_size_bytes";
constexpr const char* kDroppedRealtimeMetric = "gateway.holding.dropped_realtime";
}

ConnectionHolder::ConnectionHolder(size_t buffer_capacity_bytes,
                                   size_t disconnect_threshold_bytes)
    : buffer_(buffer_capacity_bytes),
      disconnect_threshold_bytes_(disconnect_threshold_bytes) {
  UpdateBufferGauge();
}

ConnectionHolder::State ConnectionHolder::GetState() const {
  return state_;
}

bool ConnectionHolder::EnterHolding() {
  if (state_ != State::FORWARDING) {
    return false;
  }
  state_ = State::HOLDING;
  ResetHoldingState();
  return true;
}

bool ConnectionHolder::EnterRestoring() {
  if (state_ != State::HOLDING) {
    return false;
  }
  state_ = State::RESTORING;
  return true;
}

bool ConnectionHolder::EnterFlushing() {
  if (state_ != State::RESTORING) {
    return false;
  }
  state_ = State::FLUSHING;
  return true;
}

bool ConnectionHolder::ReturnToForwarding() {
  if (state_ != State::FLUSHING) {
    return false;
  }
  state_ = State::FORWARDING;
  ResetHoldingState();
  return true;
}

ConnectionHolder::Action ConnectionHolder::HandleClientMessage(
    uint16_t msg_id,
    mir2::common::ChannelType channel,
    const std::vector<uint8_t>& payload) {
  if (state_ == State::FORWARDING) {
    return Action::kForward;
  }

  if (should_disconnect_) {
    return Action::kDisconnect;
  }

  holding_bytes_total_ += payload.size();
  if (disconnect_threshold_bytes_ > 0 &&
      holding_bytes_total_ > disconnect_threshold_bytes_) {
    should_disconnect_ = true;
    return Action::kDisconnect;
  }

  if (ShouldDropRealtime(msg_id, channel)) {
    dropped_realtime_bytes_ += payload.size();
    monitor::Metrics::Instance().IncrementCounter(kDroppedRealtimeMetric);
    return Action::kDroppedRealtime;
  }

  if (!buffer_.Push(msg_id, channel, payload)) {
    should_disconnect_ = true;
    return Action::kDisconnect;
  }

  UpdateBufferGauge();
  return Action::kBuffered;
}

bool ConnectionHolder::PopBufferedMessage(BufferedMessage* out_message) {
  if (!buffer_.Pop(out_message)) {
    return false;
  }
  UpdateBufferGauge();
  return true;
}

size_t ConnectionHolder::BufferedBytes() const {
  return buffer_.Size();
}

size_t ConnectionHolder::BufferCapacityBytes() const {
  return buffer_.Capacity();
}

size_t ConnectionHolder::DisconnectThresholdBytes() const {
  return disconnect_threshold_bytes_;
}

bool ConnectionHolder::HasBufferedMessages() const {
  return !buffer_.Empty();
}

bool ConnectionHolder::ShouldDisconnect() const {
  return should_disconnect_;
}

void ConnectionHolder::ResetHoldingState() {
  buffer_.Clear();
  holding_bytes_total_ = 0;
  dropped_realtime_bytes_ = 0;
  should_disconnect_ = false;
  UpdateBufferGauge();
}

bool ConnectionHolder::ShouldDropRealtime(uint16_t msg_id,
                                          mir2::common::ChannelType channel) const {
  if (channel == mir2::common::ChannelType::kKcp) {
    return true;
  }
  if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kMoveReq)) {
    return true;
  }
  return false;
}

void ConnectionHolder::UpdateBufferGauge() {
  monitor::Metrics::Instance().SetGauge(kBufferSizeMetric,
                                        static_cast<int64_t>(buffer_.Size()));
}

}  // namespace mir2::gateway
