/**
 * @file tcp_client.h
 * @brief TCP客户端
 */

#ifndef MIR2_NETWORK_TCP_CLIENT_H_
#define MIR2_NETWORK_TCP_CLIENT_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <asio/io_context.hpp>
#include <asio/strand.hpp>

#include "network/packet_codec.h"
#include "network/protocol_ingress_parser.h"
#include "network/tcp_connection.h"

namespace mir2::network {

/**
 * @brief TCP客户端封装
 */
class TcpClient {
 public:
  using PacketHandler = std::function<void(const Packet&)>;
  using DisconnectHandler = std::function<void()>;
  static constexpr size_t kDefaultServiceWriteQueueSize = 8192;

  explicit TcpClient(asio::io_context& io_context);

  /**
   * @brief 连接到服务器
   */
  bool Connect(const std::string& host, uint16_t port);

  /**
   * @brief 通过 UDS 连接到服务器（仅 Unix 平台）
   */
  bool ConnectUnix(const std::string& socket_path);

  /**
   * @brief 发送消息
   */
  void Send(uint16_t msg_id, const std::vector<uint8_t>& payload);
  void Send(uint16_t msg_id, std::vector<uint8_t>&& payload);

  /**
   * @brief 关闭连接
   */
  void Close();

  bool IsConnected() const { return connected_.load(); }

  void SetPacketHandler(PacketHandler handler) { packet_handler_ = std::move(handler); }
  void SetDisconnectHandler(DisconnectHandler handler) { disconnect_handler_ = std::move(handler); }
  void SetWriteQueueSize(size_t max_write_queue_size);
  void SetLowCopySendEnabled(bool enabled);

  // Testing hook for replacing white-box private-member access.
  void AttachConnectionForTest(const std::shared_ptr<TcpConnection>& connection,
                               bool connected = true);

 private:
  void HandleDisconnect(uint64_t connection_id, uint64_t epoch);
  void HandleBytes(const uint8_t* data, size_t size, uint64_t epoch);
  bool CheckRecvSequence(uint16_t seq);

  asio::io_context& io_context_;
  asio::strand<asio::io_context::executor_type> send_strand_;
  std::shared_ptr<TcpConnection> connection_;
  std::atomic<uint64_t> connection_epoch_{0};
  std::atomic<bool> connected_{false};
  size_t write_queue_size_ = kDefaultServiceWriteQueueSize;
  bool low_copy_send_enabled_ = false;
  PacketHandler packet_handler_;
  DisconnectHandler disconnect_handler_;
  ProtocolIngressParser ingress_parser_{mir2::common::ChannelType::kTcp};
  std::atomic<uint16_t> send_sequence_{0};
  std::atomic<uint16_t> recv_sequence_{0};
};

}  // namespace mir2::network

#endif  // MIR2_NETWORK_TCP_CLIENT_H_
