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

#include "common/constants.h"
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
  enum class LifecycleState : uint8_t {
    kServing = 0,
    kHolding = 1,
    kRestoring = 2,
    kFlushing = 3,
    kShuttingDown = 4,
  };

  enum class LifecycleEvent : uint8_t {
    kLogicDisconnected = 0,
    kContextRestoreRequested = 1,
    kLogicReadyReceived = 2,
    kFlushCompleted = 3,
    kShutdownRequested = 4,
  };

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
  bool TransitionLifecycleState(LifecycleEvent event);
  void FlushBufferedMessages();
  void ApplyLifecycleStateLocked(LifecycleState target);
  ConnectionHolder::State GetHolderTargetStateForLifecycle(LifecycleState state) const;
  int64_t ComputeDisconnectBackoffMs(uint32_t retry_count) const;
  std::unique_ptr<ConnectionHolder> MakeConnectionHolder() const;
  void ApplyHolderState(ConnectionHolder& holder, ConnectionHolder::State target);
  network::TcpClient* GetLogicClient() const;

  core::Application app_;
  std::unique_ptr<network::DualChannelManager> network_;
  std::unique_ptr<network::TcpClient> logic_client_;
  std::thread logic_thread_;

  // SessionMap: connection_id -> TcpSession, connected_at_ms
  mutable std::shared_mutex session_map_lock_;
  // Holder domain: connection_holders + lifecycle state
  mutable std::shared_mutex holder_lock_;
  // Backpressure domain
  mutable std::shared_mutex backpressure_lock_;
  // Disconnect retry domain
  mutable std::shared_mutex disconnect_queue_lock_;
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
  size_t max_connections_limit_ = static_cast<size_t>(common::kMaxConnections);
  int64_t stale_route_cleanup_elapsed_ms_ = 0;
  int64_t stale_route_cleanup_interval_ms_ = 30000;
  int64_t logic_reconnect_initial_backoff_ms_ = 1000;
  int64_t logic_reconnect_max_backoff_ms_ = 30000;
  int logic_reconnect_max_retries_ = 0;  // 0 means unlimited retries.
  uint32_t backpressure_default_pause_ms_ = 100;
  uint32_t backpressure_max_pause_ms_ = 2000;
  size_t max_forward_payload_bytes_ = 64 * 1024;
  int64_t disconnect_retry_initial_backoff_ms_ = 500;
  int64_t disconnect_retry_max_backoff_ms_ = 30000;
  int64_t disconnect_retry_ttl_ms_ = 300000;
  size_t disconnect_retry_max_queue_size_ = 100000;
  size_t holder_buffer_capacity_bytes_ = ConnectionHolder::kDefaultBufferCapacityBytes;
  size_t holder_disconnect_threshold_bytes_ =
      ConnectionHolder::kDefaultDisconnectThresholdBytes;

  LifecycleState lifecycle_state_ = LifecycleState::kServing;
  ConnectionHolder::State holder_state_ = ConnectionHolder::State::FORWARDING;
};

}  // namespace mir2::gateway

#endif  // MIR2_GATEWAY_GATEWAY_SERVER_H_
