/**
 * @file kcp_session.h
 * @brief KCP session management.
 */
/**
 * @note Namespace: mir2::network follows server-side convention.
 * KcpSession uses server-local Packet types rather than common::NetworkPacket
 * to maintain consistency with other server network components.
 */

#ifndef MIR2_NETWORK_KCP_SESSION_H_
#define MIR2_NETWORK_KCP_SESSION_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <asio.hpp>

#include "common/network/kcp_config.h"
#include "network/packet_codec.h"

struct IKCPCB;

namespace mir2::network {

class TcpSession;

/**
 * @brief KCP session wrapper.
 */
class KcpSession : public std::enable_shared_from_this<KcpSession> {
 public:
  using Packet = mir2::network::Packet;
  using MessageHandler =
      std::function<void(const std::shared_ptr<KcpSession>&, const Packet&)>;
  using OutputHandler =
      std::function<void(const asio::ip::udp::endpoint&, std::vector<uint8_t>&&)>;

  static constexpr size_t kTokenSize = 8;
  static constexpr int64_t kDefaultTimeoutMs = 15000;

  KcpSession(uint32_t conv_id,
             const std::array<uint8_t, kTokenSize>& token,
             mir2::common::KcpConfig config = {});
  KcpSession(uint32_t conv_id,
             const std::string& token,
             mir2::common::KcpConfig config = {});
  ~KcpSession();

  KcpSession(const KcpSession&) = delete;
  KcpSession& operator=(const KcpSession&) = delete;

  static uint32_t AllocateConvId();
  static bool ValidateConvId(uint32_t conv_id);

  uint32_t GetConvId() const { return conv_id_; }

  void BindTcpSession(const std::shared_ptr<TcpSession>& session);
  std::shared_ptr<TcpSession> GetTcpSession() const;

  void SetOutputHandler(OutputHandler handler);
  void SetMessageHandler(MessageHandler handler);

  void SetRemoteEndpoint(const asio::ip::udp::endpoint& endpoint);
  asio::ip::udp::endpoint GetRemoteEndpoint() const;
  bool HasRemoteEndpoint() const { return has_endpoint_.load(std::memory_order_relaxed); }

  bool ValidateToken(const uint8_t* token_data, size_t size) const;

  void Input(const uint8_t* data, size_t size);
  void Update(uint32_t now_ms);
  void Send(uint16_t msg_id, const std::vector<uint8_t>& payload);
  void Send(uint16_t msg_id, std::vector<uint8_t>&& payload);

  int64_t GetLastActiveMs() const { return last_active_ms_.load(); }
  bool IsTimedOut(int64_t now_ms, int64_t timeout_ms = kDefaultTimeoutMs) const;
  void SetLastActiveMsForTesting(int64_t last_active_ms);
  void SetHasRemoteEndpointForTesting(bool has_remote_endpoint);

 private:
  static int KcpOutput(const char* buf, int len, IKCPCB* kcp, void* user);
  void ConfigureKcp();
  void DrainReceive();

  uint32_t conv_id_ = 0;
  std::array<uint8_t, kTokenSize> token_{};
  mir2::common::KcpConfig config_;
  IKCPCB* kcp_ = nullptr;

  std::mutex kcp_mutex_;
  std::atomic<uint16_t> send_sequence_{0};
  std::atomic<int64_t> last_active_ms_{0};

  asio::ip::udp::endpoint remote_endpoint_;
  std::atomic<bool> has_endpoint_{false};
  mutable std::mutex endpoint_mutex_;

  std::weak_ptr<TcpSession> tcp_session_;

  mutable std::mutex handler_mutex_;
  MessageHandler message_handler_;
  OutputHandler output_handler_;
};

}  // namespace mir2::network

#endif  // MIR2_NETWORK_KCP_SESSION_H_
