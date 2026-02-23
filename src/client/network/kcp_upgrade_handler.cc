#include "client/network/kcp_upgrade_handler.h"

#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "common/time_utils.h"
#include "system_generated.h"

namespace mir2::client {

namespace {

constexpr uint16_t kMsgIdKcpUpgradeRequest =
    static_cast<uint16_t>(mir2::common::MsgId::kKcpUpgradeRequest);
constexpr uint16_t kMsgIdKcpUpgradeResponse =
    static_cast<uint16_t>(mir2::common::MsgId::kKcpUpgradeResponse);
constexpr uint16_t kMsgIdKcpHeartbeat =
    static_cast<uint16_t>(mir2::common::MsgId::kKcpHeartbeat);
constexpr uint16_t kMsgIdKcpHeartbeatAck =
    static_cast<uint16_t>(mir2::common::MsgId::kKcpHeartbeatAck);

constexpr int64_t kHeartbeatIntervalMs = 5000;
constexpr int64_t kHeartbeatTimeoutMs = 15000;

}  // namespace

KcpUpgradeHandler::KcpUpgradeHandler(mir2::common::KcpConfig config)
    : config_(config) {
}

void KcpUpgradeHandler::SetTcpSender(SendCallback sender) {
  tcp_sender_ = std::move(sender);
}

void KcpUpgradeHandler::SetKcpSender(SendCallback sender) {
  kcp_sender_ = std::move(sender);
}

void KcpUpgradeHandler::SetSessionCallback(SessionCallback callback) {
  session_callback_ = std::move(callback);
}

void KcpUpgradeHandler::SetConfirmedCallback(EventCallback callback) {
  confirmed_callback_ = std::move(callback);
}

void KcpUpgradeHandler::SetFailedCallback(EventCallback callback) {
  failed_callback_ = std::move(callback);
}

void KcpUpgradeHandler::SetDisconnectCallback(EventCallback callback) {
  disconnect_callback_ = std::move(callback);
}

void KcpUpgradeHandler::Reset() {
  request_sent_ = false;
  request_in_flight_ = false;
  request_start_ms_ = 0;
  ResetHeartbeat();
}

void KcpUpgradeHandler::ResetHeartbeat() {
  last_heartbeat_send_ms_ = 0;
  last_heartbeat_ack_ms_ = 0;
  heartbeat_confirmed_ = false;
  heartbeat_timed_out_ = false;
}

void KcpUpgradeHandler::RequestUpgrade() {
  request_sent_ = false;
  request_in_flight_ = false;
  request_start_ms_ = 0;
}

void KcpUpgradeHandler::OnTcpConnected() {
  RequestUpgrade();
  SendUpgradeRequest();
}

bool KcpUpgradeHandler::HandleTcpPacket(const NetworkPacket& packet) {
  if (packet.msg_id != kMsgIdKcpUpgradeResponse) {
    return false;
  }

  request_in_flight_ = false;

  flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
  if (!verifier.VerifyBuffer<mir2::proto::KcpUpgradeResponse>(nullptr)) {
    if (failed_callback_) {
      failed_callback_();
    }
    return true;
  }

  const auto* response = flatbuffers::GetRoot<mir2::proto::KcpUpgradeResponse>(
      packet.payload.data());
  if (!response || !response->success()) {
    if (failed_callback_) {
      failed_callback_();
    }
    return true;
  }

  const uint32_t conv_id = response->conv_id();
  const uint16_t udp_port = response->server_udp_port();
  if (conv_id == 0 || udp_port == 0) {
    if (failed_callback_) {
      failed_callback_();
    }
    return true;
  }

  std::string token;
  if (response->session_token()) {
    token = response->session_token()->str();
  }

  if (session_callback_) {
    session_callback_(conv_id, token, udp_port);
  }

  return true;
}

bool KcpUpgradeHandler::HandleKcpPacket(const NetworkPacket& packet) {
  if (packet.msg_id != kMsgIdKcpHeartbeatAck) {
    return false;
  }

  if (!packet.payload.empty()) {
    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::KcpHeartbeatAck>(nullptr)) {
      return true;
    }
  }

  MarkHeartbeatAck(mir2::common::now_ms());
  return true;
}

void KcpUpgradeHandler::Update(int64_t now_ms,
                               bool tcp_connected,
                               bool kcp_connected,
                               bool kcp_confirmed) {
  if (tcp_connected && !request_sent_ && !request_in_flight_) {
    SendUpgradeRequest();
  }

  if (request_in_flight_ &&
      now_ms - request_start_ms_ >= static_cast<int64_t>(config_.timeout_ms)) {
    request_in_flight_ = false;
    if (failed_callback_) {
      failed_callback_();
    }
  }

  if (kcp_connected) {
    if (last_heartbeat_send_ms_ == 0 ||
        now_ms - last_heartbeat_send_ms_ >= kHeartbeatIntervalMs) {
      SendHeartbeat(now_ms);
    }
  }

  if (kcp_confirmed && !heartbeat_timed_out_ && last_heartbeat_send_ms_ > 0 &&
      now_ms - last_heartbeat_send_ms_ >= kHeartbeatTimeoutMs) {
    heartbeat_timed_out_ = true;
    if (disconnect_callback_) {
      disconnect_callback_();
    }
  }
}

void KcpUpgradeHandler::SendUpgradeRequest() {
  if (!tcp_sender_) {
    return;
  }

  flatbuffers::FlatBufferBuilder builder;
  const uint16_t client_udp_port = 0;
  const auto request = mir2::proto::CreateKcpUpgradeRequest(builder, client_udp_port);
  builder.Finish(request);

  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());

  tcp_sender_(kMsgIdKcpUpgradeRequest, payload);
  request_sent_ = true;
  request_in_flight_ = true;
  request_start_ms_ = mir2::common::now_ms();
}

void KcpUpgradeHandler::SendHeartbeat(int64_t now_ms) {
  if (!kcp_sender_) {
    return;
  }

  flatbuffers::FlatBufferBuilder builder;
  const auto heartbeat = mir2::proto::CreateKcpHeartbeat(
      builder, static_cast<uint32_t>(now_ms));
  builder.Finish(heartbeat);

  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());

  kcp_sender_(kMsgIdKcpHeartbeat, payload);
  last_heartbeat_send_ms_ = now_ms;
}

void KcpUpgradeHandler::MarkHeartbeatAck(int64_t now_ms) {
  last_heartbeat_ack_ms_ = now_ms;
  heartbeat_timed_out_ = false;
  if (!heartbeat_confirmed_) {
    heartbeat_confirmed_ = true;
    if (confirmed_callback_) {
      confirmed_callback_();
    }
  }
}

}  // namespace mir2::client
