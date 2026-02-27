/**
 * @file kcp_server.h
 * @brief KCP UDP server.
 */

#ifndef MIR2_NETWORK_KCP_SERVER_H_
#define MIR2_NETWORK_KCP_SERVER_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include <asio.hpp>

#include "common/network/kcp_config.h"
#include "network/conv_blacklist.h"
#include "network/ip_rate_limiter.h"
#include "network/kcp_session.h"

namespace mir2::network {

class IKcpServer {
 public:
  virtual ~IKcpServer() = default;

  virtual bool Start(const std::string& bind_ip, uint16_t port) = 0;
  virtual void Stop() = 0;
  virtual bool IsRunning() const = 0;
  virtual uint32_t AllocateConvId() = 0;
  virtual std::shared_ptr<KcpSession> CreateSession(
      uint32_t conv_id,
      const std::array<uint8_t, KcpSession::kTokenSize>& token) = 0;
  virtual bool AddSession(const std::shared_ptr<KcpSession>& session) = 0;
  virtual void RemoveSession(uint32_t conv_id) = 0;
  virtual std::shared_ptr<KcpSession> GetSession(uint32_t conv_id) const = 0;
  virtual void SetMessageHandler(KcpSession::MessageHandler handler) = 0;
};

/**
 * @brief UDP server hosting KCP sessions.
 */
class KcpServer : public IKcpServer {
 public:
  using SessionPtr = std::shared_ptr<KcpSession>;

  explicit KcpServer(asio::io_context& io_context,
                     mir2::common::KcpConfig config = {});

  bool Start(const std::string& bind_ip, uint16_t port) override;
  void Stop() override;
  bool IsRunning() const override { return socket_.is_open(); }

  uint32_t AllocateConvId() override;

  SessionPtr CreateSession(
      uint32_t conv_id,
      const std::array<uint8_t, KcpSession::kTokenSize>& token) override;
  bool AddSession(const SessionPtr& session) override;
  void RemoveSession(uint32_t conv_id) override;
  SessionPtr GetSession(uint32_t conv_id) const override;

  void SetMessageHandler(KcpSession::MessageHandler handler) override;

  void SetUdpSendFaultInjectEveryN(uint64_t every_n);
  uint64_t GetUdpSendFaultInjectEveryN() const;

 private:
  void StartReceive();
  void HandleReceive(const asio::error_code& ec, std::size_t bytes);
  void StartUpdateTimer();
  void UpdateAllSessions();
  void SendRaw(const asio::ip::udp::endpoint& endpoint,
               const uint8_t* data,
               size_t size);

  asio::io_context& io_context_;
  asio::ip::udp::socket socket_;
  asio::steady_timer update_timer_;
  std::array<uint8_t, 2048> recv_buffer_{};
  asio::ip::udp::endpoint remote_endpoint_;

  mir2::common::KcpConfig config_;
  IpRateLimiter rate_limiter_;
  ConvBlacklist blacklist_;

  mutable std::shared_mutex mutex_;
  std::unordered_map<uint32_t, SessionPtr> sessions_;
  KcpSession::MessageHandler message_handler_;
  int64_t last_cleanup_ms_ = 0;
  std::atomic<uint64_t> udp_send_error_count_{0};
  std::atomic<uint64_t> udp_send_attempt_count_{0};
  std::atomic<uint64_t> udp_send_fault_inject_every_n_{0};
  std::atomic<int64_t> last_udp_send_warn_ms_{0};
};

}  // namespace mir2::network

#endif  // MIR2_NETWORK_KCP_SERVER_H_
