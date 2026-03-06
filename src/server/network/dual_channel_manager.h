/**
 * @file dual_channel_manager.h
 * @brief Dual channel network manager (TCP + KCP).
 */

#ifndef MIR2_NETWORK_DUAL_CHANNEL_MANAGER_H
#define MIR2_NETWORK_DUAL_CHANNEL_MANAGER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/network/channel_router.h"
#include "common/network/kcp_config.h"
#include "network/kcp_server.h"
#include "network/message_dispatcher.h"

namespace mir2::network {

class TcpSession;

class INetworkManager {
 public:
  using SessionFilter = std::function<bool(const std::shared_ptr<TcpSession>&)>;
  virtual ~INetworkManager() = default;

  virtual bool Start(const std::string& bind_ip, uint16_t port, int max_connections) = 0;
  virtual void Stop() = 0;
  virtual void RegisterHandler(uint16_t msg_id, MessageHandler handler) = 0;
  virtual void Send(uint64_t connection_id, uint16_t msg_id,
                    const std::vector<uint8_t>& payload) = 0;
  virtual std::shared_ptr<TcpSession> GetSession(uint64_t session_id) const = 0;
  virtual std::vector<std::shared_ptr<TcpSession>> GetAllSessions() const = 0;
  virtual size_t GetConnectionCount() const = 0;
  virtual void Tick() = 0;
};

class KcpUpgradeHandler;
class IDualChannelManager {
 public:
  virtual ~IDualChannelManager() = default;

  virtual IKcpServer& GetKcpServer() = 0;
  virtual std::shared_ptr<KcpSession> GetKcpSession(uint64_t session_id) const = 0;
  virtual bool BindKcpSession(uint64_t session_id,
                              const std::shared_ptr<KcpSession>& kcp_session) = 0;
  virtual bool TryBindKcpSessionIfAbsent(
      uint64_t session_id,
      const std::shared_ptr<KcpSession>& kcp_session) = 0;
};
/**
 * @brief Combines TCP and KCP sessions with automatic routing.
 */
class DualChannelManager : public IDualChannelManager {
 public:
  using SessionFilter = std::function<bool(const std::shared_ptr<TcpSession>&)>;
  using ChannelMessageHandler = std::function<void(
      const std::shared_ptr<TcpSession>&,
      mir2::common::ChannelType,
      const std::vector<uint8_t>&)>;

  DualChannelManager(asio::io_context& io_context,
                     mir2::common::KcpConfig kcp_config = {});
  DualChannelManager(asio::io_context& io_context,
                     std::unique_ptr<INetworkManager> tcp_manager,
                     std::unique_ptr<IKcpServer> kcp_server);
  ~DualChannelManager();

  bool Start(const std::string& bind_ip,
             uint16_t tcp_port,
             uint16_t udp_port,
             int max_connections);
  void Stop();

  void RegisterHandler(uint16_t msg_id, MessageHandler handler);
  void RegisterChannelAwareHandler(uint16_t msg_id, ChannelMessageHandler handler);
  void SetRoute(uint16_t msg_id, mir2::common::ChannelType channel);
  void SetDefaultChannel(mir2::common::ChannelType channel);
  void SetAcceptedConnectionWriteQueueSize(size_t max_write_queue_size);
  void SetAcceptedConnectionWriteBatchOptions(size_t max_batch_bytes,
                                              int64_t flush_interval_us);
  void SetAcceptedConnectionLowCopySendEnabled(bool enabled);
  void SetKcpCleanupIntervalMs(int64_t interval_ms);

  void Send(uint64_t session_id, uint16_t msg_id, const std::vector<uint8_t>& payload);
  void Broadcast(uint16_t msg_id, const std::vector<uint8_t>& payload);
  void BroadcastIf(uint16_t msg_id,
                   const std::vector<uint8_t>& payload,
                   SessionFilter filter);

  std::shared_ptr<TcpSession> GetSession(uint64_t session_id) const;
  std::vector<std::shared_ptr<TcpSession>> GetAllSessions() const;
  size_t GetConnectionCount() const;

  void Tick();

  bool BindKcpSession(uint64_t session_id,
                      const std::shared_ptr<KcpSession>& kcp_session) override;
  bool TryBindKcpSessionIfAbsent(
      uint64_t session_id,
      const std::shared_ptr<KcpSession>& kcp_session) override;
  void UnbindKcpSession(uint64_t session_id);
  std::shared_ptr<KcpSession> GetKcpSession(uint64_t session_id) const override;

  IKcpServer& GetKcpServer() override { return *kcp_server_; }

 private:
  struct KcpBinding {
    std::weak_ptr<KcpSession> session;
    uint32_t conv_id = 0;
  };

  void HandleKcpMessage(const std::shared_ptr<KcpSession>& session, const Packet& packet);
  bool RemoveKcpBinding(uint64_t session_id, bool stale);
  void PublishKcpBindingCountLocked() const;
  void CleanupKcpSessions();

  asio::io_context& io_context_;
  std::unique_ptr<INetworkManager> tcp_manager_;
  std::unique_ptr<IKcpServer> kcp_server_;
  mir2::common::ChannelRouter router_;
  MessageDispatcher kcp_dispatcher_;
  uint16_t udp_port_ = 0;
  std::unique_ptr<KcpUpgradeHandler> kcp_upgrade_handler_;

  mutable std::mutex mutex_;
  std::unordered_map<uint64_t, KcpBinding> kcp_sessions_;
  std::atomic<int64_t> last_kcp_cleanup_ms_{0};
  int64_t kcp_cleanup_interval_ms_ = 1000;
};

}  // namespace mir2::network

#endif  // MIR2_NETWORK_DUAL_CHANNEL_MANAGER_H
