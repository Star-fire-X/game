/**
 * @file network_manager.h
 * @brief 网络管理器
 */

#ifndef MIR2_NETWORK_NETWORK_MANAGER_H
#define MIR2_NETWORK_NETWORK_MANAGER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "network/message_dispatcher.h"
#include "network/tcp_connection.h"
#include "network/tcp_server.h"
#include "network/tcp_session.h"

namespace mir2::network {

/**
 * @brief 网络管理器
 */
class NetworkManager {
 public:
  using SessionFilter = std::function<bool(const std::shared_ptr<TcpSession>&)>;
  explicit NetworkManager(asio::io_context& io_context);

  /**
   * @brief 初始化并启动监听
   */
  bool Start(const std::string& bind_ip, uint16_t port, int max_connections);

  /**
   * @brief 停止网络
   */
  void Stop();

  /**
   * @brief 注册消息处理
   */
  void RegisterHandler(uint16_t msg_id, MessageHandler handler);

  /**
   * @brief 发送消息
   */
  void Send(uint64_t connection_id, uint16_t msg_id, const std::vector<uint8_t>& payload);

  /**
   * @brief 广播消息
   */
  void Broadcast(uint16_t msg_id, const std::vector<uint8_t>& payload);
  void BroadcastIf(uint16_t msg_id, const std::vector<uint8_t>& payload, SessionFilter filter);

  std::shared_ptr<TcpSession> GetSession(uint64_t session_id) const;
  std::vector<std::shared_ptr<TcpSession>> GetAllSessions() const;

  /**
   * @brief Tick更新
   */
  void Tick();

  size_t GetConnectionCount() const;

 private:
  void AddConnection(const std::shared_ptr<TcpConnection>& connection);
  void RemoveConnection(uint64_t connection_id);
  std::shared_ptr<TcpConnection> FindConnection(uint64_t connection_id) const;
  void BroadcastRaw(const std::vector<uint8_t>& bytes);
  void Close(uint64_t connection_id);
  void StopAll();

  void OnConnectionOpened(const std::shared_ptr<TcpConnection>& connection);
  void OnConnectionClosed(const std::shared_ptr<TcpConnection>& connection);
  void OnSessionConnected(const std::shared_ptr<TcpSession>& session);
  void OnSessionDisconnected(const std::shared_ptr<TcpSession>& session);

  asio::io_context& io_context_;
  TcpServer server_;
  MessageDispatcher dispatcher_;

  mutable std::mutex mutex_;
  std::unordered_map<uint64_t, std::shared_ptr<TcpConnection>> connections_;
  std::unordered_map<uint64_t, std::shared_ptr<TcpSession>> sessions_;
  int64_t last_heartbeat_check_ms_ = 0;
};

}  // namespace mir2::network

#endif  // MIR2_NETWORK_NETWORK_MANAGER_H
