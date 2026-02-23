#include "network/tcp_client.h"

#include <cstring>

#include <asio/connect.hpp>
#include <asio/ip/tcp.hpp>
#if defined(ASIO_HAS_LOCAL_SOCKETS)
#include <asio/local/stream_protocol.hpp>
#endif
#include <asio/post.hpp>

namespace mir2::network {

namespace {

constexpr uint16_t kSequenceWindow = 256;
constexpr size_t kServiceWriteQueueSize = 8192;

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
      kServiceWriteQueueSize);
  protocol_version_ = ProtocolVersion::kV2;
  protocol_version_detected_ = true;
  send_sequence_.store(0, std::memory_order_relaxed);
  recv_sequence_.store(0, std::memory_order_relaxed);
  read_buffer_.clear();
  read_offset_ = 0;
  connection_->SetReadHandler([this](const uint8_t* data, size_t size) {
    HandleBytes(data, size);
  });
  connection_->SetDisconnectHandler([this](uint64_t id) { HandleDisconnect(id); });
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
      kServiceWriteQueueSize);
  protocol_version_ = ProtocolVersion::kV2;
  protocol_version_detected_ = true;
  send_sequence_.store(0, std::memory_order_relaxed);
  recv_sequence_.store(0, std::memory_order_relaxed);
  read_buffer_.clear();
  read_offset_ = 0;
  connection_->SetReadHandler([this](const uint8_t* data, size_t size) {
    HandleBytes(data, size);
  });
  connection_->SetDisconnectHandler([this](uint64_t id) { HandleDisconnect(id); });
  connection_->Start();
  connected_.store(true);
  return true;
#else
  (void)socket_path;
  return false;
#endif
}

void TcpClient::Send(uint16_t msg_id, const std::vector<uint8_t>& payload) {
  if (!connected_.load(std::memory_order_acquire)) {
    return;
  }

  // Keep sequence assignment and enqueue order serialized to avoid sequence-window
  // false positives when many threads call Send() concurrently.
  asio::post(send_strand_, [this, msg_id, payload]() {
    if (!connection_ || !connected_.load(std::memory_order_acquire)) {
      return;
    }

    const uint16_t sequence = send_sequence_.fetch_add(1, std::memory_order_relaxed);
    std::vector<uint8_t> buffer =
        PacketCodec::EncodeV2(msg_id, payload.data(), payload.size(), sequence);
    connection_->SendRaw(std::move(buffer));
  });
}

void TcpClient::Close() {
  if (connection_) {
    connection_->Close();
  }
  connected_.store(false);
}

void TcpClient::HandleDisconnect(uint64_t /*connection_id*/) {
  connected_.store(false);
  if (disconnect_handler_) {
    disconnect_handler_();
  }
}

size_t TcpClient::BufferedBytes() const {
  if (read_buffer_.size() <= read_offset_) {
    return 0;
  }
  return read_buffer_.size() - read_offset_;
}

void TcpClient::ConsumeBytes(size_t bytes) {
  read_offset_ += bytes;
  if (read_offset_ >= read_buffer_.size()) {
    read_buffer_.clear();
    read_offset_ = 0;
    return;
  }
  CompactReadBufferIfNeeded();
}

void TcpClient::CompactReadBufferIfNeeded() {
  if (read_offset_ == 0) {
    return;
  }

  const size_t buffered = BufferedBytes();
  if (buffered == 0) {
    read_buffer_.clear();
    read_offset_ = 0;
    return;
  }

  if (read_offset_ < read_buffer_.size() / 2) {
    return;
  }

  std::memmove(read_buffer_.data(),
               read_buffer_.data() + read_offset_,
               buffered);
  read_buffer_.resize(buffered);
  read_offset_ = 0;
}

void TcpClient::HandleBytes(const uint8_t* data, size_t size) {
  if (!data || size == 0) {
    return;
  }

  CompactReadBufferIfNeeded();
  read_buffer_.insert(read_buffer_.end(), data, data + size);

  while (true) {
    const size_t buffered = BufferedBytes();
    if (buffered < sizeof(uint32_t)) {
      return;
    }
    const uint8_t* frame = read_buffer_.data() + read_offset_;

    if (!protocol_version_detected_) {
      protocol_version_ = mir2::common::DetectProtocolVersion(frame);
      protocol_version_detected_ = true;
    }
    if (protocol_version_ == ProtocolVersion::kV1) {
      Close();
      return;
    }

    const size_t header_size = PacketHeaderV2::kSize;
    if (buffered < header_size) {
      return;
    }

    size_t payload_size = 0;
    PacketHeaderV2 header{};
    if (!PacketHeaderV2::FromBytes(frame, header_size, &header) ||
        header.version != PacketHeaderV2::kVersion) {
      Close();
      return;
    }
    payload_size = header.payload_size;

    if (payload_size > mir2::common::kMaxPayloadSize) {
      Close();
      return;
    }

    const size_t packet_size = header_size + payload_size;
    if (buffered < packet_size) {
      return;
    }

    uint16_t sequence = 0;
    const auto status =
        PacketCodec::DecodeV2(frame, packet_size, &decode_packet_, &sequence);
    if (status != DecodeStatus::kOk || !CheckRecvSequence(sequence)) {
      Close();
      return;
    }

    if (packet_handler_) {
      packet_handler_(decode_packet_);
    }

    ConsumeBytes(packet_size);
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
