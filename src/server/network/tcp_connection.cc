#include "network/tcp_connection.h"

#include <asio/bind_executor.hpp>
#include <asio/dispatch.hpp>
#include <asio/post.hpp>

#include <algorithm>
#include <limits>

#include "log/logger.h"
#include "monitor/metrics.h"

namespace mir2::network {

TcpConnection::TcpConnection(std::unique_ptr<SocketAdapter> socket,
                             uint64_t connection_id,
                             size_t max_write_queue_size)
    : TcpConnection(std::move(socket),
                    connection_id,
                    max_write_queue_size,
                    WriteBatchOptions{}) {}

TcpConnection::TcpConnection(std::unique_ptr<SocketAdapter> socket,
                             uint64_t connection_id,
                             size_t max_write_queue_size,
                             WriteBatchOptions write_batch_options)
    : socket_(std::move(socket)),
      write_strand_(asio::make_strand(socket_->GetExecutor())),
      connection_id_(connection_id),
      max_write_queue_size_(max_write_queue_size),
      write_batch_options_(write_batch_options),
      write_batch_timer_(write_strand_) {
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
  SendRaw(std::make_shared<std::vector<uint8_t>>(bytes));
}

void TcpConnection::SendRaw(std::vector<uint8_t>&& bytes) {
  SendRaw(std::make_shared<std::vector<uint8_t>>(std::move(bytes)));
}

void TcpConnection::SendRaw(std::shared_ptr<std::vector<uint8_t>> bytes) {
  if (!bytes || bytes->empty()) {
    return;
  }

  monitor::Metrics::Instance().AddBytesOut(bytes->size());
  auto self = shared_from_this();
  asio::post(write_strand_, [this, self, data = std::move(bytes)]() mutable {
    if (closed_.load(std::memory_order_acquire)) {
      return;
    }
    if (write_queue_.size() >= max_write_queue_size_) {
      SYSLOG_WARN("Write queue full (size={}), closing connection {}",
                  write_queue_.size(), connection_id_);
      Close();
      return;
    }
    queued_write_bytes_ += data->size();
    QueuedWrite queued{};
    queued.bytes = std::move(data);
    queued.size = queued.bytes ? queued.bytes->size() : 0;
    queued.enqueued_at = std::chrono::steady_clock::now();
    write_queue_.push_back(std::move(queued));
    if (writing_.load(std::memory_order_acquire)) {
      if (IsWriteBatchingEnabled() &&
          write_batch_timer_armed_ &&
          queued_write_bytes_ >= EffectiveBatchMaxBytes()) {
        asio::error_code ignored_ec;
        write_batch_timer_.cancel(ignored_ec);
      }
      return;
    }
    if (!writing_.exchange(true, std::memory_order_acq_rel)) {
      if (IsWriteBatchingEnabled()) {
        ScheduleBatchedWrite();
        return;
      }
      DoWrite();
    }
  });
}

void TcpConnection::Close() {
  auto self = shared_from_this();
  asio::dispatch(write_strand_, [this, self]() {
    if (closed_.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    const bool write_in_flight = writing_.load(std::memory_order_acquire);
    asio::error_code timer_ec;
    write_batch_timer_.cancel(timer_ec);
    write_batch_timer_armed_ = false;
    if (write_in_flight) {
      write_queue_.clear();
      queued_write_bytes_ = 0;
    } else {
      write_queue_.clear();
      queued_write_bytes_ = 0;
      active_write_buffer_.clear();
      active_single_write_buffer_.reset();
      active_write_buffers_.clear();
      active_write_const_buffers_.clear();
      writing_.store(false, std::memory_order_release);
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

void TcpConnection::SetWriteBatchOptions(WriteBatchOptions options) {
  auto self = shared_from_this();
  asio::dispatch(write_strand_, [this, self, options]() {
    write_batch_options_.max_batch_bytes = options.max_batch_bytes;
    write_batch_options_.flush_interval_us = std::max<int64_t>(options.flush_interval_us, 0);
    if (!IsWriteBatchingEnabled() && write_batch_timer_armed_) {
      asio::error_code ignored_ec;
      write_batch_timer_.cancel(ignored_ec);
      write_batch_timer_armed_ = false;
    }
  });
}

void TcpConnection::SetLowCopySendEnabled(bool enabled) {
  auto self = shared_from_this();
  asio::dispatch(write_strand_, [this, self, enabled]() {
    low_copy_send_enabled_ = enabled;
  });
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
    writing_.store(false, std::memory_order_release);
    return;
  }

  if (write_batch_timer_armed_) {
    asio::error_code ignored_ec;
    write_batch_timer_.cancel(ignored_ec);
    write_batch_timer_armed_ = false;
  }

  active_write_buffer_.clear();
  active_single_write_buffer_.reset();
  active_write_buffers_.clear();
  active_write_const_buffers_.clear();
  if (!IsWriteBatchingEnabled()) {
    auto front = std::move(write_queue_.front());
    if (queued_write_bytes_ >= front.size) {
      queued_write_bytes_ -= front.size;
    } else {
      queued_write_bytes_ = 0;
    }
    write_queue_.pop_front();
    if (low_copy_send_enabled_) {
      active_single_write_buffer_ = std::move(front.bytes);
    } else if (front.bytes) {
      active_write_buffer_ = std::move(*front.bytes);
    }
  } else if (low_copy_send_enabled_) {
    const size_t batch_limit = EffectiveBatchMaxBytes();
    size_t batched_bytes = 0;
    while (!write_queue_.empty()) {
      auto& front = write_queue_.front();
      if (!front.bytes || front.size == 0) {
        write_queue_.pop_front();
        continue;
      }
      if (!active_write_buffers_.empty() &&
          batched_bytes + front.size > batch_limit) {
        break;
      }
      auto queued = std::move(front);
      write_queue_.pop_front();
      batched_bytes += queued.size;
      if (queued_write_bytes_ >= queued.size) {
        queued_write_bytes_ -= queued.size;
      } else {
        queued_write_bytes_ = 0;
      }
      active_write_buffers_.push_back(std::move(queued.bytes));
    }
    if (!active_write_buffers_.empty()) {
      active_write_const_buffers_.reserve(active_write_buffers_.size());
      for (const auto& bytes : active_write_buffers_) {
        if (bytes && !bytes->empty()) {
          active_write_const_buffers_.push_back(asio::buffer(*bytes));
        }
      }
    }
  } else {
    const size_t batch_limit = EffectiveBatchMaxBytes();
    active_write_buffer_.reserve(std::min(batch_limit, queued_write_bytes_));
    while (!write_queue_.empty()) {
      auto& front = write_queue_.front();
      if (!active_write_buffer_.empty() &&
          active_write_buffer_.size() + front.size > batch_limit) {
        break;
      }
      if (front.bytes) {
        active_write_buffer_.insert(
            active_write_buffer_.end(), front.bytes->begin(), front.bytes->end());
      }
      if (queued_write_bytes_ >= front.size) {
        queued_write_bytes_ -= front.size;
      } else {
        queued_write_bytes_ = 0;
      }
      write_queue_.pop_front();
    }
  }

  if (!active_single_write_buffer_ &&
      active_write_const_buffers_.empty() &&
      active_write_buffer_.empty()) {
    writing_.store(false, std::memory_order_release);
    return;
  }

  auto self = shared_from_this();
  auto on_write_complete = asio::bind_executor(
      write_strand_, [this, self](const asio::error_code& ec, std::size_t) {
        active_write_buffer_.clear();
        active_single_write_buffer_.reset();
        active_write_buffers_.clear();
        active_write_const_buffers_.clear();
        if (closed_.load(std::memory_order_acquire)) {
          writing_.store(false, std::memory_order_release);
          return;
        }

        if (ec) {
          monitor::Metrics::Instance().IncrementError("write");
          writing_.store(false, std::memory_order_release);
          Close();
          return;
        }

        if (!write_queue_.empty()) {
          if (IsWriteBatchingEnabled()) {
            ScheduleBatchedWrite();
          } else {
            DoWrite();
          }
        } else {
          writing_.store(false, std::memory_order_release);
        }
      });

  if (active_single_write_buffer_) {
    socket_->async_write(asio::buffer(*active_single_write_buffer_),
                         std::move(on_write_complete));
    return;
  }

  if (!active_write_const_buffers_.empty()) {
    socket_->async_writev(active_write_const_buffers_, std::move(on_write_complete));
    return;
  }

  socket_->async_write(asio::buffer(active_write_buffer_), std::move(on_write_complete));
}

void TcpConnection::ScheduleBatchedWrite() {
  if (!IsWriteBatchingEnabled()) {
    DoWrite();
    return;
  }
  if (write_queue_.empty()) {
    writing_.store(false, std::memory_order_release);
    return;
  }
  if (queued_write_bytes_ >= EffectiveBatchMaxBytes()) {
    DoWrite();
    return;
  }
  if (write_batch_timer_armed_) {
    return;
  }

  const int64_t flush_us = std::max<int64_t>(write_batch_options_.flush_interval_us, 0);
  if (flush_us == 0) {
    DoWrite();
    return;
  }

  write_batch_timer_armed_ = true;
  auto self = shared_from_this();
  write_batch_timer_.expires_after(std::chrono::microseconds(flush_us));
  write_batch_timer_.async_wait(asio::bind_executor(
      write_strand_,
      [this, self](const asio::error_code& ec) {
        write_batch_timer_armed_ = false;
        if (ec || closed_.load(std::memory_order_acquire) || write_queue_.empty()) {
          return;
        }
        DoWrite();
      }));
}

bool TcpConnection::IsWriteBatchingEnabled() const {
  return write_batch_options_.max_batch_bytes > 0;
}

size_t TcpConnection::EffectiveBatchMaxBytes() const {
  if (!IsWriteBatchingEnabled()) {
    return std::numeric_limits<size_t>::max();
  }
  return std::max<size_t>(write_batch_options_.max_batch_bytes, 1);
}

}  // namespace mir2::network
