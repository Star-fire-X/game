#include "network/tcp_session.h"

#include <chrono>
#include <cstring>

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
// Must be large enough to hold the largest legal frame.
constexpr size_t kMaxReadBufferSize =
    mir2::common::kMaxPayloadSize + PacketHeaderV2::kSize;
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
  const uint16_t sequence = NextSendSequence();
  std::vector<uint8_t> buffer =
      PacketCodec::EncodeV2(msg_id, payload.data(), payload.size(), sequence);
  connection_->SendRaw(std::move(buffer));
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

uint64_t TcpSession::GetAccountId() const {
  return account_id_.load();
}

void TcpSession::SetAccountId(uint64_t account_id) {
  account_id_.store(account_id);
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
  protocol_version_detected_.store(true, std::memory_order_relaxed);
  kcp_upgrade_allowed_.store(version == ProtocolVersion::kV2, std::memory_order_relaxed);
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

void TcpSession::PauseRead() {
  if (!connection_) {
    return;
  }
  connection_->PauseRead();
}

void TcpSession::ResumeRead() {
  if (!connection_) {
    return;
  }
  connection_->ResumeRead();
}

bool TcpSession::IsReadPaused() const {
  if (!connection_) {
    return false;
  }
  return connection_->IsReadPaused();
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

  if (!bypass_rate_limit_.load(std::memory_order_relaxed) &&
      !CheckRateLimit(packet.payload.size())) {
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

size_t TcpSession::BufferedBytes() const {
  if (read_buffer_.size() <= read_offset_) {
    return 0;
  }
  return read_buffer_.size() - read_offset_;
}

void TcpSession::ConsumeBytes(size_t bytes) {
  read_offset_ += bytes;
  if (read_offset_ >= read_buffer_.size()) {
    read_buffer_.clear();
    read_offset_ = 0;
    return;
  }
  CompactReadBufferIfNeeded();
}

void TcpSession::CompactReadBufferIfNeeded(size_t incoming_bytes) {
  if (read_offset_ == 0) {
    return;
  }

  const size_t buffered = BufferedBytes();
  if (buffered == 0) {
    read_buffer_.clear();
    read_offset_ = 0;
    return;
  }

  if (read_offset_ < read_buffer_.size() / 2 &&
      read_buffer_.size() + incoming_bytes <= kMaxReadBufferSize) {
    return;
  }

  std::memmove(read_buffer_.data(),
               read_buffer_.data() + read_offset_,
               buffered);
  read_buffer_.resize(buffered);
  read_offset_ = 0;
}

void TcpSession::HandleBytes(const uint8_t* data, size_t size) {
  if (!data || size == 0) {
    return;
  }
  if (state_.load() != SessionState::kActive) {
    return;
  }

  CompactReadBufferIfNeeded(size);
  const size_t buffered_before_append = BufferedBytes();

  // Prevent unbounded buffer growth from slow or malicious peers.
  if (buffered_before_append >= kMaxReadBufferSize ||
      size > kMaxReadBufferSize - buffered_before_append) {
    SYSLOG_WARN("Read buffer overflow (current={}, incoming={}), closing session {}",
                buffered_before_append, size, GetSessionId());
    Close();
    return;
  }

  read_buffer_.insert(read_buffer_.end(), data, data + size);

  while (true) {
    const size_t buffered = BufferedBytes();
    if (buffered < sizeof(uint32_t)) {
      return;
    }
    const uint8_t* frame = read_buffer_.data() + read_offset_;

    if (!protocol_version_detected_.load(std::memory_order_relaxed)) {
      protocol_version_ = mir2::common::DetectProtocolVersion(frame);
      protocol_version_detected_.store(true, std::memory_order_relaxed);
      kcp_upgrade_allowed_.store(protocol_version_ == ProtocolVersion::kV2,
                                 std::memory_order_relaxed);
    }

    if (protocol_version_ == ProtocolVersion::kV1) {
      SYSLOG_WARN("Rejecting V1 packet: protocol sunset (session_id={}, {}:{})",
                  GetSessionId(),
                  remote_address_,
                  remote_port_);
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
      monitor::Metrics::Instance().IncrementError("decode_header");
      Close();
      return;
    }
    payload_size = header.payload_size;

    if (payload_size > mir2::common::kMaxPayloadSize) {
      monitor::Metrics::Instance().IncrementError("decode_body");
      Close();
      return;
    }

    const size_t packet_size = header_size + payload_size;
    if (buffered < packet_size) {
      return;
    }

    if (!mir2::common::ValidateChannelFlag(
            header.flags, mir2::common::ChannelType::kTcp)) {
      SYSLOG_WARN("Channel flag mismatch: TCP received KCP-flagged packet from session {} ({}:{})",
                  GetSessionId(), remote_address_, remote_port_);
      monitor::Metrics::Instance().IncrementError("channel_flag_mismatch_tcp");
      ConsumeBytes(packet_size);
      continue;
    }

    uint16_t sequence = 0;
    const auto status =
        PacketCodec::DecodeV2(frame, packet_size, &decode_packet_, &sequence);
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

    HandlePacket(connection_id_, decode_packet_);
    if (state_.load() != SessionState::kActive) {
      return;
    }

    ConsumeBytes(packet_size);
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
