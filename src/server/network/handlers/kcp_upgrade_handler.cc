#include "network/handlers/kcp_upgrade_handler.h"

#include <random>

#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "network/dual_channel_manager.h"
#include "network/tcp_session.h"
#include "system_generated.h"

namespace mir2::network {

namespace {

constexpr uint16_t kMsgIdKcpUpgradeResponse =
    static_cast<uint16_t>(mir2::common::MsgId::kKcpUpgradeResponse);

constexpr uint16_t kErrorUnsupported = 1;
constexpr uint16_t kErrorInvalidRequest = 2;
constexpr uint16_t kErrorServerFailed = 3;

std::string TokenToString(const std::array<uint8_t, KcpSession::kTokenSize>& token) {
  return std::string(reinterpret_cast<const char*>(token.data()), token.size());
}

}  // namespace

KcpUpgradeHandler::KcpUpgradeHandler(IDualChannelManager& manager, uint16_t udp_port)
    : manager_(manager), udp_port_(udp_port) {
}

void KcpUpgradeHandler::HandleKcpUpgradeRequest(
    const std::shared_ptr<ITcpSession>& session,
    const std::vector<uint8_t>& payload) {
  if (!session) {
    return;
  }

  if (!manager_.GetKcpServer().IsRunning()) {
    SendUpgradeResponse(session, false, 0, std::string(), kErrorServerFailed);
    return;
  }

  if (!session->IsKcpUpgradeAllowed()) {
    SendUpgradeResponse(session, false, 0, std::string(), kErrorUnsupported);
    return;
  }

  if (!payload.empty()) {
    flatbuffers::Verifier verifier(payload.data(), payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::KcpUpgradeRequest>(nullptr)) {
      SendUpgradeResponse(session, false, 0, std::string(), kErrorInvalidRequest);
      return;
    }
  }

  const uint32_t conv_id = manager_.GetKcpServer().AllocateConvId();
  const auto token = GenerateToken();
  auto kcp_session = manager_.GetKcpServer().CreateSession(conv_id, token);
  if (!kcp_session) {
    SendUpgradeResponse(session, false, 0, std::string(), kErrorServerFailed);
    return;
  }

  if (!manager_.BindKcpSession(session->GetSessionId(), kcp_session)) {
    manager_.GetKcpServer().RemoveSession(conv_id);
    SendUpgradeResponse(session, false, 0, std::string(), kErrorServerFailed);
    return;
  }

  SendUpgradeResponse(session, true, conv_id, TokenToString(token), 0);
}

std::array<uint8_t, KcpSession::kTokenSize> KcpUpgradeHandler::GenerateToken() const {
  std::array<uint8_t, KcpSession::kTokenSize> token{};
  std::random_device rd;
  std::uniform_int_distribution<int> dist(0, 255);
  for (auto& byte : token) {
    byte = static_cast<uint8_t>(dist(rd));
  }
  return token;
}

void KcpUpgradeHandler::SendUpgradeResponse(
    const std::shared_ptr<ITcpSession>& session,
    bool success,
    uint32_t conv_id,
    const std::string& token,
    uint16_t error_code) {
  if (!session) {
    return;
  }

  flatbuffers::FlatBufferBuilder builder;
  auto token_offset = builder.CreateString(token);
  const auto response = mir2::proto::CreateKcpUpgradeResponse(
      builder,
      success,
      conv_id,
      udp_port_,
      token_offset,
      error_code);
  builder.Finish(response);

  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> rsp_payload(data, data + builder.GetSize());
  session->Send(kMsgIdKcpUpgradeResponse, rsp_payload);
}

}  // namespace mir2::network
