#include "network/packet_codec.h"

namespace mir2::network {

std::vector<uint8_t> PacketCodec::Encode(uint16_t msg_id, const uint8_t* payload, size_t payload_size) {
  return mir2::common::EncodePacket(msg_id, payload, payload_size);
}

DecodeStatus PacketCodec::Decode(const uint8_t* data, size_t length, Packet* out_packet) {
  return mir2::common::DecodePacket(data, length, out_packet);
}

std::vector<uint8_t> PacketCodec::EncodeV2(uint16_t msg_id,
                                           const uint8_t* payload,
                                           size_t payload_size,
                                           uint16_t sequence,
                                           uint8_t flags) {
  return mir2::common::EncodePacketV2(msg_id, payload, payload_size, sequence, flags);
}

DecodeStatus PacketCodec::DecodeV2(const uint8_t* data,
                                   size_t length,
                                   Packet* out_packet,
                                   uint16_t* out_sequence,
                                   uint8_t* out_flags) {
  return mir2::common::DecodePacketV2(data, length, out_packet, out_sequence, out_flags);
}

}  // namespace mir2::network
