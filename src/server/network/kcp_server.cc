#include "network/kcp_server.h"

#include <chrono>
#include <cstring>
#include <shared_mutex>
#include <vector>

#include "common/time_utils.h"
#include "log/logger.h"
#include "monitor/metrics.h"

namespace mir2::network {

namespace {

constexpr int kSocketBufferSize = 1 * 1024 * 1024;
constexpr int64_t kCleanupIntervalMs = 1000;
constexpr int64_t kHandshakeTimeoutMs = 30000;
constexpr size_t kHeaderSize = sizeof(uint32_t) + KcpSession::kTokenSize;
constexpr size_t kInlineUdpSendCapacity = 2048;
constexpr uint64_t kUdpSendWarnEveryN = 64;
constexpr int64_t kUdpSendWarnIntervalMs = 1000;

struct UdpSendBuffer {
  size_t size = 0;
  std::array<uint8_t, kInlineUdpSendCapacity> inline_data{};
  std::vector<uint8_t> overflow_data;
};

}  // namespace

KcpServer::KcpServer(asio::io_context& io_context, mir2::common::KcpConfig config)
    : io_context_(io_context),
      socket_(io_context),
      update_timer_(io_context),
      config_(config),
      rate_limiter_(),
      blacklist_() {
}

bool KcpServer::Start(const std::string& bind_ip, uint16_t port) {
  asio::error_code ec;
  if (socket_.is_open()) {
    socket_.close(ec);
  }

  socket_.open(asio::ip::udp::v4(), ec);
  if (ec) {
    SYSLOG_ERROR("Failed to open UDP socket: {}", ec.message());
    return false;
  }

  socket_.set_option(asio::socket_base::reuse_address(true), ec);
  socket_.set_option(asio::socket_base::receive_buffer_size(kSocketBufferSize), ec);
  socket_.set_option(asio::socket_base::send_buffer_size(kSocketBufferSize), ec);

  asio::ip::address address = asio::ip::address_v4::any();
  if (!bind_ip.empty()) {
    address = asio::ip::make_address(bind_ip, ec);
    if (ec) {
      SYSLOG_ERROR("Invalid bind IP {}: {}", bind_ip, ec.message());
      socket_.close(ec);
      return false;
    }
  }

  socket_.bind(asio::ip::udp::endpoint(address, port), ec);
  if (ec) {
    SYSLOG_ERROR("Failed to bind UDP {}:{} - {}", bind_ip, port, ec.message());
    socket_.close(ec);
    return false;
  }

  StartReceive();
  StartUpdateTimer();
  SYSLOG_INFO("KCP server listening on UDP {}:{}", bind_ip.empty() ? "0.0.0.0" : bind_ip, port);
  return true;
}

void KcpServer::Stop() {
  asio::error_code ec;
  update_timer_.cancel(ec);
  socket_.close(ec);
  std::unique_lock<std::shared_mutex> lock(mutex_);
  sessions_.clear();
}

uint32_t KcpServer::AllocateConvId() {
  return KcpSession::AllocateConvId();
}

KcpServer::SessionPtr KcpServer::CreateSession(
    uint32_t conv_id,
    const std::array<uint8_t, KcpSession::kTokenSize>& token) {
  auto session = std::make_shared<KcpSession>(conv_id, token, config_);
  if (!AddSession(session)) {
    return nullptr;
  }
  return session;
}

bool KcpServer::AddSession(const SessionPtr& session) {
  if (!session || !KcpSession::ValidateConvId(session->GetConvId())) {
    return false;
  }

  session->SetOutputHandler([this](const asio::ip::udp::endpoint& endpoint,
                                   const uint8_t* data,
                                   size_t size) {
    SendRaw(endpoint, data, size);
  });

  if (message_handler_) {
    session->SetMessageHandler(message_handler_);
  }

  std::unique_lock<std::shared_mutex> lock(mutex_);
  const auto conv_id = session->GetConvId();
  if (sessions_.find(conv_id) != sessions_.end()) {
    return false;
  }
  sessions_[conv_id] = session;
  return true;
}

void KcpServer::RemoveSession(uint32_t conv_id) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  sessions_.erase(conv_id);
}

KcpServer::SessionPtr KcpServer::GetSession(uint32_t conv_id) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = sessions_.find(conv_id);
  if (it != sessions_.end()) {
    return it->second;
  }
  return nullptr;
}

void KcpServer::SetMessageHandler(KcpSession::MessageHandler handler) {
  message_handler_ = std::move(handler);
  std::vector<SessionPtr> sessions;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    sessions.reserve(sessions_.size());
    for (const auto& [_, session] : sessions_) {
      if (session) {
        sessions.push_back(session);
      }
    }
  }

  for (auto& session : sessions) {
    if (session && message_handler_) {
      session->SetMessageHandler(message_handler_);
    }
  }
}

void KcpServer::StartReceive() {
  if (!socket_.is_open()) {
    return;
  }

  socket_.async_receive_from(
      asio::buffer(recv_buffer_), remote_endpoint_,
      [this](const asio::error_code& ec, std::size_t bytes) {
        HandleReceive(ec, bytes);
      });
}

void KcpServer::HandleReceive(const asio::error_code& ec, std::size_t bytes) {
  if (ec) {
    monitor::Metrics::Instance().IncrementError("udp_recv");
    if (socket_.is_open()) {
      StartReceive();
    }
    return;
  }

  if (bytes == 0) {
    if (socket_.is_open()) {
      StartReceive();
    }
    return;
  }

  monitor::Metrics::Instance().AddBytesIn(bytes);

  if (!remote_endpoint_.address().is_v4()) {
    monitor::Metrics::Instance().IncrementError("udp_non_ipv4");
    if (socket_.is_open()) {
      StartReceive();
    }
    return;
  }

  const uint32_t ip = remote_endpoint_.address().to_v4().to_uint();
  const int64_t now_ms = mir2::common::now_ms();
  if (!rate_limiter_.Allow(ip, now_ms)) {
    SYSLOG_DEBUG("Dropping UDP packet due to rate limit (ip={}, port={})",
                 remote_endpoint_.address().to_string(),
                 remote_endpoint_.port());
    monitor::Metrics::Instance().IncrementError("udp_rate_limit");
    if (socket_.is_open()) {
      StartReceive();
    }
    return;
  }

  if (bytes < kHeaderSize) {
    monitor::Metrics::Instance().IncrementError("kcp_short_packet");
    if (socket_.is_open()) {
      StartReceive();
    }
    return;
  }

  uint32_t conv = 0;
  std::memcpy(&conv, recv_buffer_.data(), sizeof(conv));
  const uint8_t* token = recv_buffer_.data() + sizeof(conv);
  const uint8_t* kcp_payload = recv_buffer_.data() + kHeaderSize;
  const size_t kcp_size = bytes - kHeaderSize;

  if (blacklist_.IsBlacklisted(conv, now_ms)) {
    SYSLOG_DEBUG("Dropping KCP packet for blacklisted conv={} (ip={}, port={})",
                 conv,
                 remote_endpoint_.address().to_string(),
                 remote_endpoint_.port());
    monitor::Metrics::Instance().IncrementError("kcp_blacklisted");
    if (socket_.is_open()) {
      StartReceive();
    }
    return;
  }

  auto session = GetSession(conv);
  if (!session) {
    SYSLOG_DEBUG("Dropping KCP packet for unknown conv={} (ip={}, port={})",
                 conv,
                 remote_endpoint_.address().to_string(),
                 remote_endpoint_.port());
    monitor::Metrics::Instance().IncrementError("kcp_unknown_conv");
    if (socket_.is_open()) {
      StartReceive();
    }
    return;
  }

  if (!session->ValidateToken(token, KcpSession::kTokenSize)) {
    SYSLOG_WARN("KCP token validation failed (conv={}, ip={}, port={})",
                conv,
                remote_endpoint_.address().to_string(),
                remote_endpoint_.port());
    blacklist_.RecordFailure(conv, now_ms);
    monitor::Metrics::Instance().IncrementError("kcp_token_invalid");
    if (socket_.is_open()) {
      StartReceive();
    }
    return;
  }

  session->SetRemoteEndpoint(remote_endpoint_);
  session->Input(kcp_payload, kcp_size);

  if (socket_.is_open()) {
    StartReceive();
  }
}

void KcpServer::StartUpdateTimer() {
  update_timer_.expires_after(std::chrono::milliseconds(config_.interval));
  update_timer_.async_wait([this](const asio::error_code& ec) {
    if (ec) {
      return;
    }
    UpdateAllSessions();
    StartUpdateTimer();
  });
}

void KcpServer::UpdateAllSessions() {
  const int64_t now_ms = mir2::common::now_ms();
  const uint32_t now_ms32 = static_cast<uint32_t>(now_ms);

  std::vector<std::pair<uint32_t, SessionPtr>> sessions;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    sessions.reserve(sessions_.size());
    for (const auto& entry : sessions_) {
      sessions.push_back(entry);
    }
  }

  std::vector<uint32_t> expired;
  for (const auto& entry : sessions) {
    const auto& session = entry.second;
    if (!session) {
      continue;
    }
    session->Update(now_ms32);
    const int64_t timeout_ms = session->HasRemoteEndpoint()
                                  ? KcpSession::kDefaultTimeoutMs
                                  : kHandshakeTimeoutMs;
    if (session->IsTimedOut(now_ms, timeout_ms)) {
      expired.push_back(entry.first);
    }
  }

  if (!expired.empty()) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (uint32_t conv : expired) {
      sessions_.erase(conv);
    }
  }

  if (now_ms - last_cleanup_ms_ >= kCleanupIntervalMs) {
    rate_limiter_.Cleanup(now_ms);
    blacklist_.Cleanup(now_ms);
    last_cleanup_ms_ = now_ms;
  }
}

void KcpServer::SendRaw(const asio::ip::udp::endpoint& endpoint,
                        const uint8_t* data,
                        size_t size) {
  if (!socket_.is_open() || !data || size == 0) {
    return;
  }

  monitor::Metrics::Instance().AddBytesOut(size);
  auto on_send = [this, endpoint, size](const asio::error_code& ec, std::size_t /*bytes*/) {
    if (!ec) {
      return;
    }

    monitor::Metrics::Instance().IncrementError("udp_send");

    const uint64_t error_count =
        udp_send_error_count_.fetch_add(1, std::memory_order_relaxed) + 1;
    const int64_t now_ms = mir2::common::now_ms();
    bool should_log = (error_count % kUdpSendWarnEveryN) == 1;

    int64_t last_warn_ms = last_udp_send_warn_ms_.load(std::memory_order_relaxed);
    if (!should_log && now_ms - last_warn_ms >= kUdpSendWarnIntervalMs) {
      should_log = last_udp_send_warn_ms_.compare_exchange_strong(
          last_warn_ms, now_ms, std::memory_order_relaxed);
    } else if (should_log) {
      last_udp_send_warn_ms_.store(now_ms, std::memory_order_relaxed);
    }

    if (should_log) {
      SYSLOG_WARN("UDP send failed (ec={}, endpoint={}:{}, size={}, error_count={})",
                  ec.message(),
                  endpoint.address().to_string(),
                  endpoint.port(),
                  size,
                  error_count);
    }
  };

  auto buffer = std::make_shared<UdpSendBuffer>();
  buffer->size = size;
  if (size <= buffer->inline_data.size()) {
    std::memcpy(buffer->inline_data.data(), data, size);
    socket_.async_send_to(
        asio::buffer(buffer->inline_data.data(), buffer->size),
        endpoint,
        [buffer, on_send](const asio::error_code& ec, std::size_t bytes) {
          on_send(ec, bytes);
        });
    return;
  }

  buffer->overflow_data.assign(data, data + size);
  socket_.async_send_to(
      asio::buffer(buffer->overflow_data),
      endpoint,
      [buffer, on_send](const asio::error_code& ec, std::size_t bytes) {
        on_send(ec, bytes);
      });
}

}  // namespace mir2::network
