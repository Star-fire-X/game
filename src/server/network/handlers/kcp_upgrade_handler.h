/**
 * @file kcp_upgrade_handler.h
 * @brief Server-side KCP upgrade handler.
 */

#ifndef MIR2_NETWORK_HANDLERS_KCP_UPGRADE_HANDLER_H_
#define MIR2_NETWORK_HANDLERS_KCP_UPGRADE_HANDLER_H_

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "network/kcp_session.h"

namespace mir2::network {

class IDualChannelManager;
class ITcpSession;

/**
 * @brief Handles KCP upgrade requests on the TCP channel.
 */
class KcpUpgradeHandler {
 public:
  KcpUpgradeHandler(IDualChannelManager& manager, uint16_t udp_port);

  void HandleKcpUpgradeRequest(const std::shared_ptr<ITcpSession>& session,
                               const std::vector<uint8_t>& payload);

 private:
  std::array<uint8_t, KcpSession::kTokenSize> GenerateToken() const;
  void SendUpgradeResponse(const std::shared_ptr<ITcpSession>& session,
                           bool success,
                           uint32_t conv_id,
                           const std::string& token,
                           uint16_t error_code);

  IDualChannelManager& manager_;
  uint16_t udp_port_ = 0;
};

}  // namespace mir2::network

#endif  // MIR2_NETWORK_HANDLERS_KCP_UPGRADE_HANDLER_H_
