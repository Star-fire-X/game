#include "network/kcp_session.h"

#include <algorithm>
#include <cstring>
#include <random>

extern "C" {
#include "common/3rd_party/ikcp.h"
}

#include "common/protocol/packet_codec.h"
#include "common/time_utils.h"
#include "log/logger.h"
#include "monitor/metrics.h"
#include "network/tcp_session.h"

namespace mir2::network {

namespace {

std::array<uint8_t, KcpSession::kTokenSize> TokenFromString(const std::string& token) {
  std::array<uint8_t, KcpSession::kTokenSize> bytes{};
  if (!token.empty()) {
    const size_t copy_len = std::min(token.size(), bytes.size());
    std::memcpy(bytes.data(), token.data(), copy_len);
  }
  return bytes;
}

}  // namespace

KcpSession::KcpSession(uint32_t conv_id,
                       const std::array<uint8_t, kTokenSize>& token,
                       mir2::common::KcpConfig config)
    : conv_id_(conv_id),
      token_(token),
      config_(config) {
  last_active_ms_.store(mir2::common::now_ms());
  if (!ValidateConvId(conv_id_)) {
    SYSLOG_WARN("Invalid KCP conv id {}", conv_id_);
    return;
  }

  kcp_ = ikcp_create(conv_id_, this);
  if (!kcp_) {
    SYSLOG_ERROR("Failed to create KCP control block for conv {}", conv_id_);
    return;
  }

  ConfigureKcp();
  ikcp_setoutput(kcp_, &KcpSession::KcpOutput);
}

KcpSession::KcpSession(uint32_t conv_id,
                       const std::string& token,
                       mir2::common::KcpConfig config)
    : KcpSession(conv_id, TokenFromString(token), config) {
}

KcpSession::~KcpSession() {
  std::lock_guard<std::mutex> lock(kcp_mutex_);
  if (kcp_) {
    ikcp_release(kcp_);
    kcp_ = nullptr;
  }
}

uint32_t KcpSession::AllocateConvId() {
  static thread_local std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<uint32_t> dist(1, UINT32_MAX);
  return dist(rng);
}

bool KcpSession::ValidateConvId(uint32_t conv_id) {
  return conv_id != 0;
}

void KcpSession::BindTcpSession(const std::shared_ptr<TcpSession>& session) {
  tcp_session_ = session;
}

std::shared_ptr<TcpSession> KcpSession::GetTcpSession() const {
  return tcp_session_.lock();
}

void KcpSession::SetOutputHandler(OutputHandler handler) {
  std::lock_guard<std::mutex> lock(handler_mutex_);
  output_handler_ = std::move(handler);
}

void KcpSession::SetMessageHandler(MessageHandler handler) {
  std::lock_guard<std::mutex> lock(handler_mutex_);
  message_handler_ = std::move(handler);
}

void KcpSession::SetRemoteEndpoint(const asio::ip::udp::endpoint& endpoint) {
  {
    std::lock_guard<std::mutex> lock(endpoint_mutex_);
    remote_endpoint_ = endpoint;
  }
  has_endpoint_.store(true, std::memory_order_relaxed);
}

asio::ip::udp::endpoint KcpSession::GetRemoteEndpoint() const {
  std::lock_guard<std::mutex> lock(endpoint_mutex_);
  return remote_endpoint_;
}

bool KcpSession::ValidateToken(const uint8_t* token_data, size_t size) const {
  if (!token_data || size != kTokenSize) {
    return false;
  }
  return std::memcmp(token_.data(), token_data, kTokenSize) == 0;
}

void KcpSession::Input(const uint8_t* data, size_t size) {
  if (!kcp_ || !data || size == 0) {
    return;
  }

  last_active_ms_.store(mir2::common::now_ms(), std::memory_order_relaxed);

  {
    std::lock_guard<std::mutex> lock(kcp_mutex_);
    if (!kcp_) {
      return;
    }

    const int ret = ikcp_input(kcp_, reinterpret_cast<const char*>(data),
                               static_cast<long>(size));
    if (ret < 0) {
      monitor::Metrics::Instance().IncrementError("kcp_input");
      return;
    }
  }

  DrainReceive();
}

void KcpSession::Update(uint32_t now_ms) {
  {
    std::lock_guard<std::mutex> lock(kcp_mutex_);
    if (!kcp_) {
      return;
    }

    ikcp_update(kcp_, now_ms);
  }

  DrainReceive();
}

void KcpSession::Send(uint16_t msg_id, const std::vector<uint8_t>& payload) {
  if (!kcp_) {
    return;
  }

  const uint16_t seq = send_sequence_.fetch_add(1, std::memory_order_relaxed);
  auto encoded = PacketCodec::EncodeV2(
      msg_id,
      payload.data(),
      payload.size(),
      seq,
      mir2::common::PacketHeaderV2::kFlagChannelKcp);
  if (encoded.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(kcp_mutex_);
  if (!kcp_) {
    return;
  }
  const int ret = ikcp_send(kcp_, reinterpret_cast<const char*>(encoded.data()),
                            static_cast<int>(encoded.size()));
  if (ret == 0) {
    monitor::Metrics::Instance().IncrementMessagesSent();
  }
}

bool KcpSession::IsTimedOut(int64_t now_ms, int64_t timeout_ms) const {
  const int64_t last_active = last_active_ms_.load(std::memory_order_relaxed);
  return now_ms >= last_active && (now_ms - last_active) >= timeout_ms;
}

int KcpSession::KcpOutput(const char* buf, int len, IKCPCB* /*kcp*/, void* user) {
  if (!buf || len <= 0 || !user) {
    return -1;
  }

  auto* session = static_cast<KcpSession*>(user);
  if (!session->has_endpoint_.load(std::memory_order_relaxed)) {
    return -1;
  }

  OutputHandler handler;
  {
    std::lock_guard<std::mutex> lock(session->handler_mutex_);
    handler = session->output_handler_;
  }
  if (!handler) {
    return -1;
  }

  asio::ip::udp::endpoint endpoint;
  {
    std::lock_guard<std::mutex> lock(session->endpoint_mutex_);
    endpoint = session->remote_endpoint_;
  }

  const size_t header_size = sizeof(session->conv_id_) + kTokenSize;
  std::vector<uint8_t> packet(header_size + static_cast<size_t>(len));
  size_t offset = 0;
  std::memcpy(packet.data() + offset, &session->conv_id_, sizeof(session->conv_id_));
  offset += sizeof(session->conv_id_);
  std::memcpy(packet.data() + offset, session->token_.data(), kTokenSize);
  offset += kTokenSize;
  std::memcpy(packet.data() + offset, buf, static_cast<size_t>(len));

  handler(endpoint, packet.data(), packet.size());
  return 0;
}

void KcpSession::ConfigureKcp() {
  if (!kcp_) {
    return;
  }

  ikcp_nodelay(kcp_, config_.nodelay, config_.interval, config_.resend, config_.nc);
  ikcp_wndsize(kcp_, config_.snd_wnd, config_.rcv_wnd);
  ikcp_setmtu(kcp_, config_.mtu);
}

void KcpSession::DrainReceive() {
  while (true) {
    Packet packet{};
    bool has_packet = false;

    {
      std::lock_guard<std::mutex> lock(kcp_mutex_);
      if (!kcp_) {
        return;
      }

      const int peek_size = ikcp_peeksize(kcp_);
      if (peek_size <= 0) {
        break;
      }

      std::vector<uint8_t> buffer(static_cast<size_t>(peek_size));
      const int recv_size =
          ikcp_recv(kcp_, reinterpret_cast<char*>(buffer.data()), peek_size);
      if (recv_size <= 0) {
        break;
      }

      const size_t recv_len = static_cast<size_t>(recv_size);
      uint16_t sequence = 0;
      uint8_t flags = 0;
      const auto status = PacketCodec::DecodeV2(
          buffer.data(), recv_len, &packet, &sequence, &flags);
      if (status != DecodeStatus::kOk) {
        monitor::Metrics::Instance().IncrementError("kcp_decode");
        continue;
      }

      if (!mir2::common::ValidateChannelFlag(flags, mir2::common::ChannelType::kKcp)) {
        SYSLOG_WARN("KCP channel flag mismatch (conv={})", conv_id_);
        monitor::Metrics::Instance().IncrementError("kcp_channel_flag");
        continue;
      }

      (void)sequence;
      has_packet = true;
    }

    if (!has_packet) {
      continue;
    }

    MessageHandler handler;
    {
      std::lock_guard<std::mutex> lock(handler_mutex_);
      handler = message_handler_;
    }
    if (handler) {
      monitor::Metrics::Instance().IncrementMessagesReceived();
      handler(shared_from_this(), packet);
    }
  }
}

}  // namespace mir2::network
