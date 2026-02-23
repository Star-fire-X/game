/**
 * @file tcp_session.h
 * @brief TCP会话封装
 */

#ifndef MIR2_NETWORK_TCP_SESSION_H_
#define MIR2_NETWORK_TCP_SESSION_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <asio/strand.hpp>

#include "network/packet_codec.h"
#include "network/tcp_connection.h"

namespace mir2::common {
enum class ErrorCode : uint16_t;
}  // namespace mir2::common

namespace mir2::network {

class ITcpSession {
 public:
  virtual ~ITcpSession() = default;

  virtual void Send(uint16_t msg_id, const std::vector<uint8_t>& payload) = 0;
  virtual uint64_t GetSessionId() const = 0;
  virtual bool IsKcpUpgradeAllowed() const = 0;
};

/**
 * @brief TCP会话
 */
class TcpSession : public ITcpSession,
                   public std::enable_shared_from_this<TcpSession> {
 public:
  enum class SessionState {
    kInit,
    kActive,
    kClosing,
    kClosed
  };

  enum class AuthState {
    kUnknown,
    kPending,
    kAuthed,
    kRejected
  };

  using ConnectedHandler = std::function<void(const std::shared_ptr<TcpSession>&)>;
  using DisconnectedHandler = std::function<void(const std::shared_ptr<TcpSession>&)>;
  using MessageHandler = std::function<void(const std::shared_ptr<TcpSession>&, const Packet&)>;

  explicit TcpSession(std::shared_ptr<TcpConnection> connection);

  /**
   * @brief 启动会话
   */
  void Start();

  /**
   * @brief 发送消息
   */
  void Send(uint16_t msg_id, const std::vector<uint8_t>& payload) override;

  /**
   * @brief 关闭会话
   */
  void Close();

  /**
   * @brief 发送踢下线消息并关闭会话
   */
  void Kick(mir2::common::ErrorCode reason, const std::string& text);

  uint64_t GetSessionId() const override { return connection_id_; }
  uint64_t GetUserId() const;
  void SetUserId(uint64_t user_id);
  uint64_t GetAccountId() const;
  void SetAccountId(uint64_t account_id);
  SessionState GetState() const { return state_.load(); }
  AuthState GetAuthState() const { return auth_state_.load(); }
  void SetAuthState(AuthState state) { auth_state_.store(state); }
  void SetBypassRateLimit(bool bypass) { bypass_rate_limit_.store(bypass); }
  bool IsRateLimitBypassed() const { return bypass_rate_limit_.load(); }

  void SetConnectedHandler(ConnectedHandler handler) { connected_handler_ = std::move(handler); }
  void SetDisconnectedHandler(DisconnectedHandler handler) { disconnected_handler_ = std::move(handler); }
  void SetMessageHandler(MessageHandler handler) { message_handler_ = std::move(handler); }

  void MarkHeartbeat();
  int64_t GetLastHeartbeatMs() const { return last_heartbeat_ms_.load(); }
  static int64_t NowMs();

  const std::string& GetRemoteAddress() const { return remote_address_; }
  uint16_t GetRemotePort() const { return remote_port_; }

  void SetProtocolVersion(ProtocolVersion version);
  ProtocolVersion GetProtocolVersion() const;
  bool IsKcpUpgradeAllowed() const override { return kcp_upgrade_allowed_.load(); }
  uint16_t NextSendSequence();
  bool CheckRecvSequence(uint16_t seq);

  bool IsRateLimited() const { return rate_limited_.load(); }
  void PauseRead();
  void ResumeRead();
  bool IsReadPaused() const;

  void HandlePacket(uint64_t connection_id, const Packet& packet);
  void HandleDisconnect(uint64_t connection_id);
  void HandleBytes(const uint8_t* data, size_t size);

 private:
  void PostSend(uint16_t msg_id, std::vector<uint8_t> payload, bool close_after_send);
  bool CheckRateLimit(size_t payload_size);
  size_t BufferedBytes() const;
  void ConsumeBytes(size_t bytes);
  void CompactReadBufferIfNeeded(size_t incoming_bytes = 0);

  std::shared_ptr<TcpConnection> connection_;
  std::optional<asio::strand<IoExecutor>> send_strand_;
  uint64_t connection_id_ = 0;
  std::atomic<SessionState> state_{SessionState::kInit};
  std::atomic<AuthState> auth_state_{AuthState::kUnknown};
  std::atomic<uint64_t> user_id_{0};
  std::atomic<uint64_t> account_id_{0};
  std::atomic<int64_t> last_heartbeat_ms_{0};
  std::atomic<bool> rate_limited_{false};
  std::atomic<bool> bypass_rate_limit_{false};

  std::string remote_address_;
  uint16_t remote_port_ = 0;

  // Rate limiter fields are atomic to avoid cross-thread races.
  std::atomic<int64_t> rate_window_start_ms_{0};
  std::atomic<uint32_t> rate_msg_count_{0};
  std::atomic<uint32_t> rate_bytes_count_{0};

  std::atomic<uint16_t> send_sequence_{0};
  std::atomic<uint16_t> recv_sequence_{0};
  ProtocolVersion protocol_version_ = ProtocolVersion::kV2;
  std::atomic<bool> protocol_version_detected_{false};
  std::atomic<bool> kcp_upgrade_allowed_{true};
  std::vector<uint8_t> read_buffer_;
  size_t read_offset_ = 0;
  Packet decode_packet_{};

  ConnectedHandler connected_handler_;
  DisconnectedHandler disconnected_handler_;
  MessageHandler message_handler_;
};

}  // namespace mir2::network

#endif  // MIR2_NETWORK_TCP_SESSION_H_
