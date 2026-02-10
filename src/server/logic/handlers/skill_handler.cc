#include "logic/handlers/skill_handler.h"

#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "combat_generated.h"
#include "common/enums.h"
#include "log/logger.h"
#include "logic/handlers/handler_error_utils.h"
#include "logic/response_sender.h"
#include "logic/services/combat_service.h"
#include "logic/services/session_role_store.h"

namespace mir2::logic {

namespace {

std::vector<uint8_t> BuildSkillRsp(const CombatResult& result,
                                   uint64_t caster_id,
                                   uint64_t target_id,
                                   uint32_t skill_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateSkillRsp(
      builder,
      ToProtoError(result.code),
      caster_id,
      target_id,
      result.damage,
      result.healing,
      result.target_dead,
      skill_id,
      mir2::proto::SkillResult::HIT,
      0);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

uint64_t ResolveCasterId(const RoleStore& role_store, uint64_t client_id) {
  if (const auto role_id = role_store.GetRoleId(client_id)) {
    return *role_id;
  }
  return client_id;
}

}  // namespace

SkillHandler::SkillHandler(CoroutineExecutor& executor,
                           ResponseSender& response_sender,
                           CombatService& service,
                           RoleStore& role_store)
    : executor_(executor),
      response_sender_(response_sender),
      service_(service),
      role_store_(role_store) {}

Task<void> SkillHandler::HandleMessage(HandlerContext ctx,
                                       const uint8_t* payload,
                                       size_t payload_size) {
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("SkillHandler ignored empty payload (client_id={})", ctx.client_id);
    co_await SendSkillError(std::move(ctx), mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::SkillReq>(nullptr)) {
    SYSLOG_WARN("SkillHandler payload verify failed (client_id={})", ctx.client_id);
    co_await SendSkillError(std::move(ctx), mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::SkillReq>(payload);
  if (!req) {
    SYSLOG_WARN("SkillHandler payload missing root (client_id={})", ctx.client_id);
    co_await SendSkillError(std::move(ctx), mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  const uint64_t target_id = req->target_id();
  const uint32_t skill_id = req->skill_id();
  if (target_id == 0) {
    SYSLOG_WARN("SkillHandler missing target_id (client_id={})", ctx.client_id);
    co_await SendSkillError(std::move(ctx), mir2::common::ErrorCode::kTargetNotFound);
    co_return;
  }

  co_await Handle(std::move(ctx), target_id, skill_id);
}

Task<void> SkillHandler::HandleHot(HandlerContext ctx,
                                   uint64_t target_id,
                                   uint32_t skill_id) {
  if (target_id == 0) {
    co_await SendSkillError(std::move(ctx), mir2::common::ErrorCode::kTargetNotFound);
    co_return;
  }

  co_await Handle(std::move(ctx), target_id, skill_id);
}

Task<void> SkillHandler::Handle(HandlerContext ctx, uint64_t target_id, uint32_t skill_id) {
  const uint64_t caster_id = ResolveCasterId(role_store_, ctx.client_id);
  CombatResult result = service_.UseSkill(caster_id, target_id, skill_id);
  auto payload = BuildSkillRsp(result, caster_id, target_id, skill_id);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kSkillRsp),
      std::move(payload));

  SYSLOG_DEBUG("SkillHandler skill client_id={} caster_id={} target_id={} skill_id={} code={}",
               ctx.client_id,
               caster_id,
               target_id,
               skill_id,
               static_cast<int>(result.code));
}

Task<void> SkillHandler::SendSkillError(HandlerContext ctx, mir2::common::ErrorCode code) {
  CombatResult result;
  result.code = code;
  const uint64_t caster_id = ResolveCasterId(role_store_, ctx.client_id);
  auto payload = BuildSkillRsp(result, caster_id, 0, 0);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kSkillRsp),
      std::move(payload));
}

}  // namespace mir2::logic
