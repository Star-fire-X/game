#include "network/tcp_session.h"

#include <chrono>

#include <asio/post.hpp>
#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "server/common/error_codes.h"
#include "log/logger.h"
#include "monitor/metrics.h"
#include "system_generated.h"

namespace mir2::network {

namespace {

constexpr int64_t kRateWindowMs = 1000;
constexpr uint32_t kMaxMessagesPerSec = 50;
constexpr uint32_t kMaxBytesPerSec = 64 * 1024;
constexpr size_t kMaxReadBufferSize = 64 * 1024;
constexpr uint16_t kSequenceWindow = 256;

}  // namespace

TcpSession::TcpSession(std::shared_ptr<TcpConnection> connection)
    : connection_(std::move(connection)) {
  if (connection_) {
    connection_id_ = connection_->GetConnectionId();
    remote_address_ = connection_->GetRemoteAddress();
    remote_port_ = connection_->GetRemotePort();
  }
  const int64_t now_ms = NowMs();
  last_heartbeat_ms_.store(now_ms);
  rate_window_start_ms_.store(now_ms, std::memory_order_relaxed);
}

void TcpSession::Start() {
  if (!connection_) {
    return;
  }

  SessionState expected = SessionState::kInit;
  if (!state_.compare_exchange_strong(expected, SessionState::kActive)) {
    return;
  }

  if (connected_handler_) {
    connected_handler_(shared_from_this());
  }

  auto self = shared_from_this();
  asio::post(connection_->GetExecutor(), [this, self]() {
    if (connection_) {
      connection_->Start();
    }
  });
}

void TcpSession::Send(uint16_t msg_id, const std::vector<uint8_t>& payload) {
  if (!connection_) {
    return;
  }
  if (state_.load() != SessionState::kActive) {
    return;
  }
  std::vector<uint8_t> buffer;
  if (protocol_version_ == ProtocolVersion::kV2) {
    const uint16_t sequence = NextSendSequence();
    buffer = PacketCodec::EncodeV2(msg_id, payload.data(), payload.size(), sequence);
  } else {
    buffer = PacketCodec::Encode(msg_id, payload.data(), payload.size());
  }
  connection_->SendRaw(buffer);
  monitor::Metrics::Instance().IncrementMessagesSent();
}

void TcpSession::Close() {
  if (!connection_) {
    return;
  }

  SessionState state = state_.load();
  if (state == SessionState::kClosing || state == SessionState::kClosed) {
    return;
  }

  state_.store(SessionState::kClosing);
  connection_->Close();
}

uint64_t TcpSession::GetUserId() const {
  return user_id_.load();
}

void TcpSession::SetUserId(uint64_t user_id) {
  user_id_.store(user_id);
}

void TcpSession::Kick(mir2::common::ErrorCode reason, const std::string& text) {
  if (!connection_) {
    return;
  }

  flatbuffers::FlatBufferBuilder builder;
  const auto message_offset = builder.CreateString(text);
  const auto reason_text_offset = builder.CreateString(mir2::common::ToString(reason));
  const auto kick = mir2::proto::CreateKick(
      builder,
      static_cast<mir2::proto::ErrorCode>(static_cast<uint16_t>(reason)),
      message_offset,
      reason_text_offset);
  builder.Finish(kick);

  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());
  Send(static_cast<uint16_t>(mir2::common::MsgId::kKick), payload);
  Close();
}

void TcpSession::MarkHeartbeat() {
  last_heartbeat_ms_.store(NowMs());
}

void TcpSession::SetProtocolVersion(ProtocolVersion version) {
  protocol_version_ = version;
}

ProtocolVersion TcpSession::GetProtocolVersion() const {
  return protocol_version_;
}

uint16_t TcpSession::NextSendSequence() {
  return send_sequence_.fetch_add(1, std::memory_order_seq_cst);
}

bool TcpSession::CheckRecvSequence(uint16_t seq) {
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

void TcpSession::HandlePacket(uint64_t connection_id, const Packet& packet) {
  if (connection_id != connection_id_) {
    return;
  }

  if (state_.load() != SessionState::kActive) {
    return;
  }

  MarkHeartbeat();
  monitor::Metrics::Instance().IncrementMessagesReceived();

  if (!CheckRateLimit(packet.payload.size())) {
    Close();
    return;
  }

  if (message_handler_) {
    message_handler_(shared_from_this(), packet);
  }
}

void TcpSession::HandleDisconnect(uint64_t connection_id) {
  if (connection_id != connection_id_) {
    return;
  }

  SessionState previous = state_.exchange(SessionState::kClosed);
  if (previous == SessionState::kClosed) {
    return;
  }

  if (disconnected_handler_) {
    disconnected_handler_(shared_from_this());
  }
}

void TcpSession::HandleBytes(const uint8_t* data, size_t size) {
  if (!data || size == 0) {
    return;
  }
  if (state_.load() != SessionState::kActive) {
    return;
  }

  // Prevent unbounded buffer growth from slow or malicious peers.
  if (read_buffer_.size() >= kMaxReadBufferSize ||
      size > kMaxReadBufferSize - read_buffer_.size()) {
    SYSLOG_WARN("Read buffer overflow (current={}, incoming={}), closing session {}",
                read_buffer_.size(), size, GetSessionId());
    Close();
    return;
  }

  read_buffer_.insert(read_buffer_.end(), data, data + size);

  while (true) {
    if (read_buffer_.size() < sizeof(uint32_t)) {
      return;
    }

    if (!protocol_version_detected_) {
      protocol_version_ = mir2::common::DetectProtocolVersion(read_buffer_.data());
      protocol_version_detected_ = true;
    }

    const size_t header_size = protocol_version_ == ProtocolVersion::kV2
                                   ? PacketHeaderV2::kSize
                                   : PacketHeader::kSize;
    if (read_buffer_.size() < header_size) {
      return;
    }

    size_t payload_size = 0;
    if (protocol_version_ == ProtocolVersion::kV2) {
      PacketHeaderV2 header{};
      if (!PacketHeaderV2::FromBytes(read_buffer_.data(), header_size, &header) ||
          header.version != PacketHeaderV2::kVersion) {
        monitor::Metrics::Instance().IncrementError("decode_header");
        Close();
        return;
      }
      payload_size = header.payload_size;
    } else {
      PacketHeader header{};
      if (!PacketHeader::FromBytes(read_buffer_.data(), header_size, &header)) {
        monitor::Metrics::Instance().IncrementError("decode_header");
        Close();
        return;
      }
      payload_size = header.payload_size;
    }

    if (payload_size > mir2::common::kMaxPayloadSize) {
      monitor::Metrics::Instance().IncrementError("decode_body");
      Close();
      return;
    }

    const size_t packet_size = header_size + payload_size;
    if (read_buffer_.size() < packet_size) {
      return;
    }

    Packet packet{};
    if (protocol_version_ == ProtocolVersion::kV2) {
      uint16_t sequence = 0;
      const auto status =
          PacketCodec::DecodeV2(read_buffer_.data(), packet_size, &packet, &sequence);
      if (status != DecodeStatus::kOk) {
        monitor::Metrics::Instance().IncrementError("decode_body");
        Close();
        return;
      }
      if (!CheckRecvSequence(sequence)) {
        monitor::Metrics::Instance().IncrementError("sequence");
        Close();
        return;
      }
    } else {
      const auto status = PacketCodec::Decode(read_buffer_.data(), packet_size, &packet);
      if (status != DecodeStatus::kOk) {
        monitor::Metrics::Instance().IncrementError("decode_body");
        Close();
        return;
      }
    }

    HandlePacket(connection_id_, packet);
    if (state_.load() != SessionState::kActive) {
      return;
    }

    read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + packet_size);
  }
}

int64_t TcpSession::NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

bool TcpSession::CheckRateLimit(size_t payload_size) {
  const int64_t now_ms = NowMs();
  int64_t window_start = rate_window_start_ms_.load(std::memory_order_relaxed);
  if (now_ms - window_start >= kRateWindowMs) {
    if (rate_window_start_ms_.compare_exchange_strong(
            window_start, now_ms, std::memory_order_relaxed)) {
      rate_msg_count_.store(0, std::memory_order_relaxed);
      rate_bytes_count_.store(0, std::memory_order_relaxed);
      rate_limited_.store(false, std::memory_order_relaxed);
    }
  }

  const uint32_t payload_size32 = static_cast<uint32_t>(payload_size);
  const uint32_t msg_count =
      rate_msg_count_.fetch_add(1, std::memory_order_relaxed);
  const uint32_t bytes_count =
      rate_bytes_count_.fetch_add(payload_size32, std::memory_order_relaxed);
  if (msg_count + 1 > kMaxMessagesPerSec ||
      bytes_count + payload_size32 > kMaxBytesPerSec) {
    rate_limited_.store(true, std::memory_order_relaxed);
    return false;
  }
  return true;
}

}  // namespace mir2::network
