#include "client/network/dual_channel_client.h"

#include <algorithm>
#include <cstring>

#include "common/time_utils.h"

namespace mir2::client {

namespace {

mir2::common::FallbackController::Config BuildFallbackConfig(
    const mir2::common::KcpConfig& config) {
  mir2::common::FallbackController::Config fallback_config{};
  fallback_config.handshake_timeout_ms = config.timeout_ms;
  fallback_config.recovery_interval_ms = config.recovery_interval_ms;
  fallback_config.max_recovery_interval_ms = 300000;
  return fallback_config;
}

}  // namespace

DualChannelClient::DualChannelClient()
    : DualChannelClient(mir2::common::KcpConfig{}) {
}

DualChannelClient::DualChannelClient(mir2::common::KcpConfig config)
    : io_context_(),
      io_work_guard_(asio::make_work_guard(io_context_)),
      tcp_client_(std::make_unique<NetworkClient>()),
      kcp_channel_(std::make_unique<KcpChannel>(io_context_, config)),
      fallback_(BuildFallbackConfig(config)),
      kcp_upgrade_handler_(config) {
  StartIoThread();
  wire_tcp_callbacks();

  kcp_upgrade_handler_.SetTcpSender([this](uint16_t msg_id,
                                           const std::vector<uint8_t>& payload) {
    if (tcp_client_) {
      tcp_client_->send(msg_id, payload);
    }
  });
  kcp_upgrade_handler_.SetKcpSender([this](uint16_t msg_id,
                                           const std::vector<uint8_t>& payload) {
    if (kcp_channel_) {
      kcp_channel_->Send(msg_id, payload);
    }
  });
  kcp_upgrade_handler_.SetSessionCallback(
      [this](uint32_t conv_id, const std::string& token, uint16_t udp_port) {
        set_kcp_session(conv_id, token, udp_port);
      });
  kcp_upgrade_handler_.SetConfirmedCallback([this]() { mark_kcp_confirmed(); });
  kcp_upgrade_handler_.SetFailedCallback([this]() { mark_kcp_failed(); });
  kcp_upgrade_handler_.SetDisconnectCallback([this]() { notify_kcp_disconnect(); });
  if (kcp_channel_) {
    kcp_channel_->SetOnMessage([this](const NetworkPacket& packet) {
      handle_kcp_packet(packet);
    });
  }
}

DualChannelClient::DualChannelClient(std::unique_ptr<NetworkClient> tcp_client,
                                     std::unique_ptr<KcpChannel> kcp_channel,
                                     mir2::common::KcpConfig config)
    : io_context_(),
      io_work_guard_(asio::make_work_guard(io_context_)),
      tcp_client_(std::move(tcp_client)),
      kcp_channel_(std::move(kcp_channel)),
      fallback_(BuildFallbackConfig(config)),
      kcp_upgrade_handler_(config) {
  StartIoThread();
  wire_tcp_callbacks();

  kcp_upgrade_handler_.SetTcpSender([this](uint16_t msg_id,
                                           const std::vector<uint8_t>& payload) {
    if (tcp_client_) {
      tcp_client_->send(msg_id, payload);
    }
  });
  kcp_upgrade_handler_.SetKcpSender([this](uint16_t msg_id,
                                           const std::vector<uint8_t>& payload) {
    if (kcp_channel_) {
      kcp_channel_->Send(msg_id, payload);
    }
  });
  kcp_upgrade_handler_.SetSessionCallback(
      [this](uint32_t conv_id, const std::string& token, uint16_t udp_port) {
        set_kcp_session(conv_id, token, udp_port);
      });
  kcp_upgrade_handler_.SetConfirmedCallback([this]() { mark_kcp_confirmed(); });
  kcp_upgrade_handler_.SetFailedCallback([this]() { mark_kcp_failed(); });
  kcp_upgrade_handler_.SetDisconnectCallback([this]() { notify_kcp_disconnect(); });
  if (kcp_channel_) {
    kcp_channel_->SetOnMessage([this](const NetworkPacket& packet) {
      handle_kcp_packet(packet);
    });
  }
}

DualChannelClient::~DualChannelClient() {
  disconnect();
  StopIoThread();
}

bool DualChannelClient::connect(const std::string& host, uint16_t port) {
  if (!tcp_client_) {
    return false;
  }

  host_ = host;
  tcp_port_ = port;
  kcp_state_.store(KcpUpgradeState::kIdle, std::memory_order_relaxed);
  fallback_.Reset();
  kcp_upgrade_handler_.Reset();

  {
    std::lock_guard<std::mutex> lock(kcp_info_mutex_);
    kcp_info_ = KcpSessionInfo{};
  }

  if (kcp_channel_) {
    kcp_channel_->Disconnect();
  }

  {
    std::lock_guard<std::mutex> lock(receive_mutex_);
    std::queue<NetworkPacket> empty;
    std::swap(receive_queue_, empty);
  }

  return tcp_client_->connect(host, port);
}

void DualChannelClient::disconnect() {
  if (kcp_channel_) {
    kcp_channel_->Disconnect();
  }
  if (tcp_client_) {
    tcp_client_->disconnect();
  }

  kcp_state_.store(KcpUpgradeState::kIdle, std::memory_order_relaxed);
  fallback_.Reset();
  kcp_upgrade_handler_.Reset();
}

bool DualChannelClient::is_connected() const {
  return tcp_client_ && tcp_client_->is_connected();
}

void DualChannelClient::send(uint16_t msg_id, const std::vector<uint8_t>& payload) {
  if (!tcp_client_) {
    return;
  }

  const bool kcp_available = is_kcp_available();
  const auto channel = router_.GetChannelWithFallback(msg_id, kcp_available);
  if (channel == mir2::common::ChannelType::kKcp && kcp_available && kcp_channel_) {
    kcp_channel_->Send(msg_id, payload);
    return;
  }

  tcp_client_->send(msg_id, payload);
}

std::optional<NetworkPacket> DualChannelClient::receive() {
  std::lock_guard<std::mutex> lock(receive_mutex_);
  if (receive_queue_.empty()) {
    return std::nullopt;
  }

  NetworkPacket packet = std::move(receive_queue_.front());
  receive_queue_.pop();
  return packet;
}

void DualChannelClient::update() {
  if (tcp_client_) {
    tcp_client_->update();
  }
  if (kcp_channel_) {
    kcp_channel_->Update();
  }

  const int64_t now_ms = mir2::common::now_ms();
  kcp_upgrade_handler_.Update(now_ms,
                              tcp_client_ && tcp_client_->is_connected(),
                              kcp_channel_ && kcp_channel_->IsConnected(),
                              kcp_state_.load(std::memory_order_relaxed) ==
                                  KcpUpgradeState::kConfirmed);
  process_fallback(now_ms);

  // Message dispatch contract:
  // - Callback mode: on_message_ is set, update() drains the queue.
  // - Polling mode: on_message_ is unset, receive() owns the queue.
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (!on_message_) {
      return;
    }
  }

  while (true) {
    std::optional<NetworkPacket> packet;
    {
      std::lock_guard<std::mutex> lock(receive_mutex_);
      if (receive_queue_.empty()) {
        break;
      }
      packet = std::move(receive_queue_.front());
      receive_queue_.pop();
    }

    if (packet) {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      if (on_message_) {
        on_message_(*packet);
      }
    }
  }
}

ConnectionState DualChannelClient::get_state() const {
  if (!tcp_client_) {
    return ConnectionState::DISCONNECTED;
  }
  return tcp_client_->get_state();
}

ErrorCode DualChannelClient::get_last_error() const {
  if (!tcp_client_) {
    return ErrorCode::CONNECTION_FAILED;
  }
  return tcp_client_->get_last_error();
}

void DualChannelClient::set_on_message(MessageCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  on_message_ = std::move(callback);
}

void DualChannelClient::set_on_disconnect(EventCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  on_disconnect_ = std::move(callback);
}

void DualChannelClient::set_on_connect(EventCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  on_connect_ = std::move(callback);
}

void DualChannelClient::set_kcp_session(uint32_t conv_id,
                                        const std::string& token,
                                        uint16_t udp_port) {
  std::array<uint8_t, KcpChannel::kTokenSize> token_bytes{};
  if (!token.empty()) {
    const size_t copy_len = std::min(token.size(), token_bytes.size());
    std::memcpy(token_bytes.data(), token.data(), copy_len);
  }
  set_kcp_session(conv_id, token_bytes, udp_port);
}

void DualChannelClient::set_kcp_session(uint32_t conv_id,
                                        const std::array<uint8_t, KcpChannel::kTokenSize>& token,
                                        uint16_t udp_port) {
  {
    std::lock_guard<std::mutex> lock(kcp_info_mutex_);
    kcp_info_.conv_id = conv_id;
    kcp_info_.token = token;
    kcp_info_.udp_port = udp_port;
    kcp_info_.valid = true;
  }

  kcp_upgrade_handler_.ResetHeartbeat();
  kcp_state_.store(KcpUpgradeState::kRequested, std::memory_order_relaxed);
  fallback_.StartHandshake(mir2::common::now_ms());
  attempt_kcp_connect();
}

void DualChannelClient::mark_kcp_confirmed() {
  kcp_state_.store(KcpUpgradeState::kConfirmed, std::memory_order_relaxed);
  fallback_.OnKcpHandshakeSuccess();
}

void DualChannelClient::mark_kcp_failed() {
  kcp_state_.store(KcpUpgradeState::kFailed, std::memory_order_relaxed);
  fallback_.OnKcpHandshakeTimeout(mir2::common::now_ms());
  kcp_upgrade_handler_.ResetHeartbeat();
  reset_kcp_session_info();
}

void DualChannelClient::notify_kcp_disconnect() {
  kcp_state_.store(KcpUpgradeState::kFailed, std::memory_order_relaxed);
  fallback_.OnKcpDisconnect(mir2::common::now_ms());
  kcp_upgrade_handler_.ResetHeartbeat();
  reset_kcp_session_info();
  if (kcp_channel_) {
    kcp_channel_->Disconnect();
  }
}

void DualChannelClient::reset_kcp_session() {
  if (kcp_channel_) {
    kcp_channel_->ResetSession();
  }

  kcp_state_.store(KcpUpgradeState::kIdle, std::memory_order_relaxed);
  reset_kcp_session_info();
  fallback_.Reset();
  kcp_upgrade_handler_.Reset();

  if (tcp_client_ && tcp_client_->is_connected()) {
    kcp_upgrade_handler_.Update(mir2::common::now_ms(), true, false, false);
  }
}

DualChannelClient::KcpUpgradeState DualChannelClient::get_kcp_state() const {
  return kcp_state_.load(std::memory_order_relaxed);
}

void DualChannelClient::set_route(uint16_t msg_id, mir2::common::ChannelType channel) {
  router_.SetRoute(msg_id, channel);
}

void DualChannelClient::set_use_v2_protocol(bool enable) {
  if (tcp_client_) {
    tcp_client_->set_use_v2_protocol(enable);
  }
}

bool DualChannelClient::use_v2_protocol() const {
  return tcp_client_ ? tcp_client_->use_v2_protocol() : true;
}

void DualChannelClient::wire_tcp_callbacks() {
  if (!tcp_client_) {
    return;
  }

  tcp_client_->set_on_message([this](const NetworkPacket& packet) {
    if (kcp_upgrade_handler_.HandleTcpPacket(packet)) {
      return;
    }
    enqueue_packet(packet);
  });

  tcp_client_->set_on_connect([this]() {
    kcp_upgrade_handler_.OnTcpConnected();
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (on_connect_) {
      on_connect_();
    }
  });

  tcp_client_->set_on_disconnect([this]() {
    if (kcp_channel_) {
      kcp_channel_->Disconnect();
    }
    kcp_state_.store(KcpUpgradeState::kIdle, std::memory_order_relaxed);
    fallback_.Reset();
    kcp_upgrade_handler_.Reset();
    reset_kcp_session_info();

    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (on_disconnect_) {
      on_disconnect_();
    }
  });
}

void DualChannelClient::enqueue_packet(const NetworkPacket& packet) {
  std::lock_guard<std::mutex> lock(receive_mutex_);
  if (receive_queue_.size() >= kMaxReceiveQueueSize) {
    return;
  }
  receive_queue_.push(packet);
}

void DualChannelClient::StartIoThread() {
  if (io_thread_.joinable()) {
    return;
  }
  if (!io_work_guard_) {
    io_work_guard_.emplace(io_context_.get_executor());
  }
  io_thread_ = std::thread([this]() {
    io_context_.run();
  });
}

void DualChannelClient::StopIoThread() {
  io_work_guard_.reset();
  io_context_.stop();
  if (io_thread_.joinable()) {
    io_thread_.join();
  }
}

void DualChannelClient::handle_kcp_packet(const NetworkPacket& packet) {
  if (kcp_upgrade_handler_.HandleKcpPacket(packet)) {
    return;
  }
  if (kcp_state_.load(std::memory_order_relaxed) != KcpUpgradeState::kConfirmed) {
    mark_kcp_confirmed();
  }
  enqueue_packet(packet);
}

void DualChannelClient::attempt_kcp_connect() {
  if (!kcp_channel_ || host_.empty()) {
    return;
  }

  KcpSessionInfo info;
  {
    std::lock_guard<std::mutex> lock(kcp_info_mutex_);
    info = kcp_info_;
  }

  if (!info.valid || info.udp_port == 0) {
    return;
  }

  kcp_channel_->SetConvId(info.conv_id);
  kcp_channel_->SetSessionToken(info.token);
  if (!kcp_channel_->Connect(host_, info.udp_port)) {
    mark_kcp_failed();
  }
}

void DualChannelClient::reset_kcp_session_info() {
  std::lock_guard<std::mutex> lock(kcp_info_mutex_);
  kcp_info_ = KcpSessionInfo{};
}

void DualChannelClient::process_fallback(int64_t now_ms) {
  const auto state = kcp_state_.load(std::memory_order_relaxed);
  if (state == KcpUpgradeState::kRequested && fallback_.IsHandshakeTimedOut(now_ms)) {
    mark_kcp_failed();
    if (kcp_channel_) {
      kcp_channel_->Disconnect();
    }
    return;
  }

  if (fallback_.ShouldAttemptRecovery(now_ms) && tcp_client_ && tcp_client_->is_connected()) {
    fallback_.OnRecoveryAttempt(now_ms);
    kcp_state_.store(KcpUpgradeState::kIdle, std::memory_order_relaxed);
    reset_kcp_session_info();
    kcp_upgrade_handler_.RequestUpgrade();
  }
}

bool DualChannelClient::is_kcp_available() const {
  if (!kcp_channel_) {
    return false;
  }
  if (!kcp_channel_->IsConnected()) {
    return false;
  }
  if (kcp_state_.load(std::memory_order_relaxed) != KcpUpgradeState::kConfirmed) {
    return false;
  }
  return fallback_.IsKcpAllowed();
}

}  // namespace mir2::client
