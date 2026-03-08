/**
 * @file packet_codec.h
 * @brief 统一网络包头和编解码工具
 *
 * 协议格式（Little Endian）：
 * - Magic: "MIR2" (0x4D495232)
 * - MsgId: uint16_t
 * - PayloadSize: uint32_t
 * - Payload: FlatBuffers bytes (0..16MB)
 */

#ifndef MIR2_COMMON_PROTOCOL_PACKET_CODEC_H
#define MIR2_COMMON_PROTOCOL_PACKET_CODEC_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/network/i_channel.h"

namespace mir2::common {

/**
 * @brief V2包头（16字节, 8字节对齐）
 */
struct PacketHeaderV2 {
    static constexpr uint32_t kMagic = 0x4D495233;  // "MIR3"
    static constexpr uint8_t kVersion = 0x01;
    // flags 位分配：
    // bit0: 压缩标志
    // bit1: 通道标识（1=KCP, 0=TCP）
    // bit2-7: 保留
    static constexpr uint8_t kFlagCompressed = 0x01;
    static constexpr uint8_t kFlagChannelKcp = 0x02;
    static constexpr size_t kSize = 16;

    uint32_t magic = kMagic;
    uint8_t version = kVersion;
    uint8_t flags = 0;
    uint16_t msg_id = 0;
    uint32_t payload_size = 0;
    uint16_t sequence = 0;
    uint16_t checksum = 0;

    std::array<uint8_t, kSize> ToBytes() const;
    static bool FromBytes(const uint8_t* data, size_t len, PacketHeaderV2* out);
};

/**
 * @brief 网络包数据（解码后）
 */
struct NetworkPacket {
    uint16_t msg_id = 0;
    std::vector<uint8_t> payload;
};

/**
 * @brief 单包最大负载（16MB）
 */
constexpr size_t kMaxPayloadSize = 16 * 1024 * 1024;

/**
 * @brief 解码结果
 */
enum class DecodeStatus : uint8_t {
    kOk = 0,
    kInvalidMagic,
    kInvalidVersion,
    kInvalidChecksum,
    kInvalidPayload,
    kPayloadTooLarge,
    kTruncated,
    kProtocolNotSupported,
    kDecompressFailed
};

/**
 * @brief CRC-16-CCITT (多项式0x1021, 初值0xFFFF)
 */
uint16_t CalcCRC16(const uint8_t* data, size_t length);

/**
 * @brief 编码 V2 网络包
 *
 * @return 编码后的字节序列；当 payload_size 超过限制时返回空 vector。
 */
std::vector<uint8_t> EncodePacketV2(uint16_t msg_id,
                                    const uint8_t* payload,
                                    size_t payload_size,
                                    uint16_t sequence,
                                    uint8_t flags = 0);

/**
 * @brief V2 解码网络包
 *
 * @return DecodeStatus 解码结果（Ok/InvalidMagic/InvalidVersion/InvalidChecksum/
 *                                  InvalidPayload/PayloadTooLarge/Truncated/
 *                                  ProtocolNotSupported/DecompressFailed）
 */
DecodeStatus DecodePacketV2(const uint8_t* data,
                            size_t length,
                            NetworkPacket* out_packet,
                            uint16_t* out_sequence = nullptr,
                            uint8_t* out_flags = nullptr);

/**
 * @brief 校验包头通道标志是否与实际通道匹配。
 */
bool ValidateChannelFlag(uint8_t flags, ChannelType actual_channel);

/**
 * @brief 协议版本检测
 */
enum class ProtocolVersion : uint8_t { kV1 = 1, kV2 = 2 };
ProtocolVersion DetectProtocolVersion(const uint8_t* magic_bytes);

}  // namespace mir2::common

#endif  // MIR2_COMMON_PROTOCOL_PACKET_CODEC_H
