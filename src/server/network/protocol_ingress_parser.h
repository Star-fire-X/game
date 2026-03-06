/**
 * @file protocol_ingress_parser.h
 * @brief Shared ingress frame parser for TCP/KCP packet streams.
 */

#ifndef MIR2_NETWORK_PROTOCOL_INGRESS_PARSER_H_
#define MIR2_NETWORK_PROTOCOL_INGRESS_PARSER_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/network/i_channel.h"
#include "network/packet_codec.h"

namespace mir2::network {

enum class IngressParseAction : uint8_t {
  kNeedMoreData = 0,
  kPacketReady,
  kFatalError,
};

enum class IngressParseError : uint8_t {
  kNone = 0,
  kLegacyV1,
  kHeaderInvalid,
  kPayloadTooLarge,
  kChannelMismatch,
  kDecodeFailed,
};

struct ParsedIngressPacket {
  Packet packet;
  uint16_t sequence = 0;
  uint8_t flags = 0;
};

struct IngressParseResult {
  IngressParseAction action = IngressParseAction::kNeedMoreData;
  IngressParseError error = IngressParseError::kNone;
  ParsedIngressPacket parsed;
};

/**
 * @brief Incremental parser for framed packets in a byte stream.
 */
class ProtocolIngressParser {
 public:
  explicit ProtocolIngressParser(mir2::common::ChannelType expected_channel);

  void Reset();
  void SetExpectedChannel(mir2::common::ChannelType expected_channel);
  void SetProtocolVersion(ProtocolVersion version, bool detected);

  ProtocolVersion GetProtocolVersion() const { return protocol_version_; }
  bool IsProtocolVersionDetected() const { return protocol_version_detected_; }

  /**
   * @brief Appends bytes into parser buffer with an upper bound guard.
   */
  bool AppendBytes(const uint8_t* data, size_t size, size_t max_buffer_size);

  /**
   * @brief Attempts to parse one packet from buffered bytes.
   */
  IngressParseResult NextPacket();

 private:
  size_t BufferedBytes() const;
  void ConsumeBytes(size_t bytes);
  void CompactReadBufferIfNeeded(size_t incoming_bytes, size_t max_buffer_size);

  mir2::common::ChannelType expected_channel_;
  std::vector<uint8_t> read_buffer_;
  size_t read_offset_ = 0;
  ProtocolVersion protocol_version_ = ProtocolVersion::kV2;
  bool protocol_version_detected_ = false;
};

}  // namespace mir2::network

#endif  // MIR2_NETWORK_PROTOCOL_INGRESS_PARSER_H_
