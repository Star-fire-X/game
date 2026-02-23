//
// @file ring_buffer.h
// @brief Gateway ring buffer for holding client messages.
//

#ifndef MIR2_GATEWAY_RING_BUFFER_H_
#define MIR2_GATEWAY_RING_BUFFER_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/network/i_channel.h"

namespace mir2::gateway {

struct BufferedMessage {
  uint16_t msg_id = 0;
  mir2::common::ChannelType channel = mir2::common::ChannelType::kTcp;
  std::vector<uint8_t> payload;
};

class RingBuffer {
 public:
  explicit RingBuffer(size_t capacity_bytes);

  RingBuffer(const RingBuffer&) = delete;
  RingBuffer& operator=(const RingBuffer&) = delete;

  size_t Capacity() const;
  size_t Size() const;
  bool Empty() const;

  void Clear();

  bool Push(uint16_t msg_id,
            mir2::common::ChannelType channel,
            const std::vector<uint8_t>& payload);
  bool Pop(BufferedMessage* out_message);

 private:
  struct RecordHeader {
    uint32_t payload_size = 0;
    uint16_t msg_id = 0;
    uint8_t channel_type = 0;
    uint8_t reserved = 0;
  };
  static constexpr size_t kHeaderSize = sizeof(RecordHeader);

  bool WriteBytes(const uint8_t* data, size_t len);
  bool ReadBytes(uint8_t* data, size_t len);

  std::vector<uint8_t> buffer_;
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t used_ = 0;
};

}  // namespace mir2::gateway

#endif  // MIR2_GATEWAY_RING_BUFFER_H_
