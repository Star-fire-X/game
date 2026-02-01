/**
 * @file packet_codec.h
 * @brief 网络包编解码
 */

#ifndef MIR2_NETWORK_PACKET_CODEC_H
#define MIR2_NETWORK_PACKET_CODEC_H

#include "common/protocol/packet_codec.h"

namespace mir2::network {

using PacketHeader = mir2::common::PacketHeader;
using PacketHeaderV2 = mir2::common::PacketHeaderV2;
using Packet = mir2::common::NetworkPacket;
using DecodeStatus = mir2::common::DecodeStatus;
using ProtocolVersion = mir2::common::ProtocolVersion;

/**
 * @brief 编解码工具
 */
class PacketCodec {
 public:
  /**
   * @brief 编码网络包
   */
  static std::vector<uint8_t> Encode(uint16_t msg_id, const uint8_t* payload, size_t payload_size);

  /**
   * @brief 解码网络包
   */
  static DecodeStatus Decode(const uint8_t* data, size_t length, Packet* out_packet);

  /**
   * @brief 编码 V2 网络包
   */
  static std::vector<uint8_t> EncodeV2(uint16_t msg_id,
                                       const uint8_t* payload,
                                       size_t payload_size,
                                       uint16_t sequence,
                                       uint8_t flags = 0);

  /**
   * @brief 解码 V2 网络包
   */
  static DecodeStatus DecodeV2(const uint8_t* data,
                               size_t length,
                               Packet* out_packet,
                               uint16_t* out_sequence = nullptr,
                               uint8_t* out_flags = nullptr);
};

}  // namespace mir2::network

#endif  // MIR2_NETWORK_PACKET_CODEC_H
