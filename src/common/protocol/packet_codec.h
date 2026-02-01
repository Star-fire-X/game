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

namespace mir2::common {

/**
 * @brief 网络包头（10字节）
 *
 * 布局：
 * [0..3]  magic ("MIR2")
 * [4..5]  msg_id
 * [6..9]  payload_size
 */
struct PacketHeader {
    static constexpr uint32_t kMagic = 0x4D495232;  // "MIR2"
    static constexpr size_t kSize = 10;

    uint32_t magic = kMagic;
    uint16_t msg_id = 0;
    uint32_t payload_size = 0;

    std::array<uint8_t, kSize> ToBytes() const;
    static bool FromBytes(const uint8_t* data, size_t len, PacketHeader* out_header);
};

/**
 * @brief V2包头（16字节, 8字节对齐）
 */
struct PacketHeaderV2 {
    static constexpr uint32_t kMagic = 0x4D495233;  // "MIR3"
    static constexpr uint8_t kVersion = 0x01;
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
    kPayloadTooLarge,
    kTruncated
};

/**
 * @brief CRC-16-CCITT (多项式0x1021, 初值0xFFFF)
 */
uint16_t CalcCRC16(const uint8_t* data, size_t length);

/**
 * @brief 编码网络包
 *
 * @return 编码后的字节序列；当 payload_size 超过限制时返回空 vector。
 */
std::vector<uint8_t> EncodePacket(uint16_t msg_id, const uint8_t* payload, size_t payload_size);

/**
 * @brief 解码网络包
 *
 * @return DecodeStatus 解码结果（Ok/InvalidMagic/InvalidVersion/InvalidChecksum/PayloadTooLarge/Truncated）
 */
DecodeStatus DecodePacket(const uint8_t* data, size_t length, NetworkPacket* out_packet);

/**
 * @brief V2 编码网络包
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
 * @return DecodeStatus 解码结果（Ok/InvalidMagic/InvalidVersion/InvalidChecksum/PayloadTooLarge/Truncated）
 */
DecodeStatus DecodePacketV2(const uint8_t* data,
                            size_t length,
                            NetworkPacket* out_packet,
                            uint16_t* out_sequence = nullptr,
                            uint8_t* out_flags = nullptr);

/**
 * @brief 协议版本检测
 */
enum class ProtocolVersion : uint8_t { kV1 = 1, kV2 = 2 };
ProtocolVersion DetectProtocolVersion(const uint8_t* magic_bytes);

}  // namespace mir2::common

#endif  // MIR2_COMMON_PROTOCOL_PACKET_CODEC_H
