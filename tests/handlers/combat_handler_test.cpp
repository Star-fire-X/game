#include <gtest/gtest.h>

#include <flatbuffers/flatbuffers.h>

#include "server/common/error_codes.h"
#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "combat_generated.h"
#include "handlers/combat/combat_handler.h"

namespace {

class StubCombatService : public legend2::handlers::CombatService {
public:
    legend2::handlers::CombatResult attack_result;
    legend2::handlers::CombatResult skill_result;

    legend2::handlers::CombatResult Attack(uint64_t /*attacker_id*/,
                                           uint64_t /*target_id*/) override {
        return attack_result;
    }

    legend2::handlers::CombatResult UseSkill(uint64_t /*caster_id*/,
                                             uint64_t /*target_id*/,
                                             uint32_t /*skill_id*/) override {
        return skill_result;
    }
};

std::vector<uint8_t> BuildAttackReq(uint64_t target_id) {
    mir2::common::AttackRequest request;
    request.target_id = target_id;
    request.target_type = mir2::proto::EntityType::PLAYER;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeAttackRequest(request, &status);
    if (status != mir2::common::MessageCodecStatus::kOk) {
        return {};
    }
    return payload;
}

std::vector<uint8_t> BuildRawAttackReq(uint64_t target_id, mir2::proto::EntityType target_type) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateAttackReq(
        builder, target_id, target_type);
    builder.Finish(req);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildSkillReq(uint32_t skill_id, uint64_t target_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateSkillReq(builder, skill_id, target_id);
    builder.Finish(req);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

}  // namespace

TEST(CombatHandlerTest, AttackReturnsResult) {
    StubCombatService service;
    service.attack_result.code = mir2::common::ErrorCode::kOk;
    service.attack_result.damage = 5;
    service.attack_result.target_hp = 95;
    service.attack_result.target_dead = false;

    legend2::handlers::CombatHandler handler(service);
    legend2::handlers::HandlerContext context;
    context.client_id = 7;

    const auto payload = BuildAttackReq(9);
    legend2::handlers::ResponseList responses;
    handler.Handle(context,
                   static_cast<uint16_t>(mir2::common::MsgId::kAttackReq),
                   payload,
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });

    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].msg_id,
              static_cast<uint16_t>(mir2::common::MsgId::kAttackRsp));

    mir2::common::AttackResponse response;
    const auto status = mir2::common::DecodeAttackResponse(
        mir2::common::kAttackResponseMsgId, responses[0].payload, &response);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(static_cast<uint16_t>(response.code),
              static_cast<uint16_t>(mir2::common::ErrorCode::kOk));
    EXPECT_EQ(response.damage, 5);
}

TEST(CombatHandlerTest, SkillErrorReturnsErrorCode) {
    StubCombatService service;
    service.skill_result.code = mir2::common::ErrorCode::kTargetNotFound;

    legend2::handlers::CombatHandler handler(service);
    legend2::handlers::HandlerContext context;
    context.client_id = 7;

    const auto payload = BuildSkillReq(1, 9);
    legend2::handlers::ResponseList responses;
    handler.Handle(context,
                   static_cast<uint16_t>(mir2::common::MsgId::kSkillReq),
                   payload,
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });

    ASSERT_EQ(responses.size(), 1u);
    flatbuffers::Verifier verifier(responses[0].payload.data(), responses[0].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::SkillRsp>(nullptr));
    const auto* rsp = flatbuffers::GetRoot<mir2::proto::SkillRsp>(responses[0].payload.data());
    ASSERT_NE(rsp, nullptr);
    EXPECT_EQ(static_cast<uint16_t>(rsp->code()),
              static_cast<uint16_t>(mir2::common::ErrorCode::kTargetNotFound));
}

TEST(CombatHandlerTest, InvalidPayloadReturnsError) {
    StubCombatService service;
    legend2::handlers::CombatHandler handler(service);
    legend2::handlers::HandlerContext context;
    context.client_id = 7;

    legend2::handlers::ResponseList responses;
    handler.Handle(context,
                   static_cast<uint16_t>(mir2::common::MsgId::kAttackReq),
                   {},
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });

    ASSERT_EQ(responses.size(), 1u);
    mir2::common::AttackResponse response;
    const auto status = mir2::common::DecodeAttackResponse(
        mir2::common::kAttackResponseMsgId, responses[0].payload, &response);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(static_cast<uint16_t>(response.code),
              static_cast<uint16_t>(mir2::common::ErrorCode::kInvalidAction));
}

TEST(CombatHandlerTest, AttackMissingTargetTypeReturnsError) {
    StubCombatService service;
    legend2::handlers::CombatHandler handler(service);
    legend2::handlers::HandlerContext context;
    context.client_id = 7;

    const auto payload = BuildRawAttackReq(9, mir2::proto::EntityType::NONE);
    legend2::handlers::ResponseList responses;
    handler.Handle(context,
                   static_cast<uint16_t>(mir2::common::MsgId::kAttackReq),
                   payload,
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });

    ASSERT_EQ(responses.size(), 1u);
    mir2::common::AttackResponse response;
    const auto status = mir2::common::DecodeAttackResponse(
        mir2::common::kAttackResponseMsgId, responses[0].payload, &response);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(static_cast<uint16_t>(response.code),
              static_cast<uint16_t>(mir2::common::ErrorCode::kInvalidAction));
}
