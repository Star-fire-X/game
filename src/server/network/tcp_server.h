/**
 * @file tcp_server.h
 * @brief TCP服务器
 */

#ifndef MIR2_NETWORK_TCP_SERVER_H
#define MIR2_NETWORK_TCP_SERVER_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <asio/error_code.hpp>
#include <asio/ip/tcp.hpp>
#if defined(ASIO_HAS_LOCAL_SOCKETS)
#include <asio/local/stream_protocol.hpp>
#endif

#include "network/tcp_connection.h"

namespace mir2::network {

/**
 * @brief TCP服务器封装
 */
class TcpServer {
 public:
  using ConnectHandler = std::function<void(const std::shared_ptr<TcpConnection>&)>;

  explicit TcpServer(asio::io_context& io_context);

  /**
   * @brief 启动监听
   */
  bool Start(const std::string& bind_ip, uint16_t port, int max_connections);

  /**
   * @brief 启动 UDS 监听（仅 Unix 平台）
   */
  bool StartUnix(const std::string& socket_path, int max_connections);

  /**
   * @brief 停止服务器
   */
  void Stop();

  void SetConnectHandler(ConnectHandler handler) { connect_handler_ = std::move(handler); }

 private:
  void DoAccept();
  void StopAndResetAcceptors();
  void CleanupUnixSocketPath();

  asio::io_context& io_context_;
  std::unique_ptr<asio::ip::tcp::acceptor> tcp_acceptor_;
#if defined(ASIO_HAS_LOCAL_SOCKETS)
  std::unique_ptr<asio::local::stream_protocol::acceptor> uds_acceptor_;
#endif
  std::string uds_socket_path_;
  std::atomic<uint64_t> next_connection_id_{1};
  int max_connections_ = 0;

  ConnectHandler connect_handler_;
};

}  // namespace mir2::network

#endif  // MIR2_NETWORK_TCP_SERVER_H
