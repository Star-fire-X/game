/**
 * @file kcp_upgrade_handler.h
 * @brief Client-side KCP upgrade and heartbeat handler.
 */

#ifndef LEGEND2_CLIENT_NETWORK_KCP_UPGRADE_HANDLER_H
#define LEGEND2_CLIENT_NETWORK_KCP_UPGRADE_HANDLER_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "common/network/kcp_config.h"
#include "common/protocol/packet_codec.h"

namespace mir2::client {

/**
 * @brief Handles KCP upgrade handshake and KCP heartbeat messages.
 */
class KcpUpgradeHandler {
 public:
  using NetworkPacket = mir2::common::NetworkPacket;
  using SendCallback = std::function<void(uint16_t, const std::vector<uint8_t>&)>;
  using SessionCallback = std::function<void(uint32_t, const std::string&, uint16_t)>;
  using EventCallback = std::function<void()>;

  explicit KcpUpgradeHandler(mir2::common::KcpConfig config = {});

  void SetTcpSender(SendCallback sender);
  void SetKcpSender(SendCallback sender);
  void SetSessionCallback(SessionCallback callback);
  void SetConfirmedCallback(EventCallback callback);
  void SetFailedCallback(EventCallback callback);
  void SetDisconnectCallback(EventCallback callback);

  void Reset();
  void ResetHeartbeat();
  void RequestUpgrade();
  void OnTcpConnected();

  bool HandleTcpPacket(const NetworkPacket& packet);
  bool HandleKcpPacket(const NetworkPacket& packet);

  void Update(int64_t now_ms, bool tcp_connected, bool kcp_connected, bool kcp_confirmed);

 private:
  void SendUpgradeRequest();
  void SendHeartbeat(int64_t now_ms);
  void MarkHeartbeatAck(int64_t now_ms);

  mir2::common::KcpConfig config_;
  SendCallback tcp_sender_;
  SendCallback kcp_sender_;
  SessionCallback session_callback_;
  EventCallback confirmed_callback_;
  EventCallback failed_callback_;
  EventCallback disconnect_callback_;

  bool request_sent_ = false;
  bool request_in_flight_ = false;
  int64_t request_start_ms_ = 0;

  int64_t last_heartbeat_send_ms_ = 0;
  int64_t last_heartbeat_ack_ms_ = 0;
  bool heartbeat_confirmed_ = false;
  bool heartbeat_timed_out_ = false;
};

}  // namespace mir2::client

#endif  // LEGEND2_CLIENT_NETWORK_KCP_UPGRADE_HANDLER_H
