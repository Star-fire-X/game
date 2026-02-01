/**
 * @file mock_socket.h
 * @brief Mock SocketAdapter 用于网络层测试
 */

#ifndef MIR2_TESTS_MOCKS_MOCK_SOCKET_H
#define MIR2_TESTS_MOCKS_MOCK_SOCKET_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

#include <asio/buffer.hpp>
#include <asio/error.hpp>
#include <asio/error_code.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/post.hpp>

#include "network/tcp_connection.h"

namespace mir2::network {

class MockSocket : public SocketAdapter {
 public:
  explicit MockSocket(IoExecutor executor,
                      asio::ip::tcp::endpoint endpoint = asio::ip::tcp::endpoint(
                          asio::ip::address_v4::loopback(),
                          0))
      : executor_(std::move(executor)), endpoint_(std::move(endpoint)) {}

  void PushReadData(std::vector<uint8_t> data) {
    read_queue_.push_back(std::move(data));
    TryFulfillPendingRead();
  }

  void SetReadError(asio::error_code error) {
    read_error_ = error;
    read_error_once_ = true;
    TryFulfillPendingRead();
  }

  void SetWriteError(asio::error_code error) {
    write_error_ = error;
    write_error_once_ = true;
  }

  const std::vector<std::vector<uint8_t>>& GetWrites() const { return writes_; }
  bool IsClosed() const { return closed_; }
  bool ShutdownCalled() const { return shutdown_called_; }

  void async_read_some(const asio::mutable_buffer& buffer, IoHandler handler) override {
    if (read_error_once_) {
      const auto error = read_error_;
      read_error_once_ = false;
      asio::post(executor_, [handler = std::move(handler), error]() { handler(error, 0); });
      return;
    }

    if (!read_queue_.empty()) {
      FulfillRead(buffer, std::move(handler));
      return;
    }

    pending_read_ = PendingRead{static_cast<uint8_t*>(buffer.data()),
                                buffer.size(),
                                std::move(handler)};
  }

  void async_write(const asio::const_buffer& buffer, IoHandler handler) override {
    if (write_error_once_) {
      const auto error = write_error_;
      write_error_once_ = false;
      asio::post(executor_, [handler = std::move(handler), error]() { handler(error, 0); });
      return;
    }

    const auto* data = static_cast<const uint8_t*>(buffer.data());
    writes_.emplace_back(data, data + buffer.size());
    const auto bytes = buffer.size();
    asio::post(executor_, [handler = std::move(handler), bytes]() { handler({}, bytes); });
  }

  void shutdown(asio::ip::tcp::socket::shutdown_type /*type*/, asio::error_code& ec) override {
    shutdown_called_ = true;
    ec.clear();
  }

  void close(asio::error_code& ec) override {
    closed_ = true;
    ec.clear();
  }

  asio::ip::tcp::endpoint remote_endpoint(asio::error_code& ec) const override {
    ec.clear();
    return endpoint_;
  }

  IoExecutor GetExecutor() override { return executor_; }

 private:
  struct PendingRead {
    uint8_t* data = nullptr;
    std::size_t size = 0;
    IoHandler handler;
  };

  void TryFulfillPendingRead() {
    if (!pending_read_) {
      return;
    }

    if (read_error_once_) {
      const auto error = read_error_;
      read_error_once_ = false;
      auto handler = std::move(pending_read_->handler);
      pending_read_.reset();
      asio::post(executor_, [handler = std::move(handler), error]() { handler(error, 0); });
      return;
    }

    if (!read_queue_.empty()) {
      auto pending = std::move(*pending_read_);
      pending_read_.reset();
      FulfillRead(asio::mutable_buffer(pending.data, pending.size), std::move(pending.handler));
    }
  }

  void FulfillRead(const asio::mutable_buffer& buffer, IoHandler handler) {
    if (read_queue_.empty()) {
      asio::post(executor_, [handler = std::move(handler)]() {
        handler(asio::error::would_block, 0);
      });
      return;
    }

    auto& chunk = read_queue_.front();
    const auto bytes = std::min(buffer.size(), chunk.size());
    std::memcpy(buffer.data(), chunk.data(), bytes);
    if (bytes < chunk.size()) {
      chunk.erase(chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(bytes));
    } else {
      read_queue_.pop_front();
    }

    asio::post(executor_, [handler = std::move(handler), bytes]() { handler({}, bytes); });
  }

  IoExecutor executor_;
  asio::ip::tcp::endpoint endpoint_;
  std::deque<std::vector<uint8_t>> read_queue_;
  std::vector<std::vector<uint8_t>> writes_;
  std::optional<PendingRead> pending_read_;
  asio::error_code read_error_;
  asio::error_code write_error_;
  bool read_error_once_ = false;
  bool write_error_once_ = false;
  bool closed_ = false;
  bool shutdown_called_ = false;
};

}  // namespace mir2::network

#endif  // MIR2_TESTS_MOCKS_MOCK_SOCKET_H
