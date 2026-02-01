/**
 * @file tcp_session.h
 * @brief TCP会话封装
 */

#ifndef MIR2_NETWORK_TCP_SESSION_H
#define MIR2_NETWORK_TCP_SESSION_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "network/packet_codec.h"
#include "network/tcp_connection.h"

namespace mir2::common {
enum class ErrorCode : uint16_t;
}  // namespace mir2::common

namespace mir2::network {

/**
 * @brief TCP会话
 */
class TcpSession : public std::enable_shared_from_this<TcpSession> {
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
  void Send(uint16_t msg_id, const std::vector<uint8_t>& payload);

  /**
   * @brief 关闭会话
   */
  void Close();

  /**
   * @brief 发送踢下线消息并关闭会话
   */
  void Kick(mir2::common::ErrorCode reason, const std::string& text);

  uint64_t GetSessionId() const { return connection_id_; }
  uint64_t GetUserId() const;
  void SetUserId(uint64_t user_id);
  SessionState GetState() const { return state_.load(); }
  AuthState GetAuthState() const { return auth_state_.load(); }
  void SetAuthState(AuthState state) { auth_state_.store(state); }

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
  uint16_t NextSendSequence();
  bool CheckRecvSequence(uint16_t seq);

  bool IsRateLimited() const { return rate_limited_.load(); }

  void HandlePacket(uint64_t connection_id, const Packet& packet);
  void HandleDisconnect(uint64_t connection_id);
  void HandleBytes(const uint8_t* data, size_t size);

 private:
  bool CheckRateLimit(size_t payload_size);

  std::shared_ptr<TcpConnection> connection_;
  uint64_t connection_id_ = 0;
  std::atomic<SessionState> state_{SessionState::kInit};
  std::atomic<AuthState> auth_state_{AuthState::kUnknown};
  std::atomic<uint64_t> user_id_{0};
  std::atomic<int64_t> last_heartbeat_ms_{0};
  std::atomic<bool> rate_limited_{false};

  std::string remote_address_;
  uint16_t remote_port_ = 0;

  // Rate limiter fields are atomic to avoid cross-thread races.
  std::atomic<int64_t> rate_window_start_ms_{0};
  std::atomic<uint32_t> rate_msg_count_{0};
  std::atomic<uint32_t> rate_bytes_count_{0};

  std::atomic<uint16_t> send_sequence_{0};
  std::atomic<uint16_t> recv_sequence_{0};
  ProtocolVersion protocol_version_ = ProtocolVersion::kV1;
  bool protocol_version_detected_ = false;
  std::vector<uint8_t> read_buffer_;

  ConnectedHandler connected_handler_;
  DisconnectedHandler disconnected_handler_;
  MessageHandler message_handler_;
};

}  // namespace mir2::network

#endif  // MIR2_NETWORK_TCP_SESSION_H
