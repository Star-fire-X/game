#include "network/tcp_connection.h"

#include <asio/dispatch.hpp>
#include <asio/post.hpp>

#include "log/logger.h"
#include "monitor/metrics.h"

namespace mir2::network {

TcpConnection::TcpConnection(std::unique_ptr<SocketAdapter> socket,
                             uint64_t connection_id,
                             size_t max_write_queue_size)
    : socket_(std::move(socket)),
      connection_id_(connection_id),
      max_write_queue_size_(max_write_queue_size) {
  asio::error_code ec;
  const auto endpoint = socket_->remote_endpoint(ec);
  if (!ec) {
    remote_address_ = endpoint.address().to_string();
    remote_port_ = endpoint.port();
  }
}

void TcpConnection::Start() {
  closed_.store(false, std::memory_order_release);
  read_paused_.store(false, std::memory_order_release);
  DoRead();
}

void TcpConnection::SendRaw(const std::vector<uint8_t>& bytes) {
  SendRaw(std::vector<uint8_t>(bytes));
}

void TcpConnection::SendRaw(std::vector<uint8_t>&& bytes) {
  monitor::Metrics::Instance().AddBytesOut(bytes.size());
  auto self = shared_from_this();
  asio::post(socket_->GetExecutor(), [this, self, data = std::move(bytes)]() mutable {
    if (write_queue_.size() >= max_write_queue_size_) {
      SYSLOG_WARN("Write queue full (size={}), closing connection {}",
                  write_queue_.size(), connection_id_);
      Close();
      return;
    }
    write_queue_.push_back(std::move(data));
    if (!writing_.exchange(true)) {
      DoWrite();
    }
  });
}

void TcpConnection::Close() {
  auto self = shared_from_this();
  asio::dispatch(socket_->GetExecutor(), [this, self]() {
    if (closed_.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    asio::error_code ec;
    socket_->shutdown(asio::socket_base::shutdown_both, ec);
    socket_->close(ec);
    if (disconnect_handler_) {
      disconnect_handler_(connection_id_);
    }
  });
}

void TcpConnection::SetReadHandler(BytesHandler handler) {
  read_handler_ = std::move(handler);
}

void TcpConnection::PauseRead() {
  auto self = shared_from_this();
  asio::post(socket_->GetExecutor(), [this, self]() {
    read_paused_.store(true, std::memory_order_release);
  });
}

void TcpConnection::ResumeRead() {
  auto self = shared_from_this();
  asio::post(socket_->GetExecutor(), [this, self]() {
    read_paused_.store(false, std::memory_order_release);
    if (closed_.load(std::memory_order_acquire)) {
      return;
    }
    if (!read_in_flight_.load(std::memory_order_acquire)) {
      DoRead();
    }
  });
}

void TcpConnection::SetDisconnectHandler(DisconnectHandler handler) {
  disconnect_handler_ = std::move(handler);
}

void TcpConnection::DoRead() {
  if (closed_.load(std::memory_order_acquire) ||
      read_paused_.load(std::memory_order_acquire)) {
    return;
  }
  if (read_in_flight_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }

  auto self = shared_from_this();
  socket_->async_read_some(
      asio::buffer(read_buffer_),
      [this, self](const asio::error_code& ec, std::size_t bytes) {
        read_in_flight_.store(false, std::memory_order_release);

        if (ec) {
          monitor::Metrics::Instance().IncrementError("read");
          Close();
          return;
        }

        if (bytes == 0) {
          Close();
          return;
        }

        monitor::Metrics::Instance().AddBytesIn(bytes);
        if (read_handler_) {
          read_handler_(read_buffer_.data(), bytes);
        }
        if (!closed_.load(std::memory_order_acquire) &&
            !read_paused_.load(std::memory_order_acquire)) {
          DoRead();
        }
      });
}

void TcpConnection::DoWrite() {
  if (closed_.load(std::memory_order_acquire)) {
    writing_.store(false, std::memory_order_release);
    return;
  }
  if (write_queue_.empty()) {
    writing_.store(false);
    return;
  }

  auto self = shared_from_this();
  socket_->async_write(asio::buffer(write_queue_.front()),
                       [this, self](const asio::error_code& ec, std::size_t) {
                         if (ec) {
                           monitor::Metrics::Instance().IncrementError("write");
                           Close();
                           return;
                         }

                         write_queue_.pop_front();
                         if (!write_queue_.empty()) {
                           DoWrite();
                         } else {
                           writing_.store(false);
                         }
                       });
}

}  // namespace mir2::network
