/**
 * @file tcp_connection.h
 * @brief TCP连接封装
 */

#ifndef MIR2_NETWORK_TCP_CONNECTION_H
#define MIR2_NETWORK_TCP_CONNECTION_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <asio/buffer.hpp>
#include <asio/error_code.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/write.hpp>

namespace mir2::network {

using IoExecutor =
    std::decay_t<decltype(std::declval<asio::ip::tcp::socket&>().get_executor())>;

/**
 * @brief Socket适配器接口
 */
class SocketAdapter {
 public:
  using IoHandler = std::function<void(const asio::error_code&, std::size_t)>;

  virtual ~SocketAdapter() = default;
  virtual void async_read_some(const asio::mutable_buffer& buffer, IoHandler handler) = 0;
  virtual void async_write(const asio::const_buffer& buffer, IoHandler handler) = 0;
  virtual void shutdown(asio::ip::tcp::socket::shutdown_type type, asio::error_code& ec) = 0;
  virtual void close(asio::error_code& ec) = 0;
  virtual asio::ip::tcp::endpoint remote_endpoint(asio::error_code& ec) const = 0;
  virtual IoExecutor GetExecutor() = 0;
};

/**
 * @brief Asio socket 适配器实现
 */
class AsioSocketAdapter : public SocketAdapter {
 public:
  explicit AsioSocketAdapter(asio::ip::tcp::socket socket) : socket_(std::move(socket)) {}

  void async_read_some(const asio::mutable_buffer& buffer, IoHandler handler) override {
    socket_.async_read_some(buffer, std::move(handler));
  }

  void async_write(const asio::const_buffer& buffer, IoHandler handler) override {
    asio::async_write(socket_, buffer, std::move(handler));
  }

  void shutdown(asio::ip::tcp::socket::shutdown_type type, asio::error_code& ec) override {
    socket_.shutdown(type, ec);
  }

  void close(asio::error_code& ec) override {
    socket_.close(ec);
  }

  asio::ip::tcp::endpoint remote_endpoint(asio::error_code& ec) const override {
    return socket_.remote_endpoint(ec);
  }

  IoExecutor GetExecutor() override { return socket_.get_executor(); }

 private:
  asio::ip::tcp::socket socket_;
};

/**
 * @brief TCP连接
 */
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
 public:
  using BytesHandler = std::function<void(const uint8_t*, size_t)>;
  using DisconnectHandler = std::function<void(uint64_t)>;

  TcpConnection(std::unique_ptr<SocketAdapter> socket, uint64_t connection_id);

  /**
   * @brief 启动读循环
   */
  void Start();

  /**
   * @brief 发送原始字节流
   */
  void SendRaw(const std::vector<uint8_t>& bytes);

  /**
   * @brief 关闭连接
   */
  void Close();

  /**
   * @brief 设置读取回调
   */
  void SetReadHandler(BytesHandler handler);

  /**
   * @brief 设置断开回调
   */
  void SetDisconnectHandler(DisconnectHandler handler);

  uint64_t GetConnectionId() const { return connection_id_; }
  const std::string& GetRemoteAddress() const { return remote_address_; }
  uint16_t GetRemotePort() const { return remote_port_; }
  IoExecutor GetExecutor() { return socket_->GetExecutor(); }

 private:
  // Prevent unbounded memory growth when clients read slowly.
  static constexpr size_t kMaxWriteQueueSize = 100;

  void DoRead();
  void DoWrite();

  std::unique_ptr<SocketAdapter> socket_;
  uint64_t connection_id_ = 0;
  std::array<uint8_t, 4096> read_buffer_{};
  std::deque<std::vector<uint8_t>> write_queue_;
  std::atomic<bool> writing_{false};

  BytesHandler read_handler_;
  DisconnectHandler disconnect_handler_;

  std::string remote_address_;
  uint16_t remote_port_ = 0;
};

}  // namespace mir2::network

#endif  // MIR2_NETWORK_TCP_CONNECTION_H
