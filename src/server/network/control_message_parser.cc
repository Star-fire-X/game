#include "network/control_message_parser.h"

#include <flatbuffers/flatbuffers.h>

#include "system_generated.h"

namespace mir2::network {

KcpUpgradeRequestStatus ControlMessageParser::ParseKcpUpgradeRequest(
    const std::vector<uint8_t>& payload) {
  if (payload.empty()) {
    return KcpUpgradeRequestStatus::kOk;
  }

  flatbuffers::Verifier verifier(payload.data(), payload.size());
  if (!verifier.VerifyBuffer<mir2::proto::KcpUpgradeRequest>(nullptr)) {
    return KcpUpgradeRequestStatus::kInvalidPayload;
  }

  return KcpUpgradeRequestStatus::kOk;
}

bool ControlMessageParser::ParseKcpHeartbeatTimestamp(
    const std::vector<uint8_t>& payload,
    uint32_t fallback_timestamp,
    uint32_t* out_timestamp) {
  if (!out_timestamp) {
    return false;
  }
  *out_timestamp = fallback_timestamp;

  if (payload.empty()) {
    return true;
  }

  flatbuffers::Verifier verifier(payload.data(), payload.size());
  if (!verifier.VerifyBuffer<mir2::proto::KcpHeartbeat>(nullptr)) {
    return false;
  }

  const auto* heartbeat = flatbuffers::GetRoot<mir2::proto::KcpHeartbeat>(
      payload.data());
  if (!heartbeat) {
    return false;
  }

  *out_timestamp = heartbeat->timestamp();
  return true;
}

}  // namespace mir2::network
