#include "gateway/ring_buffer.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace mir2::gateway {

RingBuffer::RingBuffer(size_t capacity_bytes)
    : buffer_(capacity_bytes, 0) {}

size_t RingBuffer::Capacity() const {
  return buffer_.size();
}

size_t RingBuffer::Size() const {
  return used_;
}

bool RingBuffer::Empty() const {
  return used_ == 0;
}

void RingBuffer::Clear() {
  head_ = 0;
  tail_ = 0;
  used_ = 0;
}

bool RingBuffer::Push(uint16_t msg_id,
                      mir2::common::ChannelType channel,
                      const std::vector<uint8_t>& payload) {
  if (buffer_.empty()) {
    return false;
  }
  if (payload.size() > std::numeric_limits<uint32_t>::max()) {
    return false;
  }

  const size_t record_size = kHeaderSize + payload.size();
  if (record_size > buffer_.size()) {
    return false;
  }
  if (buffer_.size() - used_ < record_size) {
    return false;
  }

  RecordHeader header{};
  header.payload_size = static_cast<uint32_t>(payload.size());
  header.msg_id = msg_id;
  header.channel_type = static_cast<uint8_t>(channel);

  // Keep Push transactional: either the whole record is committed or nothing is.
  const size_t original_head = head_;
  const size_t original_tail = tail_;
  const size_t original_used = used_;
  if (!WriteBytes(reinterpret_cast<const uint8_t*>(&header), kHeaderSize) ||
      (!payload.empty() && !WriteBytes(payload.data(), payload.size()))) {
    head_ = original_head;
    tail_ = original_tail;
    used_ = original_used;
    return false;
  }
  return true;
}

bool RingBuffer::Pop(BufferedMessage* out_message) {
  if (!out_message) {
    return false;
  }
  if (used_ < kHeaderSize) {
    return false;
  }

  RecordHeader header{};
  if (!ReadBytes(reinterpret_cast<uint8_t*>(&header), kHeaderSize)) {
    return false;
  }

  const size_t payload_size = header.payload_size;
  if (used_ < payload_size) {
    Clear();
    return false;
  }

  BufferedMessage message{};
  message.msg_id = header.msg_id;
  message.channel = static_cast<mir2::common::ChannelType>(header.channel_type);
  if (payload_size > 0) {
    message.payload.resize(payload_size);
    if (!ReadBytes(message.payload.data(), payload_size)) {
      Clear();
      return false;
    }
  }

  *out_message = std::move(message);
  return true;
}

bool RingBuffer::WriteBytes(const uint8_t* data, size_t len) {
  if (len == 0) {
    return true;
  }
  if (!data || len > buffer_.size() - used_) {
    return false;
  }

  const size_t first_chunk = std::min(len, buffer_.size() - tail_);
  std::memcpy(buffer_.data() + tail_, data, first_chunk);
  if (len > first_chunk) {
    std::memcpy(buffer_.data(), data + first_chunk, len - first_chunk);
  }

  tail_ = (tail_ + len) % buffer_.size();
  used_ += len;
  return true;
}

bool RingBuffer::ReadBytes(uint8_t* data, size_t len) {
  if (len == 0) {
    return true;
  }
  if (!data || len > used_) {
    return false;
  }

  const size_t first_chunk = std::min(len, buffer_.size() - head_);
  std::memcpy(data, buffer_.data() + head_, first_chunk);
  if (len > first_chunk) {
    std::memcpy(data + first_chunk, buffer_.data(), len - first_chunk);
  }

  head_ = (head_ + len) % buffer_.size();
  used_ -= len;
  return true;
}

}  // namespace mir2::gateway
