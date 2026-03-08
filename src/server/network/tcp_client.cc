#include "network/tcp_client.h"

#include <algorithm>

#include <asio/connect.hpp>
#include <asio/ip/tcp.hpp>
#if defined(ASIO_HAS_LOCAL_SOCKETS)
#include <asio/local/stream_protocol.hpp>
#endif
#include <asio/post.hpp>

namespace mir2::network {

namespace {

constexpr uint16_t kSequenceWindow = 256;

}  // namespace

TcpClient::TcpClient(asio::io_context& io_context)
    : io_context_(io_context),
      send_strand_(asio::make_strand(io_context)) {}

bool TcpClient::Connect(const std::string& host, uint16_t port) {
  if (connected_.load()) {
    return true;
  }

  asio::ip::tcp::resolver resolver(io_context_);
  asio::error_code ec;
  auto endpoints = resolver.resolve(host, std::to_string(port), ec);
  if (ec) {
    return false;
  }

  asio::ip::tcp::socket socket(io_context_);
  asio::connect(socket, endpoints, ec);
  if (ec) {
    return false;
  }

  connection_ = std::make_shared<TcpConnection>(
      std::make_unique<AsioSocketAdapter>(std::move(socket)),
      1,
      std::max<size_t>(write_queue_size_, 1));
  connection_->SetLowCopySendEnabled(low_copy_send_enabled_);
  const uint64_t epoch =
      connection_epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
  ingress_parser_.Reset();
  ingress_parser_.SetProtocolVersion(ProtocolVersion::kV2, /*detected=*/true);
  send_sequence_.store(0, std::memory_order_relaxed);
  recv_sequence_.store(0, std::memory_order_relaxed);
  connection_->SetReadHandler([this, epoch](const uint8_t* data, size_t size) {
    HandleBytes(data, size, epoch);
  });
  connection_->SetDisconnectHandler(
      [this, epoch](uint64_t id) { HandleDisconnect(id, epoch); });
  connection_->Start();
  connected_.store(true);
  return true;
}

bool TcpClient::ConnectUnix(const std::string& socket_path) {
  if (connected_.load()) {
    return true;
  }

#if defined(ASIO_HAS_LOCAL_SOCKETS)
  if (socket_path.empty()) {
    return false;
  }

  asio::error_code ec;
  asio::local::stream_protocol::socket socket(io_context_);
  socket.connect(asio::local::stream_protocol::endpoint(socket_path), ec);
  if (ec) {
    return false;
  }

  connection_ = std::make_shared<TcpConnection>(
      std::make_unique<UdsSocketAdapter>(std::move(socket)),
      1,
      std::max<size_t>(write_queue_size_, 1));
  connection_->SetLowCopySendEnabled(low_copy_send_enabled_);
  const uint64_t epoch =
      connection_epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
  ingress_parser_.Reset();
  ingress_parser_.SetProtocolVersion(ProtocolVersion::kV2, /*detected=*/true);
  send_sequence_.store(0, std::memory_order_relaxed);
  recv_sequence_.store(0, std::memory_order_relaxed);
  connection_->SetReadHandler([this, epoch](const uint8_t* data, size_t size) {
    HandleBytes(data, size, epoch);
  });
  connection_->SetDisconnectHandler(
      [this, epoch](uint64_t id) { HandleDisconnect(id, epoch); });
  connection_->Start();
  connected_.store(true);
  return true;
#else
  (void)socket_path;
  return false;
#endif
}

void TcpClient::Send(uint16_t msg_id, const std::vector<uint8_t>& payload) {
  Send(msg_id, std::vector<uint8_t>(payload));
}

void TcpClient::Send(uint16_t msg_id, std::vector<uint8_t>&& payload) {
  if (!connected_.load(std::memory_order_acquire)) {
    return;
  }
  const uint64_t epoch = connection_epoch_.load(std::memory_order_acquire);

  // Keep sequence assignment and enqueue order serialized to avoid sequence-window
  // false positives when many threads call Send() concurrently.
  asio::post(send_strand_, [this, epoch, msg_id, payload = std::move(payload)]() mutable {
    if (!connection_ || !connected_.load(std::memory_order_acquire) ||
        connection_epoch_.load(std::memory_order_acquire) != epoch) {
      return;
    }

    const uint16_t sequence = send_sequence_.fetch_add(1, std::memory_order_relaxed);
    std::vector<uint8_t> buffer =
        PacketCodec::EncodeV2(msg_id, payload.data(), payload.size(), sequence);
    connection_->SendRaw(std::move(buffer));
  });
}

void TcpClient::SetWriteQueueSize(size_t max_write_queue_size) {
  write_queue_size_ = std::max<size_t>(max_write_queue_size, 1);
}

void TcpClient::SetLowCopySendEnabled(bool enabled) {
  low_copy_send_enabled_ = enabled;
  if (connection_) {
    connection_->SetLowCopySendEnabled(enabled);
  }
}

void TcpClient::AttachConnectionForTest(
    const std::shared_ptr<TcpConnection>& connection,
    bool connected) {
  connection_ = connection;
  const uint64_t epoch =
      connection_epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
  ingress_parser_.Reset();
  ingress_parser_.SetProtocolVersion(ProtocolVersion::kV2, /*detected=*/true);
  send_sequence_.store(0, std::memory_order_relaxed);
  recv_sequence_.store(0, std::memory_order_relaxed);
  if (connection_) {
    connection_->SetLowCopySendEnabled(low_copy_send_enabled_);
    connection_->SetReadHandler([this, epoch](const uint8_t* data, size_t size) {
      HandleBytes(data, size, epoch);
    });
    connection_->SetDisconnectHandler(
        [this, epoch](uint64_t id) { HandleDisconnect(id, epoch); });
  }
  connected_.store(connected, std::memory_order_release);
}

void TcpClient::Close() {
  const bool was_connected = connected_.exchange(false, std::memory_order_acq_rel);
  std::shared_ptr<TcpConnection> old_connection = std::move(connection_);
  connection_epoch_.fetch_add(1, std::memory_order_acq_rel);
  ingress_parser_.Reset();
  ingress_parser_.SetProtocolVersion(ProtocolVersion::kV2, /*detected=*/true);
  if (old_connection) {
    old_connection->Close();
  }
  if (was_connected && disconnect_handler_) {
    disconnect_handler_();
  }
}

void TcpClient::HandleDisconnect(uint64_t /*connection_id*/, uint64_t epoch) {
  if (connection_epoch_.load(std::memory_order_acquire) != epoch) {
    return;
  }
  connection_.reset();
  connected_.store(false);
  if (disconnect_handler_) {
    disconnect_handler_();
  }
}

void TcpClient::HandleBytes(const uint8_t* data, size_t size, uint64_t epoch) {
  if (connection_epoch_.load(std::memory_order_acquire) != epoch) {
    return;
  }
  if (!data || size == 0) {
    return;
  }
  if (!connected_.load(std::memory_order_acquire)) {
    return;
  }

  if (!ingress_parser_.AppendBytes(
          data,
          size,
          mir2::common::kMaxPayloadSize + PacketHeaderV2::kSize)) {
    Close();
    return;
  }

  while (true) {
    const IngressParseResult result = ingress_parser_.NextPacket();
    if (result.action == IngressParseAction::kNeedMoreData) {
      return;
    }
    if (result.action == IngressParseAction::kFatalError) {
      Close();
      return;
    }
    if (!CheckRecvSequence(result.parsed.sequence)) {
      Close();
      return;
    }

    if (packet_handler_) {
      packet_handler_(result.parsed.packet);
    }
  }
}

bool TcpClient::CheckRecvSequence(uint16_t seq) {
  const uint16_t last = recv_sequence_.load(std::memory_order_relaxed);
  const uint16_t forward = static_cast<uint16_t>(seq - last);
  if (forward == 0) {
    return true;
  }
  if (forward < kSequenceWindow) {
    recv_sequence_.store(seq, std::memory_order_relaxed);
    return true;
  }

  const uint16_t backward = static_cast<uint16_t>(last - seq);
  if (backward < kSequenceWindow) {
    return true;
  }

  return false;
}

}  // namespace mir2::network
