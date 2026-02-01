/**
 * @file gateway_server.h
 * @brief 网关服务器
 */

#ifndef MIR2_GATEWAY_GATEWAY_SERVER_H
#define MIR2_GATEWAY_GATEWAY_SERVER_H

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/enums.h"
#include "core/application.h"
#include "gateway/message_router.h"
#include "network/tcp_client.h"
#include "network/network_manager.h"

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
  void RegisterUser(uint64_t user_id, const std::shared_ptr<network::TcpSession>& session);
  void UnregisterSession(const std::shared_ptr<network::TcpSession>& session);
  void CleanupStaleRoutes();

  std::shared_ptr<network::TcpSession> GetConnectionSession(uint64_t connection_id) const;
  std::shared_ptr<network::TcpSession> GetUserSession(uint64_t user_id) const;
  size_t GetConnectionRouteCount() const;
  size_t GetUserRouteCount() const;

 protected:
  void Tick(float delta_time);
  virtual void CheckHeartbeatTimeouts(
      const std::vector<std::shared_ptr<network::TcpSession>>& sessions,
      int64_t now_ms);

 private:
  void RegisterHandlers();
  void RegisterDefaultRoutes();
  bool ConnectServices();
  void StartAsyncConnect(common::ServiceType service);
  bool IsServiceConnected(common::ServiceType service) const;
  bool ConnectToWorldService();
  bool ConnectToGameService();
  bool ConnectToDbService();
  void ScheduleReconnect(common::ServiceType service, int retry_count);
  void ForwardToService(common::ServiceType service, uint64_t client_id, uint16_t msg_id,
                        const std::vector<uint8_t>& payload);
  void NotifyClientDisconnected(uint64_t client_id);
  void OnServicePacket(common::ServiceType service, const network::Packet& packet);
  network::TcpClient* GetServiceClient(common::ServiceType service) const;

  core::Application app_;
  std::unique_ptr<network::NetworkManager> network_;
  std::unique_ptr<network::TcpClient> world_client_;
  std::unique_ptr<network::TcpClient> game_client_;
  std::unique_ptr<network::TcpClient> db_client_;
  std::thread logic_thread_;

  mutable std::shared_mutex route_table_mutex_;
  std::unordered_map<uint64_t, std::shared_ptr<network::TcpSession>> connection_route_table_;
  std::unordered_map<uint64_t, std::shared_ptr<network::TcpSession>> user_route_table_;

  MessageRouter message_router_;
  std::shared_mutex reconnect_mutex_;
  // Track reconnect attempts per service to avoid duplicate timers.
  std::unordered_map<common::ServiceType, bool> reconnecting_;
  float stale_route_cleanup_elapsed_sec_ = 0.0f;
};

}  // namespace mir2::gateway

#endif  // MIR2_GATEWAY_GATEWAY_SERVER_H
