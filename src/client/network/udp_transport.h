/**
 * @file udp_transport.h
 * @brief UDP transport wrapper for async send/receive.
 */

#ifndef LEGEND2_CLIENT_NETWORK_UDP_TRANSPORT_H
#define LEGEND2_CLIENT_NETWORK_UDP_TRANSPORT_H

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>

#include <asio.hpp>

namespace mir2::client {

/**
 * @brief UDP transport based on asio::ip::udp::socket.
 */
class IUdpTransport {
 public:
  using ReceiveHandler = std::function<void(const asio::ip::udp::endpoint&,
                                            const uint8_t*,
                                            size_t)>;

  virtual ~IUdpTransport() = default;

  virtual bool Bind(uint16_t port) = 0;
  virtual void Close() = 0;
  virtual bool IsOpen() const = 0;

  virtual void StartReceive(ReceiveHandler handler) = 0;
  virtual void SendTo(const asio::ip::udp::endpoint& endpoint,
                      const uint8_t* data,
                      size_t size) = 0;
};

class UdpTransport : public IUdpTransport {
 public:
  using ReceiveHandler = IUdpTransport::ReceiveHandler;

  explicit UdpTransport(asio::io_context& io_context);
  ~UdpTransport() override;

  UdpTransport(const UdpTransport&) = delete;
  UdpTransport& operator=(const UdpTransport&) = delete;

  bool Bind(uint16_t port) override;
  void Close() override;
  bool IsOpen() const override;

  void StartReceive(ReceiveHandler handler) override;
  void SendTo(const asio::ip::udp::endpoint& endpoint,
              const uint8_t* data,
              size_t size) override;

 private:
  void DoReceive(uint64_t generation);
  void HandleReceive(uint64_t generation,
                     const asio::error_code& ec,
                     size_t bytes);

  static constexpr size_t kMaxDatagramSize = 2048;
  static constexpr int kSocketBufferSize = 1 * 1024 * 1024;

  asio::io_context& io_context_;
  asio::ip::udp::socket socket_;
  mutable std::mutex socket_mutex_;
  std::atomic<uint64_t> receive_generation_{0};
  ReceiveHandler receive_handler_;
  std::array<uint8_t, kMaxDatagramSize> recv_buffer_{};
  asio::ip::udp::endpoint remote_endpoint_;
};

}  // namespace mir2::client

#endif  // LEGEND2_CLIENT_NETWORK_UDP_TRANSPORT_H
