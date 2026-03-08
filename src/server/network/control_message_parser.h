/**
 * @file control_message_parser.h
 * @brief Shared parser for network control-plane messages.
 */

#ifndef MIR2_NETWORK_CONTROL_MESSAGE_PARSER_H_
#define MIR2_NETWORK_CONTROL_MESSAGE_PARSER_H_

#include <cstdint>
#include <vector>

namespace mir2::network {

enum class KcpUpgradeRequestStatus : uint8_t {
  kOk = 0,
  kInvalidPayload,
};

class ControlMessageParser {
 public:
  /**
   * @brief Empty payload is accepted for backward compatibility.
   */
  static KcpUpgradeRequestStatus ParseKcpUpgradeRequest(
      const std::vector<uint8_t>& payload);

  /**
   * @brief Parses heartbeat timestamp; returns fallback on invalid payload.
   */
  static bool ParseKcpHeartbeatTimestamp(const std::vector<uint8_t>& payload,
                                         uint32_t fallback_timestamp,
                                         uint32_t* out_timestamp);
};

}  // namespace mir2::network

#endif  // MIR2_NETWORK_CONTROL_MESSAGE_PARSER_H_
