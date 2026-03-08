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
// Must be large enough to hold the largest legal frame.
constexpr size_t kMaxReadBufferSize =
    mir2::common::kMaxPayloadSize + PacketHeaderV2::kSize;
constexpr uint16_t kSequenceWindow = 256;

}  // namespace

TcpSession::TcpSession(std::shared_ptr<TcpConnection> connection)
    : connection_(std::move(connection)) {
  if (connection_) {
    send_strand_.emplace(asio::make_strand(connection_->GetExecutor()));
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
  Send(msg_id, std::vector<uint8_t>(payload));
}

void TcpSession::Send(uint16_t msg_id, std::vector<uint8_t>&& payload) {
  if (!connection_ || !send_strand_) {
    return;
  }
  if (state_.load(std::memory_order_acquire) != SessionState::kActive) {
    return;
  }

  PostSend(msg_id, std::move(payload), false);
}

void TcpSession::Close() {
  if (!connection_) {
    return;
  }

  const SessionState state = state_.load(std::memory_order_acquire);
  if (state == SessionState::kClosed) {
    return;
  }
  if (state != SessionState::kClosing) {
    state_.store(SessionState::kClosing, std::memory_order_release);
  }
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
  const SessionState state = state_.load(std::memory_order_acquire);
  if (state == SessionState::kClosed) {
    return;
  }
  if (state != SessionState::kClosing) {
    // Enter closing immediately so in-flight frame loops stop after current packet.
    state_.store(SessionState::kClosing, std::memory_order_release);
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
  PostSend(static_cast<uint16_t>(mir2::common::MsgId::kKick),
           std::move(payload),
           true);
}

void TcpSession::PostSend(uint16_t msg_id,
                          std::vector<uint8_t> payload,
                          bool close_after_send) {
  if (!connection_ || !send_strand_) {
    if (close_after_send) {
      Close();
    }
    return;
  }

  auto self = shared_from_this();
  asio::post(*send_strand_,
             [this,
              self,
              msg_id,
              payload = std::move(payload),
              close_after_send]() mutable {
               if (!connection_ ||
                   state_.load(std::memory_order_acquire) == SessionState::kClosed) {
                 return;
               }

               const uint16_t sequence = NextSendSequence();
               std::vector<uint8_t> buffer =
                   PacketCodec::EncodeV2(msg_id, payload.data(), payload.size(), sequence);
               connection_->SendRaw(std::move(buffer));
               monitor::Metrics::Instance().IncrementMessagesSent();

               if (close_after_send) {
                 Close();
               }
             });
}

void TcpSession::MarkHeartbeat() {
  last_heartbeat_ms_.store(NowMs());
}

void TcpSession::SetProtocolVersion(ProtocolVersion version) {
  ingress_parser_.SetProtocolVersion(version, /*detected=*/true);
  kcp_upgrade_allowed_.store(version == ProtocolVersion::kV2, std::memory_order_relaxed);
}

ProtocolVersion TcpSession::GetProtocolVersion() const {
  return ingress_parser_.GetProtocolVersion();
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

void TcpSession::HandleBytes(const uint8_t* data, size_t size) {
  if (!data || size == 0) {
    return;
  }
  if (state_.load() != SessionState::kActive) {
    return;
  }

  if (!ingress_parser_.AppendBytes(data, size, kMaxReadBufferSize)) {
    SYSLOG_WARN("Read buffer overflow (incoming={}), closing session {}",
                size,
                GetSessionId());
    Close();
    return;
  }

  auto apply_protocol_gate = [this]() {
    if (!ingress_parser_.IsProtocolVersionDetected()) {
      return;
    }
    const bool allow_upgrade =
        ingress_parser_.GetProtocolVersion() == ProtocolVersion::kV2;
    kcp_upgrade_allowed_.store(allow_upgrade, std::memory_order_relaxed);
  };

  while (true) {
    const IngressParseResult result = ingress_parser_.NextPacket();
    apply_protocol_gate();
    if (result.action == IngressParseAction::kNeedMoreData) {
      return;
    }
    if (result.action == IngressParseAction::kFatalError) {
      switch (result.error) {
        case IngressParseError::kLegacyV1:
          SYSLOG_WARN("Rejecting V1 packet: protocol sunset (session_id={}, {}:{})",
                      GetSessionId(),
                      remote_address_,
                      remote_port_);
          break;
        case IngressParseError::kHeaderInvalid:
          monitor::Metrics::Instance().IncrementError("decode_header");
          break;
        case IngressParseError::kPayloadTooLarge:
        case IngressParseError::kDecodeFailed:
          monitor::Metrics::Instance().IncrementError("decode_body");
          break;
        case IngressParseError::kChannelMismatch:
          SYSLOG_WARN(
              "Channel flag mismatch: TCP received KCP-flagged packet from session {} ({}:{})",
              GetSessionId(),
              remote_address_,
              remote_port_);
          monitor::Metrics::Instance().IncrementError("channel_flag_mismatch_tcp");
          break;
        case IngressParseError::kNone:
          break;
      }
      Close();
      return;
    }

    if (!CheckRecvSequence(result.parsed.sequence)) {
      monitor::Metrics::Instance().IncrementError("sequence");
      Close();
      return;
    }

    HandlePacket(connection_id_, result.parsed.packet);
    if (state_.load() != SessionState::kActive) {
      return;
    }
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
