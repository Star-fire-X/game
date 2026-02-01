/**
 * @file tcp_client.h
 * @brief TCP客户端
 */

#ifndef MIR2_NETWORK_TCP_CLIENT_H
#define MIR2_NETWORK_TCP_CLIENT_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <asio/io_context.hpp>

#include "network/packet_codec.h"
#include "network/tcp_connection.h"

namespace mir2::network {

/**
 * @brief TCP客户端封装
 */
class TcpClient {
 public:
  using PacketHandler = std::function<void(const Packet&)>;
  using DisconnectHandler = std::function<void()>;

  explicit TcpClient(asio::io_context& io_context);

  /**
   * @brief 连接到服务器
   */
  bool Connect(const std::string& host, uint16_t port);

  /**
   * @brief 发送消息
   */
  void Send(uint16_t msg_id, const std::vector<uint8_t>& payload);

  /**
   * @brief 关闭连接
   */
  void Close();

  bool IsConnected() const { return connected_.load(); }

  void SetPacketHandler(PacketHandler handler) { packet_handler_ = std::move(handler); }
  void SetDisconnectHandler(DisconnectHandler handler) { disconnect_handler_ = std::move(handler); }

 private:
  void HandleDisconnect(uint64_t connection_id);
  void HandleBytes(const uint8_t* data, size_t size);
  bool CheckRecvSequence(uint16_t seq);

  asio::io_context& io_context_;
  std::shared_ptr<TcpConnection> connection_;
  std::atomic<bool> connected_{false};
  PacketHandler packet_handler_;
  DisconnectHandler disconnect_handler_;
  std::vector<uint8_t> read_buffer_;
  ProtocolVersion protocol_version_ = ProtocolVersion::kV1;
  bool protocol_version_detected_ = false;
  std::atomic<uint16_t> send_sequence_{0};
  std::atomic<uint16_t> recv_sequence_{0};
};

}  // namespace mir2::network

#endif  // MIR2_NETWORK_TCP_CLIENT_H
