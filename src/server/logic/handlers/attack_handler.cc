#include "logic/handlers/attack_handler.h"

#include <optional>
#include <utility>
#include <vector>

#include "combat_generated.h"
#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "log/logger.h"
#include "logic/handlers/handler_error_utils.h"
#include "logic/response_sender.h"
#include "logic/services/combat_service.h"
#include "logic/services/session_role_store.h"

namespace mir2::logic {

namespace {

std::vector<uint8_t> BuildAttackRsp(const CombatResult& result,
                                    uint64_t attacker_id,
                                    uint64_t target_id) {
  mir2::common::AttackResponse response;
  response.code = ToProtoError(result.code);
  response.attacker_id = attacker_id;
  response.target_id = target_id;
  response.damage = result.damage;
  response.target_hp = result.target_hp;
  response.target_dead = result.target_dead;

  mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
  auto payload = mir2::common::EncodeAttackResponse(response, &status);
  if (status == mir2::common::MessageCodecStatus::kOk) {
    return payload;
  }

  response.code = mir2::proto::ErrorCode::ERR_UNKNOWN;
  response.attacker_id = 0;
  response.target_id = 0;
  response.damage = 0;
  response.target_hp = 0;
  response.target_dead = false;
  mir2::common::MessageCodecStatus fallback_status = mir2::common::MessageCodecStatus::kOk;
  auto fallback_payload = mir2::common::EncodeAttackResponse(response, &fallback_status);
  if (fallback_status != mir2::common::MessageCodecStatus::kOk) {
    SYSLOG_ERROR("BuildAttackRsp fallback encode failed (status={})",
                 static_cast<int>(fallback_status));
    return {};
  }
  return fallback_payload;
}

uint64_t ResolveAttackerId(const RoleStore& role_store, uint64_t client_id) {
  if (const auto role_id = role_store.GetRoleId(client_id)) {
    return *role_id;
  }
  return 0;
}

}  // namespace

AttackHandler::AttackHandler(ResponseSender& response_sender,
                             CombatService& service,
                             RoleStore& role_store)
    : response_sender_(response_sender),
      service_(service),
      role_store_(role_store) {}

Task<void> AttackHandler::HandleMessage(HandlerContext ctx,
                                        const uint8_t* payload,
                                        size_t payload_size) {
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("AttackHandler ignored empty payload (client_id={})", ctx.client_id);
    co_await SendAttackError(std::move(ctx), mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  mir2::common::AttackRequest request{};
  const auto status = mir2::common::DecodeAttackRequest(
      mir2::common::kAttackRequestMsgId, payload, payload_size, &request);
  if (status != mir2::common::MessageCodecStatus::kOk) {
    auto error = ToCommonError(status);
    if (status == mir2::common::MessageCodecStatus::kMissingField &&
        request.target_id == 0) {
      error = mir2::common::ErrorCode::kTargetNotFound;
    }
    SYSLOG_WARN("AttackHandler decode failed (client_id={}, status={})",
                ctx.client_id, static_cast<int>(status));
    co_await SendAttackError(std::move(ctx), error);
    co_return;
  }

  if (request.target_type == mir2::proto::EntityType::NONE) {
    SYSLOG_WARN("AttackHandler missing target type (client_id={})", ctx.client_id);
    co_await SendAttackError(std::move(ctx), mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  co_await Handle(
      std::move(ctx), request.target_id, static_cast<uint16_t>(request.target_type));
}

Task<void> AttackHandler::HandleHot(HandlerContext ctx,
                                    uint64_t target_id,
                                    uint16_t target_type) {
  if (target_type == static_cast<uint16_t>(mir2::proto::EntityType::NONE)) {
    co_await SendAttackError(std::move(ctx), mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  if (target_id == 0) {
    co_await SendAttackError(std::move(ctx), mir2::common::ErrorCode::kTargetNotFound);
    co_return;
  }

  co_await Handle(std::move(ctx), target_id, target_type);
}

Task<void> AttackHandler::Handle(HandlerContext ctx,
                                 uint64_t target_id,
                                 uint16_t target_type) {
  const auto role_id = role_store_.GetRoleId(ctx.client_id);
  if (!role_id.has_value()) {
    SYSLOG_WARN("AttackHandler rejected unbound client (client_id={}, target_id={})",
                ctx.client_id,
                target_id);
    co_await SendAttackError(std::move(ctx), mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }
  const uint64_t attacker_id = *role_id;
  const auto proto_target_type = static_cast<mir2::proto::EntityType>(target_type);
  CombatResult result = service_.Attack(attacker_id, target_id, proto_target_type);
  auto payload = BuildAttackRsp(result, attacker_id, target_id);
  if (payload.empty()) {
    SYSLOG_ERROR(
        "AttackHandler failed to build response payload (client_id={}, attacker_id={}, target_id={})",
        ctx.client_id,
        attacker_id,
        target_id);
    co_return;
  }
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kAttackRsp),
      std::move(payload));

  SYSLOG_DEBUG(
      "AttackHandler attack client_id={} attacker_id={} target_id={} target_type={} code={} ({})",
               ctx.client_id,
               attacker_id,
               target_id,
               target_type,
               static_cast<int>(result.code),
               mir2::common::error_code_to_string(result.code));
}

Task<void> AttackHandler::SendAttackError(HandlerContext ctx, mir2::common::ErrorCode code) {
  CombatResult result;
  result.code = code;
  const uint64_t attacker_id = ResolveAttackerId(role_store_, ctx.client_id);
  auto payload = BuildAttackRsp(result, attacker_id, 0);
  if (payload.empty()) {
    SYSLOG_ERROR("AttackHandler failed to build error payload (client_id={}, code={})",
                 ctx.client_id,
                 static_cast<int>(code));
    co_return;
  }
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kAttackRsp),
      std::move(payload));
}

}  // namespace mir2::logic
