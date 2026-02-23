#include "client/network/kcp_channel.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

extern "C" {
#include "common/3rd_party/ikcp.h"
}

namespace mir2::client {

KcpChannel::KcpChannel(asio::io_context& io_context,
                       mir2::common::KcpConfig config)
    : KcpChannel(io_context, config, nullptr) {
}

KcpChannel::KcpChannel(asio::io_context& io_context,
                       mir2::common::KcpConfig config,
                       std::unique_ptr<IUdpTransport> transport)
    : config_(config),
      io_context_(io_context),
      transport_(std::move(transport)),
      update_timer_(io_context_) {
  if (!transport_) {
    transport_ = std::make_unique<UdpTransport>(io_context_);
  }
}

KcpChannel::~KcpChannel() {
  Disconnect();
}

bool KcpChannel::Connect(const std::string& host, uint16_t port) {
  if (state_ != mir2::common::ChannelState::Disconnected) {
    Disconnect();
  }

  state_ = mir2::common::ChannelState::Connecting;

  try {
    if (io_context_.stopped()) {
      io_context_.restart();
    }

    if (!transport_->Bind(0)) {
      state_ = mir2::common::ChannelState::Disconnected;
      return false;
    }

    asio::ip::udp::resolver resolver(io_context_);
    auto results = resolver.resolve(asio::ip::udp::v4(), host, std::to_string(port));
    if (results.begin() == results.end()) {
      transport_->Close();
      state_ = mir2::common::ChannelState::Disconnected;
      return false;
    }
    remote_endpoint_ = *results.begin();

    {
      std::lock_guard<std::mutex> lock(kcp_mutex_);
      if (kcp_) {
        ikcp_release(kcp_);
        kcp_ = nullptr;
      }
    }

    uint32_t conv = conv_id_.load(std::memory_order_relaxed);
    if (conv == 0) {
      conv = GetTimestampMs();
      conv_id_.store(conv, std::memory_order_relaxed);
    }

    IKCPCB* new_kcp = ikcp_create(conv, this);
    if (!new_kcp) {
      transport_->Close();
      state_ = mir2::common::ChannelState::Disconnected;
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(kcp_mutex_);
      kcp_ = new_kcp;
      ConfigureKcp();
      ikcp_setoutput(kcp_, &KcpChannel::KcpOutput);
    }

    transport_->StartReceive([this](const asio::ip::udp::endpoint& endpoint,
                                    const uint8_t* data,
                                    size_t size) {
      HandleUdpPacket(endpoint, data, size);
    });

    StartUpdateTimer();

    state_ = mir2::common::ChannelState::Connected;
    return true;
  } catch (const std::exception&) {
    transport_->Close();
    {
      std::lock_guard<std::mutex> lock(kcp_mutex_);
      if (kcp_) {
        ikcp_release(kcp_);
        kcp_ = nullptr;
      }
    }
    state_ = mir2::common::ChannelState::Disconnected;
    return false;
  }
}

void KcpChannel::Disconnect() {
  auto expected = mir2::common::ChannelState::Connected;
  if (!state_.compare_exchange_strong(expected, mir2::common::ChannelState::Disconnecting)) {
    expected = mir2::common::ChannelState::Connecting;
    if (!state_.compare_exchange_strong(expected, mir2::common::ChannelState::Disconnecting)) {
      expected = mir2::common::ChannelState::Disconnecting;
      if (!state_.compare_exchange_strong(expected, mir2::common::ChannelState::Disconnecting)) {
        return;
      }
    }
  }

  asio::error_code ec;
  update_timer_.cancel(ec);
  if (transport_) {
    transport_->Close();
  }

  {
    std::lock_guard<std::mutex> lock(kcp_mutex_);
    if (kcp_) {
      ikcp_release(kcp_);
      kcp_ = nullptr;
    }
  }

  {
    std::lock_guard<std::mutex> lock(receive_mutex_);
    std::queue<NetworkPacket> empty;
    std::swap(receive_queue_, empty);
  }

  state_ = mir2::common::ChannelState::Disconnected;
}

bool KcpChannel::IsConnected() const {
  return state_ == mir2::common::ChannelState::Connected;
}

void KcpChannel::Send(uint16_t msg_id, const std::vector<uint8_t>& payload) {
  {
    std::lock_guard<std::mutex> lock(kcp_mutex_);
    if (state_ != mir2::common::ChannelState::Connected || !kcp_) {
      return;
    }
  }

  const uint16_t seq = send_sequence_.fetch_add(1, std::memory_order_relaxed);
  auto encoded = mir2::common::EncodePacketV2(
      msg_id,
      payload.data(),
      payload.size(),
      seq,
      mir2::common::PacketHeaderV2::kFlagChannelKcp);
  if (encoded.empty()) {
    return;
  }

  asio::post(io_context_, [this, data = std::move(encoded)]() mutable {
    std::lock_guard<std::mutex> lock(kcp_mutex_);
    if (state_ != mir2::common::ChannelState::Connected || !kcp_) {
      return;
    }
    ikcp_send(kcp_, reinterpret_cast<const char*>(data.data()),
              static_cast<int>(data.size()));
  });
}

void KcpChannel::Update() {
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

mir2::common::ChannelType KcpChannel::Type() const {
  return mir2::common::ChannelType::kKcp;
}

void KcpChannel::ResetSession() {
  Disconnect();
  conv_id_.store(0, std::memory_order_relaxed);
  send_sequence_.store(0, std::memory_order_relaxed);
  token_set_.store(false, std::memory_order_relaxed);
  session_token_.fill(0);
  remote_endpoint_ = asio::ip::udp::endpoint();
}

void KcpChannel::SetOnMessage(MessageCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  on_message_ = std::move(callback);
}

void KcpChannel::SetConvId(uint32_t conv) {
  conv_id_.store(conv, std::memory_order_relaxed);
}

void KcpChannel::SetSessionToken(const std::array<uint8_t, kTokenSize>& token) {
  session_token_ = token;
  token_set_.store(true, std::memory_order_relaxed);
}

void KcpChannel::SetSessionToken(const std::string& token) {
  session_token_.fill(0);
  if (!token.empty()) {
    const size_t copy_len = std::min(token.size(), session_token_.size());
    std::memcpy(session_token_.data(), token.data(), copy_len);
  }
  token_set_.store(true, std::memory_order_relaxed);
}

int KcpChannel::KcpOutput(const char* buf, int len, IKCPCB* /*kcp*/, void* user) {
  if (!buf || len <= 0 || !user) {
    return -1;
  }
  auto* channel = static_cast<KcpChannel*>(user);
  if (!channel->transport_ || !channel->transport_->IsOpen()) {
    return -1;
  }

  const uint32_t conv = channel->conv_id_.load(std::memory_order_relaxed);
  const size_t header_size = sizeof(conv) + kTokenSize;
  std::vector<uint8_t> packet(header_size + static_cast<size_t>(len));
  size_t offset = 0;
  std::memcpy(packet.data() + offset, &conv, sizeof(conv));
  offset += sizeof(conv);
  std::memcpy(packet.data() + offset, channel->session_token_.data(), kTokenSize);
  offset += kTokenSize;
  std::memcpy(packet.data() + offset, buf, static_cast<size_t>(len));

  channel->transport_->SendTo(channel->remote_endpoint_, packet.data(), packet.size());
  return 0;
}

uint32_t KcpChannel::GetTimestampMs() {
  using namespace std::chrono;
  return static_cast<uint32_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void KcpChannel::StartUpdateTimer() {
  update_timer_.expires_after(std::chrono::milliseconds(config_.interval));
  update_timer_.async_wait([this](const asio::error_code& ec) {
    if (ec || state_ != mir2::common::ChannelState::Connected) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(kcp_mutex_);
      if (!kcp_) {
        return;
      }
      ikcp_update(kcp_, GetTimestampMs());
      DrainKcpReceive();
    }
    StartUpdateTimer();
  });
}

void KcpChannel::ConfigureKcp() {
  if (!kcp_) {
    return;
  }

  ikcp_nodelay(kcp_, config_.nodelay, config_.interval, config_.resend, config_.nc);
  ikcp_wndsize(kcp_, config_.snd_wnd, config_.rcv_wnd);
  ikcp_setmtu(kcp_, config_.mtu);
}

void KcpChannel::HandleUdpPacket(const asio::ip::udp::endpoint& /*endpoint*/,
                                 const uint8_t* data,
                                 size_t size) {
  if (!data || size == 0) {
    return;
  }

  const size_t header_size = sizeof(uint32_t) + kTokenSize;
  if (size <= header_size) {
    return;
  }

  uint32_t conv = 0;
  std::memcpy(&conv, data, sizeof(conv));
  if (conv_id_.load(std::memory_order_relaxed) != 0 &&
      conv != conv_id_.load(std::memory_order_relaxed)) {
    return;
  }

  if (token_set_.load(std::memory_order_relaxed)) {
    const uint8_t* token_data = data + sizeof(conv);
    if (std::memcmp(token_data, session_token_.data(), kTokenSize) != 0) {
      return;
    }
  }

  const uint8_t* kcp_payload = data + header_size;
  const size_t kcp_size = size - header_size;
  {
    std::lock_guard<std::mutex> lock(kcp_mutex_);
    if (!kcp_) {
      return;
    }
    ikcp_input(kcp_, reinterpret_cast<const char*>(kcp_payload),
               static_cast<long>(kcp_size));
    DrainKcpReceive();
  }
}

void KcpChannel::DrainKcpReceive() {
  if (!kcp_) {
    return;
  }

  while (true) {
    int peek_size = ikcp_peeksize(kcp_);
    if (peek_size <= 0) {
      break;
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(peek_size));
    int recv_size = ikcp_recv(kcp_, reinterpret_cast<char*>(buffer.data()), peek_size);
    if (recv_size <= 0) {
      break;
    }

    mir2::common::NetworkPacket packet{};
    uint16_t sequence = 0;
    uint8_t flags = 0;
    const auto status = mir2::common::DecodePacketV2(
        buffer.data(), buffer.size(), &packet, &sequence, &flags);
    if (status != mir2::common::DecodeStatus::kOk) {
      continue;
    }


    (void)sequence;
    (void)flags;

    std::lock_guard<std::mutex> lock(receive_mutex_);
    if (receive_queue_.size() >= kMaxReceiveQueueSize) {
      break;
    }
    receive_queue_.push(std::move(packet));
  }
}

}  // namespace mir2::client
