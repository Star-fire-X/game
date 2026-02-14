/**
 * @file combat_handlers_auth_test.cc
 * @brief Auth binding tests for attack/skill logic handlers.
 */

#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include <flatbuffers/flatbuffers.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "combat_generated.h"
#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "logic/coroutine_executor.h"
#include "logic/handlers/attack_handler.h"
#include "logic/handlers/skill_handler.h"
#include "logic/mock_response_sender.h"
#include "logic/services/combat_service.h"
#include "logic/services/session_role_store.h"

namespace mir2::logic::test {
namespace {

class StubCombatService final : public CombatService {
 public:
  CombatResult Attack(uint64_t attacker_id,
                      uint64_t target_id,
                      mir2::proto::EntityType target_type) override {
    ++attack_calls;
    last_attacker_id = attacker_id;
    last_attack_target_id = target_id;
    last_attack_target_type = target_type;
    CombatResult result;
    result.code = mir2::common::ErrorCode::kOk;
    result.damage = 7;
    result.target_hp = 93;
    result.target_dead = false;
    return result;
  }

  CombatResult UseSkill(uint64_t caster_id, uint64_t target_id, uint32_t skill_id) override {
    ++skill_calls;
    last_caster_id = caster_id;
    last_skill_target_id = target_id;
    last_skill_id = skill_id;
    CombatResult result;
    result.code = mir2::common::ErrorCode::kOk;
    result.damage = 11;
    result.healing = 0;
    result.target_hp = 89;
    result.target_dead = false;
    return result;
  }

  int attack_calls = 0;
  int skill_calls = 0;
  uint64_t last_attacker_id = 0;
  uint64_t last_attack_target_id = 0;
  mir2::proto::EntityType last_attack_target_type = mir2::proto::EntityType::NONE;
  uint64_t last_caster_id = 0;
  uint64_t last_skill_target_id = 0;
  uint32_t last_skill_id = 0;
};

std::vector<uint8_t> BuildAttackReqPayload(uint64_t target_id,
                                           mir2::proto::EntityType target_type) {
  mir2::common::AttackRequest request;
  request.target_id = target_id;
  request.target_type = target_type;
  mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
  auto payload = mir2::common::EncodeAttackRequest(request, &status);
  if (status != mir2::common::MessageCodecStatus::kOk) {
    return {};
  }
  return payload;
}

std::vector<uint8_t> BuildSkillReqPayload(uint64_t target_id, uint32_t skill_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateSkillReq(builder, skill_id, target_id);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

class CombatHandlersAuthTest : public ::testing::Test {
 protected:
  void SetUp() override {
    executor_ = std::make_unique<CoroutineExecutor>(io_context_, 1);
    response_sender_ = std::make_unique<MockResponseSender>();
    attack_handler_ =
        std::make_unique<AttackHandler>(*response_sender_, combat_service_, role_store_);
    skill_handler_ =
        std::make_unique<SkillHandler>(*response_sender_, combat_service_, role_store_);
  }

  void RunIo() {
    io_context_.run();
    io_context_.restart();
  }

  asio::io_context io_context_;
  std::unique_ptr<CoroutineExecutor> executor_;
  std::unique_ptr<MockResponseSender> response_sender_;
  StubCombatService combat_service_;
  RoleStore role_store_;
  std::unique_ptr<AttackHandler> attack_handler_;
  std::unique_ptr<SkillHandler> skill_handler_;
};

TEST_F(CombatHandlersAuthTest, AttackRejectsUnboundClientRole) {
  HandlerContext context;
  context.client_id = 9001;
  const auto payload = BuildAttackReqPayload(3001, mir2::proto::EntityType::MONSTER);
  ASSERT_FALSE(payload.empty());

  executor_->Spawn(attack_handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIo();

  EXPECT_EQ(combat_service_.attack_calls, 0);
  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].msg_id, static_cast<uint16_t>(mir2::common::MsgId::kAttackRsp));

  mir2::common::AttackResponse response;
  const auto status = mir2::common::DecodeAttackResponse(
      mir2::common::kAttackResponseMsgId, responses[0].payload, &response);
  ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
  EXPECT_EQ(response.code, mir2::proto::ErrorCode::ERR_INVALID_ACTION);
  EXPECT_EQ(response.attacker_id, 0u);
}

TEST_F(CombatHandlersAuthTest, AttackUsesBoundRoleId) {
  HandlerContext context;
  context.client_id = 9002;
  role_store_.BindClientRole(context.client_id, 7002);
  const auto payload = BuildAttackReqPayload(3002, mir2::proto::EntityType::PLAYER);
  ASSERT_FALSE(payload.empty());

  executor_->Spawn(attack_handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIo();

  EXPECT_EQ(combat_service_.attack_calls, 1);
  EXPECT_EQ(combat_service_.last_attacker_id, 7002u);
  EXPECT_EQ(combat_service_.last_attack_target_id, 3002u);
  EXPECT_EQ(combat_service_.last_attack_target_type, mir2::proto::EntityType::PLAYER);
}

TEST_F(CombatHandlersAuthTest, SkillRejectsUnboundClientRole) {
  HandlerContext context;
  context.client_id = 9101;
  const auto payload = BuildSkillReqPayload(4001, 101);
  ASSERT_FALSE(payload.empty());

  executor_->Spawn(skill_handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIo();

  EXPECT_EQ(combat_service_.skill_calls, 0);
  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].msg_id, static_cast<uint16_t>(mir2::common::MsgId::kSkillRsp));

  flatbuffers::Verifier verifier(responses[0].payload.data(), responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::SkillRsp>(nullptr));
  const auto* rsp = flatbuffers::GetRoot<mir2::proto::SkillRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_EQ(rsp->code(), mir2::proto::ErrorCode::ERR_INVALID_ACTION);
  EXPECT_EQ(rsp->caster_id(), 0u);
  EXPECT_EQ(rsp->skill_id(), 101u);
}

TEST_F(CombatHandlersAuthTest, SkillRejectsMissingTargetAndKeepsSkillId) {
  HandlerContext context;
  context.client_id = 9103;
  const auto payload = BuildSkillReqPayload(0, 103);
  ASSERT_FALSE(payload.empty());

  executor_->Spawn(skill_handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIo();

  EXPECT_EQ(combat_service_.skill_calls, 0);
  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].msg_id, static_cast<uint16_t>(mir2::common::MsgId::kSkillRsp));

  flatbuffers::Verifier verifier(responses[0].payload.data(), responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::SkillRsp>(nullptr));
  const auto* rsp = flatbuffers::GetRoot<mir2::proto::SkillRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_EQ(rsp->code(), mir2::proto::ErrorCode::ERR_TARGET_NOT_FOUND);
  EXPECT_EQ(rsp->skill_id(), 103u);
}

TEST_F(CombatHandlersAuthTest, SkillUsesBoundRoleId) {
  HandlerContext context;
  context.client_id = 9102;
  role_store_.BindClientRole(context.client_id, 7102);
  const auto payload = BuildSkillReqPayload(4002, 102);
  ASSERT_FALSE(payload.empty());

  executor_->Spawn(skill_handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIo();

  EXPECT_EQ(combat_service_.skill_calls, 1);
  EXPECT_EQ(combat_service_.last_caster_id, 7102u);
  EXPECT_EQ(combat_service_.last_skill_target_id, 4002u);
  EXPECT_EQ(combat_service_.last_skill_id, 102u);
}

}  // namespace
}  // namespace mir2::logic::test
