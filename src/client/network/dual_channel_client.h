/**
 * @file dual_channel_client.h
 * @brief Dual channel network client combining TCP and KCP.
 */

#ifndef LEGEND2_CLIENT_NETWORK_DUAL_CHANNEL_CLIENT_H
#define LEGEND2_CLIENT_NETWORK_DUAL_CHANNEL_CLIENT_H

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>

#include "client/network/kcp_channel.h"
#include "client/network/kcp_upgrade_handler.h"
#include "client/network/network_client.h"
#include "common/network/channel_router.h"
#include "common/network/fallback_controller.h"
#include "common/network/kcp_config.h"

namespace mir2::client {

/**
 * @brief TCP/KCP dual-channel network client.
 *
 * Message Dispatch Modes:
 * 1. Callback Mode: set_on_message() + update()
 *    - update() drains receive queue and invokes on_message callback
 *    - Do NOT call receive() in this mode
 * 2. Polling Mode: receive() without set_on_message()
 *    - update() only updates internal state, does not dispatch
 *    - Call receive() to manually poll for messages
 *
 * WARNING: Do not mix callback and polling modes.
 */
class DualChannelClient : public INetworkClient {
 public:
  enum class KcpUpgradeState : uint8_t {
    kIdle = 0,
    kRequested = 1,
    kConfirmed = 2,
    kFailed = 3
  };

  DualChannelClient();
  explicit DualChannelClient(mir2::common::KcpConfig config);
  DualChannelClient(std::unique_ptr<NetworkClient> tcp_client,
                    std::unique_ptr<KcpChannel> kcp_channel,
                    mir2::common::KcpConfig config = {});
  ~DualChannelClient() override;

  bool connect(const std::string& host, uint16_t port) override;
  void disconnect() override;
  bool is_connected() const override;

  void send(uint16_t msg_id, const std::vector<uint8_t>& payload) override;
  std::optional<NetworkPacket> receive() override;
  void update() override;

  ConnectionState get_state() const override;
  ErrorCode get_last_error() const override;

  void set_on_message(MessageCallback callback) override;
  void set_on_disconnect(EventCallback callback) override;
  void set_on_connect(EventCallback callback) override;

  void set_kcp_session(uint32_t conv_id, const std::string& token, uint16_t udp_port);
  void set_kcp_session(uint32_t conv_id,
                       const std::array<uint8_t, KcpChannel::kTokenSize>& token,
                       uint16_t udp_port);
  void mark_kcp_confirmed();
  void mark_kcp_failed();
  void notify_kcp_disconnect();
  void reset_kcp_session();

  KcpUpgradeState get_kcp_state() const;
  void set_route(uint16_t msg_id, mir2::common::ChannelType channel);

 private:
  struct KcpSessionInfo {
    uint32_t conv_id = 0;
    std::array<uint8_t, KcpChannel::kTokenSize> token{};
    uint16_t udp_port = 0;
    bool valid = false;
  };

  void wire_tcp_callbacks();
  void enqueue_packet(const NetworkPacket& packet);
  void handle_kcp_packet(const NetworkPacket& packet);
  void attempt_kcp_connect();
  void reset_kcp_session_info();
  void process_fallback(int64_t now_ms);
  bool is_kcp_available() const;
  void StartIoThread();
  void StopIoThread();

  asio::io_context io_context_;
  std::optional<asio::executor_work_guard<asio::io_context::executor_type>> io_work_guard_;
  std::thread io_thread_;

  std::unique_ptr<NetworkClient> tcp_client_;
  std::unique_ptr<KcpChannel> kcp_channel_;
  mir2::common::ChannelRouter router_;
  mir2::common::FallbackController fallback_;
  KcpUpgradeHandler kcp_upgrade_handler_;

  std::string host_;
  uint16_t tcp_port_ = 0;

  mutable std::mutex kcp_info_mutex_;
  KcpSessionInfo kcp_info_{};

  std::atomic<KcpUpgradeState> kcp_state_{KcpUpgradeState::kIdle};

  static constexpr size_t kMaxReceiveQueueSize = 1000;
  mutable std::mutex receive_mutex_;
  std::queue<NetworkPacket> receive_queue_;

  mutable std::mutex callback_mutex_;
  MessageCallback on_message_;
  EventCallback on_disconnect_;
  EventCallback on_connect_;
};

}  // namespace mir2::client

#endif  // LEGEND2_CLIENT_NETWORK_DUAL_CHANNEL_CLIENT_H
