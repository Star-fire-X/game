#include "handlers/combat/combat_handler.h"

#include <flatbuffers/flatbuffers.h>

#include "combat_generated.h"
#include "common/protocol/message_codec.h"
#include "handlers/handler_utils.h"

namespace legend2::handlers {

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
    return mir2::common::EncodeAttackResponse(response, nullptr);
}

std::vector<uint8_t> BuildSkillRsp(const CombatResult& result,
                                   uint64_t caster_id,
                                   uint64_t target_id,
                                   uint32_t skill_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreateSkillRsp(
        builder, ToProtoError(result.code), caster_id, target_id,
        result.damage, result.healing, result.target_dead,
        skill_id, mir2::proto::SkillResult::HIT, 0);
    builder.Finish(rsp);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

}  // namespace

CombatHandler::CombatHandler(CombatService& service)
    : BaseHandler(mir2::log::LogCategory::kCombat),
      service_(service) {}

void CombatHandler::DoHandle(const HandlerContext& context,
                             uint16_t msg_id,
                             const std::vector<uint8_t>& payload,
                             ResponseCallback callback) {
    if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kAttackReq)) {
        HandleAttack(context, payload, std::move(callback));
        return;
    }
    if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kSkillReq)) {
        HandleSkill(context, payload, std::move(callback));
        return;
    }

    OnError(context, msg_id, mir2::common::ErrorCode::kInvalidAction, std::move(callback));
}

void CombatHandler::OnError(const HandlerContext& context,
                            uint16_t msg_id,
                            mir2::common::ErrorCode error_code,
                            ResponseCallback callback) {
    BaseHandler::OnError(context, msg_id, error_code, callback);

    ResponseList responses;
    CombatResult result;
    result.code = error_code;

    if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kAttackReq)) {
        responses.push_back({context.client_id,
                             static_cast<uint16_t>(mir2::common::MsgId::kAttackRsp),
                             BuildAttackRsp(result, context.client_id, 0)});
    } else if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kSkillReq)) {
        responses.push_back({context.client_id,
                             static_cast<uint16_t>(mir2::common::MsgId::kSkillRsp),
                             BuildSkillRsp(result, context.client_id, 0, 0)});
    }

    if (callback) {
        callback(responses);
    }
}

void CombatHandler::HandleAttack(const HandlerContext& context,
                                 const std::vector<uint8_t>& payload,
                                 ResponseCallback callback) {
    mir2::common::AttackRequest request;
    const auto status = mir2::common::DecodeAttackRequest(
        mir2::common::kAttackRequestMsgId, payload, &request);
    if (status != mir2::common::MessageCodecStatus::kOk) {
        auto error = ToCommonError(status);
        if (status == mir2::common::MessageCodecStatus::kMissingField &&
            request.target_id == 0) {
            error = mir2::common::ErrorCode::kTargetNotFound;
        }
        OnError(context, mir2::common::kAttackRequestMsgId, error, std::move(callback));
        return;
    }

    if (request.target_type == mir2::proto::EntityType::NONE) {
        OnError(context, mir2::common::kAttackRequestMsgId,
                mir2::common::ErrorCode::kInvalidAction, std::move(callback));
        return;
    }

    CombatResult result = service_.Attack(context.client_id, request.target_id);

    ResponseList responses;
    responses.push_back({context.client_id,
                         static_cast<uint16_t>(mir2::common::MsgId::kAttackRsp),
                         BuildAttackRsp(result, context.client_id, request.target_id)});
    if (callback) {
        callback(responses);
    }
}

void CombatHandler::HandleSkill(const HandlerContext& context,
                                const std::vector<uint8_t>& payload,
                                ResponseCallback callback) {
    flatbuffers::Verifier verifier(payload.data(), payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::SkillReq>(nullptr)) {
        OnError(context, static_cast<uint16_t>(mir2::common::MsgId::kSkillReq),
                mir2::common::ErrorCode::kInvalidAction, std::move(callback));
        return;
    }

    const auto* req = flatbuffers::GetRoot<mir2::proto::SkillReq>(payload.data());
    const uint64_t target_id = req ? req->target_id() : 0;
    const uint32_t skill_id = req ? req->skill_id() : 0;
    if (target_id == 0) {
        OnError(context, static_cast<uint16_t>(mir2::common::MsgId::kSkillReq),
                mir2::common::ErrorCode::kTargetNotFound, std::move(callback));
        return;
    }

    CombatResult result = service_.UseSkill(context.client_id, target_id, skill_id);

    ResponseList responses;
    responses.push_back({context.client_id,
                         static_cast<uint16_t>(mir2::common::MsgId::kSkillRsp),
                         BuildSkillRsp(result, context.client_id, target_id, skill_id)});
    if (callback) {
        callback(responses);
    }
}

}  // namespace legend2::handlers
