#include "client/network/udp_transport.h"

#include <memory>
#include <vector>

namespace mir2::client {

UdpTransport::UdpTransport(asio::io_context& io_context)
    : io_context_(io_context),
      socket_(io_context) {
}

UdpTransport::~UdpTransport() {
  Close();
}

bool UdpTransport::Bind(uint16_t port) {
  std::lock_guard<std::mutex> lock(socket_mutex_);
  receive_generation_.fetch_add(1, std::memory_order_relaxed);

  asio::error_code ec;
  if (socket_.is_open()) {
    socket_.cancel(ec);
    socket_.close(ec);
  }

  socket_.open(asio::ip::udp::v4(), ec);
  if (ec) {
    return false;
  }

  socket_.set_option(asio::socket_base::reuse_address(true), ec);
  socket_.set_option(asio::socket_base::receive_buffer_size(kSocketBufferSize), ec);
  socket_.set_option(asio::socket_base::send_buffer_size(kSocketBufferSize), ec);

  asio::ip::udp::endpoint local_endpoint(asio::ip::udp::v4(), port);
  socket_.bind(local_endpoint, ec);
  if (ec) {
    socket_.close(ec);
    return false;
  }

  return true;
}

void UdpTransport::Close() {
  std::lock_guard<std::mutex> lock(socket_mutex_);
  receive_generation_.fetch_add(1, std::memory_order_relaxed);
  receive_handler_ = nullptr;

  if (!socket_.is_open()) {
    return;
  }

  asio::error_code ec;
  socket_.cancel(ec);
  socket_.close(ec);
}

bool UdpTransport::IsOpen() const {
  std::lock_guard<std::mutex> lock(socket_mutex_);
  return socket_.is_open();
}

void UdpTransport::StartReceive(ReceiveHandler handler) {
  uint64_t generation = 0;
  {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    receive_handler_ = std::move(handler);
    generation = receive_generation_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!socket_.is_open()) {
      return;
    }
  }

  DoReceive(generation);
}

void UdpTransport::SendTo(const asio::ip::udp::endpoint& endpoint,
                          const uint8_t* data,
                          size_t size) {
  std::lock_guard<std::mutex> lock(socket_mutex_);
  if (!socket_.is_open() || !data || size == 0) {
    return;
  }

  auto buffer = std::make_shared<std::vector<uint8_t>>(data, data + size);
  socket_.async_send_to(asio::buffer(*buffer), endpoint,
                        [buffer](const asio::error_code& /*ec*/, std::size_t /*bytes*/) {
                        });
}

void UdpTransport::DoReceive(uint64_t generation) {
  std::lock_guard<std::mutex> lock(socket_mutex_);
  if (!socket_.is_open()) {
    return;
  }
  if (generation != receive_generation_.load(std::memory_order_relaxed)) {
    return;
  }

  socket_.async_receive_from(
      asio::buffer(recv_buffer_), remote_endpoint_,
      [this, generation](const asio::error_code& ec, std::size_t bytes) {
        HandleReceive(generation, ec, bytes);
      });
}

void UdpTransport::HandleReceive(uint64_t generation,
                                 const asio::error_code& ec,
                                 size_t bytes) {
  ReceiveHandler handler;
  asio::ip::udp::endpoint endpoint;
  bool should_continue = false;

  {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (generation != receive_generation_.load(std::memory_order_relaxed)) {
      return;
    }

    if (!ec && bytes > 0 && receive_handler_) {
      handler = receive_handler_;
      endpoint = remote_endpoint_;
    }

    should_continue = socket_.is_open() &&
                      generation == receive_generation_.load(std::memory_order_relaxed);
  }

  if (handler) {
    handler(endpoint, recv_buffer_.data(), bytes);
  }

  if (should_continue) {
    DoReceive(generation);
  }
}

}  // namespace mir2::client
