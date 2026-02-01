#include "network/tcp_connection.h"

#include <asio/dispatch.hpp>
#include <asio/post.hpp>

#include "log/logger.h"
#include "monitor/metrics.h"

namespace mir2::network {

TcpConnection::TcpConnection(std::unique_ptr<SocketAdapter> socket, uint64_t connection_id)
    : socket_(std::move(socket)), connection_id_(connection_id) {
  asio::error_code ec;
  const auto endpoint = socket_->remote_endpoint(ec);
  if (!ec) {
    remote_address_ = endpoint.address().to_string();
    remote_port_ = endpoint.port();
  }
}

void TcpConnection::Start() {
  DoRead();
}

void TcpConnection::SendRaw(const std::vector<uint8_t>& bytes) {
  monitor::Metrics::Instance().AddBytesOut(bytes.size());
  auto self = shared_from_this();
  asio::post(socket_->GetExecutor(), [this, self, data = bytes]() mutable {
    if (write_queue_.size() >= kMaxWriteQueueSize) {
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
    asio::error_code ec;
    socket_->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    socket_->close(ec);
    if (disconnect_handler_) {
      disconnect_handler_(connection_id_);
    }
  });
}

void TcpConnection::SetReadHandler(BytesHandler handler) {
  read_handler_ = std::move(handler);
}

void TcpConnection::SetDisconnectHandler(DisconnectHandler handler) {
  disconnect_handler_ = std::move(handler);
}

void TcpConnection::DoRead() {
  auto self = shared_from_this();
  socket_->async_read_some(
      asio::buffer(read_buffer_),
      [this, self](const asio::error_code& ec, std::size_t bytes) {
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
        DoRead();
      });
}

void TcpConnection::DoWrite() {
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
