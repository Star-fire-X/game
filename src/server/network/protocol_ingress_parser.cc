#include "network/protocol_ingress_parser.h"

#include <cstring>

namespace mir2::network {

ProtocolIngressParser::ProtocolIngressParser(
    mir2::common::ChannelType expected_channel)
    : expected_channel_(expected_channel) {}

void ProtocolIngressParser::Reset() {
  read_buffer_.clear();
  read_offset_ = 0;
  protocol_version_ = ProtocolVersion::kV2;
  protocol_version_detected_ = false;
}

void ProtocolIngressParser::SetExpectedChannel(
    mir2::common::ChannelType expected_channel) {
  expected_channel_ = expected_channel;
}

void ProtocolIngressParser::SetProtocolVersion(ProtocolVersion version,
                                               bool detected) {
  protocol_version_ = version;
  protocol_version_detected_ = detected;
}

bool ProtocolIngressParser::AppendBytes(const uint8_t* data,
                                        size_t size,
                                        size_t max_buffer_size) {
  if (!data || size == 0) {
    return true;
  }
  CompactReadBufferIfNeeded(size, max_buffer_size);
  const size_t buffered = BufferedBytes();
  if (buffered >= max_buffer_size || size > max_buffer_size - buffered) {
    return false;
  }
  read_buffer_.insert(read_buffer_.end(), data, data + size);
  return true;
}

IngressParseResult ProtocolIngressParser::NextPacket() {
  IngressParseResult result{};
  const size_t buffered = BufferedBytes();
  if (buffered < sizeof(uint32_t)) {
    result.action = IngressParseAction::kNeedMoreData;
    return result;
  }

  const uint8_t* frame = read_buffer_.data() + read_offset_;
  if (!protocol_version_detected_) {
    protocol_version_ = mir2::common::DetectProtocolVersion(frame);
    protocol_version_detected_ = true;
  }
  if (protocol_version_ == ProtocolVersion::kV1) {
    result.action = IngressParseAction::kFatalError;
    result.error = IngressParseError::kLegacyV1;
    return result;
  }

  const size_t header_size = PacketHeaderV2::kSize;
  if (buffered < header_size) {
    result.action = IngressParseAction::kNeedMoreData;
    return result;
  }

  PacketHeaderV2 header{};
  if (!PacketHeaderV2::FromBytes(frame, header_size, &header) ||
      header.version != PacketHeaderV2::kVersion) {
    result.action = IngressParseAction::kFatalError;
    result.error = IngressParseError::kHeaderInvalid;
    return result;
  }

  if (header.payload_size > mir2::common::kMaxPayloadSize) {
    result.action = IngressParseAction::kFatalError;
    result.error = IngressParseError::kPayloadTooLarge;
    return result;
  }

  const size_t packet_size = header_size + static_cast<size_t>(header.payload_size);
  if (buffered < packet_size) {
    result.action = IngressParseAction::kNeedMoreData;
    return result;
  }

  if (!mir2::common::ValidateChannelFlag(header.flags, expected_channel_)) {
    result.action = IngressParseAction::kFatalError;
    result.error = IngressParseError::kChannelMismatch;
    return result;
  }

  const auto status = PacketCodec::DecodeV2(
      frame,
      packet_size,
      &result.parsed.packet,
      &result.parsed.sequence,
      &result.parsed.flags);
  if (status != DecodeStatus::kOk) {
    result.action = IngressParseAction::kFatalError;
    if (status == DecodeStatus::kPayloadTooLarge) {
      result.error = IngressParseError::kPayloadTooLarge;
    } else if (status == DecodeStatus::kProtocolNotSupported) {
      result.error = IngressParseError::kLegacyV1;
    } else {
      result.error = IngressParseError::kDecodeFailed;
    }
    return result;
  }

  ConsumeBytes(packet_size);
  result.action = IngressParseAction::kPacketReady;
  result.error = IngressParseError::kNone;
  return result;
}

size_t ProtocolIngressParser::BufferedBytes() const {
  if (read_buffer_.size() <= read_offset_) {
    return 0;
  }
  return read_buffer_.size() - read_offset_;
}

void ProtocolIngressParser::ConsumeBytes(size_t bytes) {
  read_offset_ += bytes;
  if (read_offset_ >= read_buffer_.size()) {
    read_buffer_.clear();
    read_offset_ = 0;
    return;
  }
  CompactReadBufferIfNeeded(/*incoming_bytes=*/0,
                            /*max_buffer_size=*/mir2::common::kMaxPayloadSize +
                                PacketHeaderV2::kSize);
}

void ProtocolIngressParser::CompactReadBufferIfNeeded(size_t incoming_bytes,
                                                      size_t max_buffer_size) {
  if (read_offset_ == 0) {
    return;
  }
  const size_t buffered = BufferedBytes();
  if (buffered == 0) {
    read_buffer_.clear();
    read_offset_ = 0;
    return;
  }

  if (read_offset_ < read_buffer_.size() / 2 &&
      read_buffer_.size() + incoming_bytes <= max_buffer_size) {
    return;
  }

  std::memmove(read_buffer_.data(),
               read_buffer_.data() + read_offset_,
               buffered);
  read_buffer_.resize(buffered);
  read_offset_ = 0;
}

}  // namespace mir2::network
