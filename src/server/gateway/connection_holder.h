//
// @file connection_holder.h
// @brief Gateway connection holding state machine.
//

#ifndef MIR2_GATEWAY_CONNECTION_HOLDER_H_
#define MIR2_GATEWAY_CONNECTION_HOLDER_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/network/i_channel.h"
#include "gateway/ring_buffer.h"

namespace mir2::gateway {

class ConnectionHolder {
 public:
  enum class State : uint8_t {
    FORWARDING = 0,
    HOLDING = 1,
    RESTORING = 2,
    FLUSHING = 3
  };

  enum class Action : uint8_t {
    kForward = 0,
    kBuffered = 1,
    kDroppedRealtime = 2,
    kDisconnect = 3
  };

  static constexpr size_t kDefaultBufferCapacityBytes = 2 * 1024 * 1024;
  static constexpr size_t kDefaultDisconnectThresholdBytes = 5 * 1024 * 1024;

  explicit ConnectionHolder(
      size_t buffer_capacity_bytes = kDefaultBufferCapacityBytes,
      size_t disconnect_threshold_bytes = kDefaultDisconnectThresholdBytes);

  ConnectionHolder(const ConnectionHolder&) = delete;
  ConnectionHolder& operator=(const ConnectionHolder&) = delete;

  State GetState() const;

  bool EnterHolding();
  bool EnterRestoring();
  bool EnterFlushing();
  bool ReturnToForwarding();

  Action HandleClientMessage(uint16_t msg_id,
                             mir2::common::ChannelType channel,
                             const std::vector<uint8_t>& payload);

  bool PopBufferedMessage(BufferedMessage* out_message);

  size_t BufferedBytes() const;
  size_t BufferCapacityBytes() const;
  size_t DisconnectThresholdBytes() const;
  bool HasBufferedMessages() const;
  bool ShouldDisconnect() const;

 private:
  void ResetHoldingState();
  bool ShouldDropRealtime(uint16_t msg_id, mir2::common::ChannelType channel) const;

  State state_ = State::FORWARDING;
  RingBuffer buffer_;
  size_t holding_bytes_total_ = 0;
  size_t dropped_realtime_bytes_ = 0;
  size_t disconnect_threshold_bytes_ = kDefaultDisconnectThresholdBytes;
  bool should_disconnect_ = false;
};

}  // namespace mir2::gateway

#endif  // MIR2_GATEWAY_CONNECTION_HOLDER_H_
