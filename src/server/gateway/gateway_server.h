/**
 * @file gateway_server.h
 * @brief 网关服务器
 */

#ifndef MIR2_GATEWAY_GATEWAY_SERVER_H_
#define MIR2_GATEWAY_GATEWAY_SERVER_H_

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/application.h"
#include "gateway/connection_holder.h"
#include "network/tcp_client.h"
#include "network/dual_channel_manager.h"
#include "security/rate_limiter.h"

namespace mir2::gateway {

/**
 * @brief 网关服务器
 *
 * 负责客户端连接管理与消息路由。
 */
class GatewayServer {
 public:
  bool Initialize(const std::string& config_path);
  void Run();
  void Shutdown();

  void RegisterConnection(uint64_t connection_id,
                          const std::shared_ptr<network::TcpSession>& session);
  void UnregisterSession(const std::shared_ptr<network::TcpSession>& session);
  void CleanupStaleRoutes();

  std::shared_ptr<network::TcpSession> GetConnectionSession(uint64_t connection_id) const;
  size_t GetConnectionCount() const;

 protected:
  void Tick(float delta_time);
  virtual void CheckHeartbeatTimeouts(
      const std::vector<std::shared_ptr<network::TcpSession>>& sessions,
      int64_t now_ms);

 private:
  void RegisterHandlers();
  void HandleHeartbeat(const std::shared_ptr<network::TcpSession>& session,
                       const std::vector<uint8_t>& payload);
  void HandleForwardMessage(const std::shared_ptr<network::TcpSession>& session,
                            uint16_t msg_id,
                            mir2::common::ChannelType channel,
                            const std::vector<uint8_t>& payload);
  bool ConnectLogicService();
  void StartAsyncConnect();
  bool IsLogicConnected() const;
  bool ConnectToLogicService();
  void ScheduleReconnect(int retry_count);
  bool ForwardToLogic(uint64_t client_id, uint16_t msg_id,
                      const std::vector<uint8_t>& payload);
  void NotifyClientDisconnected(uint64_t client_id);
  void OnLogicPacket(const network::Packet& packet);
  void HandleBackpressureControl(const std::vector<uint8_t>& payload);
  void ResumeBackpressuredSessions(int64_t now_ms);
  std::vector<uint8_t> BuildContextRestoreResponse(uint32_t request_id) const;
  struct PendingDisconnectEvent {
    uint64_t client_id = 0;
    uint64_t sequence = 0;
    int64_t first_seen_ms = 0;
    int64_t next_retry_ms = 0;
    uint32_t retry_count = 0;
  };
  void EnqueueDisconnectEvent(uint64_t client_id, int64_t now_ms);
  void ProcessDisconnectRetryQueue(int64_t now_ms);
  bool TrySendDisconnectEvent(const PendingDisconnectEvent& event);
  void TrimExpiredDisconnectEvents(int64_t now_ms);
  void UpdateHoldingMetrics();
  void EnterHoldingState();
  void EnterRestoringState();
  void EnterFlushingState();
  void FlushBufferedMessages();
  void ApplyHolderState(ConnectionHolder& holder, ConnectionHolder::State target);
  network::TcpClient* GetLogicClient() const;

  core::Application app_;
  std::unique_ptr<network::DualChannelManager> network_;
  std::unique_ptr<network::TcpClient> logic_client_;
  std::thread logic_thread_;

  // SessionMap: connection_id -> TcpSession
  // 用于上下文注入和消息转发
  mutable std::shared_mutex session_map_mutex_;
  std::unordered_map<uint64_t, std::shared_ptr<network::TcpSession>> session_map_;
  std::unordered_map<uint64_t, std::unique_ptr<ConnectionHolder>> connection_holders_;
  std::unordered_map<uint64_t, int64_t> connection_connected_at_ms_;
  std::unordered_map<uint64_t, int64_t> client_backpressure_until_ms_;
  std::deque<PendingDisconnectEvent> pending_disconnect_events_;
  uint64_t next_disconnect_sequence_ = 1;
  mir2::security::RateLimiter login_ip_rate_limiter_{
      {.capacity = 5, .refill_rate = 1}};
  std::atomic<bool> logic_reconnecting_{false};
  std::atomic<bool> shutting_down_{false};
  float stale_route_cleanup_elapsed_sec_ = 0.0f;
  ConnectionHolder::State holder_state_ = ConnectionHolder::State::FORWARDING;
};

}  // namespace mir2::gateway

#endif  // MIR2_GATEWAY_GATEWAY_SERVER_H_
