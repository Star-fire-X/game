/**
 * @file kcp_channel.h
 * @brief KCP channel implementation for the client.
 */

#ifndef LEGEND2_CLIENT_NETWORK_KCP_CHANNEL_H
#define LEGEND2_CLIENT_NETWORK_KCP_CHANNEL_H

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include <asio.hpp>

#include "client/network/udp_transport.h"
#include "common/network/i_channel.h"
#include "common/network/kcp_config.h"
#include "common/protocol/packet_codec.h"

struct IKCPCB;

namespace mir2::client {

/**
 * @brief KCP-based client channel.
 */
class KcpChannel : public mir2::common::IChannel {
 public:
  using NetworkPacket = mir2::common::NetworkPacket;
  using MessageCallback = std::function<void(const NetworkPacket&)>;

  static constexpr size_t kTokenSize = 8;
  static constexpr size_t kMaxReceiveQueueSize = 1000;

  explicit KcpChannel(asio::io_context& io_context,
                      mir2::common::KcpConfig config = {});
  KcpChannel(asio::io_context& io_context,
             mir2::common::KcpConfig config,
             std::unique_ptr<IUdpTransport> transport);
  ~KcpChannel() override;

  KcpChannel(const KcpChannel&) = delete;
  KcpChannel& operator=(const KcpChannel&) = delete;

  bool Connect(const std::string& host, uint16_t port) override;
  void Disconnect() override;
  bool IsConnected() const override;

  void Send(uint16_t msg_id, const std::vector<uint8_t>& payload) override;
  void Update() override;

  mir2::common::ChannelType Type() const override;

  // Reset KCP state for a fresh handshake without touching TCP.
  void ResetSession();

  void SetOnMessage(MessageCallback callback);
  void SetConvId(uint32_t conv);
  void SetSessionToken(const std::array<uint8_t, kTokenSize>& token);
  void SetSessionToken(const std::string& token);

 private:
  static int KcpOutput(const char* buf, int len, IKCPCB* kcp, void* user);
  static uint32_t GetTimestampMs();

  void StartUpdateTimer();
  void ConfigureKcp();
  void HandleUdpPacket(const asio::ip::udp::endpoint& endpoint,
                       const uint8_t* data,
                       size_t size);
  void DrainKcpReceive();

  mir2::common::KcpConfig config_;
  asio::io_context& io_context_;
  std::unique_ptr<IUdpTransport> transport_;
  asio::steady_timer update_timer_;
  asio::ip::udp::endpoint remote_endpoint_;

  std::atomic<mir2::common::ChannelState> state_{mir2::common::ChannelState::Disconnected};
  std::atomic<uint16_t> send_sequence_{0};
  std::atomic<uint32_t> conv_id_{0};
  std::array<uint8_t, kTokenSize> session_token_{};
  std::atomic<bool> token_set_{false};

  std::mutex receive_mutex_;
  std::queue<NetworkPacket> receive_queue_;

  std::mutex callback_mutex_;
  MessageCallback on_message_;

  std::mutex kcp_mutex_;
  IKCPCB* kcp_ = nullptr;
};

}  // namespace mir2::client

#endif  // LEGEND2_CLIENT_NETWORK_KCP_CHANNEL_H
