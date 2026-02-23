/**
 * @file attack_handler_test.cc
 * @brief Comprehensive tests for AttackHandler - core attack request processing
 *
 * Test Coverage:
 * - Valid attack request handling (message path + hot-event path)
 * - Invalid target validation (missing, invalid type, zero ID)
 * - Player authentication and role binding
 * - Payload encoding/decoding error handling
 * - Combat service integration and error propagation
 * - Response generation and delivery
 * - Edge cases: empty payload, malformed payload, max IDs
 *
 * Priority: P0 Ultra-Critical (Score: 48)
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
#include "logic/mock_response_sender.h"
#include "logic/services/combat_service.h"
#include "logic/services/session_role_store.h"

namespace mir2::logic::test {
namespace {

// ---------------------------------------------------------------------------
// Stub CombatService
// ---------------------------------------------------------------------------

class StubCombatService final : public CombatService {
 public:
  CombatResult Attack(uint64_t attacker_id,
                      uint64_t target_id,
                      mir2::proto::EntityType target_type) override {
    ++attack_calls;
    last_attacker_id = attacker_id;
    last_target_id = target_id;
    last_target_type = target_type;
    return next_result;
  }

  CombatResult UseSkill(uint64_t, uint64_t, uint32_t) override {
    return {};
  }

  void SetResult(mir2::common::ErrorCode code, int damage, int target_hp, bool dead) {
    next_result.code = code;
    next_result.damage = damage;
    next_result.target_hp = target_hp;
    next_result.target_dead = dead;
  }

  int attack_calls = 0;
  uint64_t last_attacker_id = 0;
  uint64_t last_target_id = 0;
  mir2::proto::EntityType last_target_type = mir2::proto::EntityType::NONE;
  CombatResult next_result = [] {
    CombatResult result;
    result.code = mir2::common::ErrorCode::kOk;
    result.damage = 50;
    result.target_hp = 950;
    result.target_dead = false;
    return result;
  }();
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::vector<uint8_t> BuildAttackPayload(uint64_t target_id,
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

std::vector<uint8_t> BuildRawAttackPayload(uint64_t target_id,
                                           mir2::proto::EntityType target_type) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateAttackReq(builder, target_id, target_type);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

/**
 * @brief Decode attack response from captured payload
 */
bool DecodeResponse(const std::vector<uint8_t>& payload,
                    mir2::common::AttackResponse* out) {
  auto status = mir2::common::DecodeAttackResponse(
      mir2::common::kAttackResponseMsgId, payload, out);
  return status == mir2::common::MessageCodecStatus::kOk;
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class AttackHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    executor_ = std::make_unique<CoroutineExecutor>(io_context_, 1);
    response_sender_ = std::make_unique<MockResponseSender>();
    handler_ = std::make_unique<AttackHandler>(
        *response_sender_, combat_service_, role_store_);

    // Default: bind test client to a role
    test_client_id_ = 12345;
    test_role_id_ = 54321;
    role_store_.BindClientRole(test_client_id_, test_role_id_);
  }

  void RunIo() {
    io_context_.run();
    io_context_.restart();
  }

  HandlerContext MakeCtx(uint64_t client_id) {
    HandlerContext ctx;
    ctx.client_id = client_id;
    return ctx;
  }

  HandlerContext MakeCtx() { return MakeCtx(test_client_id_); }

  asio::io_context io_context_;
  std::unique_ptr<CoroutineExecutor> executor_;
  std::unique_ptr<MockResponseSender> response_sender_;
  StubCombatService combat_service_;
  RoleStore role_store_;
  std::unique_ptr<AttackHandler> handler_;

  uint64_t test_client_id_ = 0;
  uint64_t test_role_id_ = 0;
};

// ============================================================================
// Valid Attack Request Tests
// ============================================================================

TEST_F(AttackHandlerTest, ValidAttackMonster) {
  const uint64_t target_id = 99999;
  auto payload = BuildAttackPayload(target_id, mir2::proto::EntityType::MONSTER);
  ASSERT_FALSE(payload.empty());

  executor_->Spawn(handler_->HandleMessage(MakeCtx(), payload.data(), payload.size()));
  RunIo();

  EXPECT_EQ(combat_service_.attack_calls, 1);
  EXPECT_EQ(combat_service_.last_attacker_id, test_role_id_);
  EXPECT_EQ(combat_service_.last_target_id, target_id);
  EXPECT_EQ(combat_service_.last_target_type, mir2::proto::EntityType::MONSTER);

  auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].msg_id, static_cast<uint16_t>(mir2::common::MsgId::kAttackRsp));

  mir2::common::AttackResponse rsp;
  ASSERT_TRUE(DecodeResponse(responses[0].payload, &rsp));
  EXPECT_EQ(rsp.code, mir2::proto::ErrorCode::ERR_OK);
  EXPECT_EQ(rsp.attacker_id, test_role_id_);
  EXPECT_EQ(rsp.target_id, target_id);
  EXPECT_EQ(rsp.damage, 50);
  EXPECT_EQ(rsp.target_hp, 950);
  EXPECT_FALSE(rsp.target_dead);
}

TEST_F(AttackHandlerTest, ValidAttackPlayer) {
  const uint64_t target_id = 88888;
  auto payload = BuildAttackPayload(target_id, mir2::proto::EntityType::PLAYER);
  ASSERT_FALSE(payload.empty());

  executor_->Spawn(handler_->HandleMessage(MakeCtx(), payload.data(), payload.size()));
  RunIo();

  EXPECT_EQ(combat_service_.attack_calls, 1);
  EXPECT_EQ(combat_service_.last_target_type, mir2::proto::EntityType::PLAYER);
}

TEST_F(AttackHandlerTest, AttackKillsTarget) {
  combat_service_.SetResult(mir2::common::ErrorCode::kOk, 1000, 0, true);

  const uint64_t target_id = 77777;
  auto payload = BuildAttackPayload(target_id, mir2::proto::EntityType::MONSTER);
  ASSERT_FALSE(payload.empty());

  executor_->Spawn(handler_->HandleMessage(MakeCtx(), payload.data(), payload.size()));
  RunIo();

  auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);

  mir2::common::AttackResponse rsp;
  ASSERT_TRUE(DecodeResponse(responses[0].payload, &rsp));
  EXPECT_EQ(rsp.code, mir2::proto::ErrorCode::ERR_OK);
  EXPECT_TRUE(rsp.target_dead);
  EXPECT_EQ(rsp.target_hp, 0);
  EXPECT_EQ(rsp.damage, 1000);
}

// ============================================================================
// HandleHot Path Tests
// ============================================================================

TEST_F(AttackHandlerTest, HandleHotValidAttack) {
  const uint64_t target_id = 66666;
  const uint16_t target_type = static_cast<uint16_t>(mir2::proto::EntityType::MONSTER);

  executor_->Spawn(handler_->HandleHot(MakeCtx(), target_id, target_type));
  RunIo();

  EXPECT_EQ(combat_service_.attack_calls, 1);
  EXPECT_EQ(combat_service_.last_attacker_id, test_role_id_);
  EXPECT_EQ(combat_service_.last_target_id, target_id);
}

TEST_F(AttackHandlerTest, HandleHotRejectsNoneType) {
  const uint16_t none_type = static_cast<uint16_t>(mir2::proto::EntityType::NONE);

  executor_->Spawn(handler_->HandleHot(MakeCtx(), 55555, none_type));
  RunIo();

  EXPECT_EQ(combat_service_.attack_calls, 0);
  auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);

  mir2::common::AttackResponse rsp;
  ASSERT_TRUE(DecodeResponse(responses[0].payload, &rsp));
  EXPECT_EQ(rsp.code, mir2::proto::ErrorCode::ERR_INVALID_ACTION);
}

TEST_F(AttackHandlerTest, HandleHotRejectsZeroTargetId) {
  const uint16_t monster_type = static_cast<uint16_t>(mir2::proto::EntityType::MONSTER);

  executor_->Spawn(handler_->HandleHot(MakeCtx(), 0, monster_type));
  RunIo();

  EXPECT_EQ(combat_service_.attack_calls, 0);
  auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);

  mir2::common::AttackResponse rsp;
  ASSERT_TRUE(DecodeResponse(responses[0].payload, &rsp));
  EXPECT_EQ(rsp.code, mir2::proto::ErrorCode::ERR_TARGET_NOT_FOUND);
}

// ============================================================================
// Invalid Target Tests
// ============================================================================

TEST_F(AttackHandlerTest, RejectNoneEntityType) {
  auto payload = BuildRawAttackPayload(55555, mir2::proto::EntityType::NONE);
  ASSERT_FALSE(payload.empty());

  executor_->Spawn(handler_->HandleMessage(MakeCtx(), payload.data(), payload.size()));
  RunIo();

  EXPECT_EQ(combat_service_.attack_calls, 0);
  auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);

  mir2::common::AttackResponse rsp;
  ASSERT_TRUE(DecodeResponse(responses[0].payload, &rsp));
  EXPECT_EQ(rsp.code, mir2::proto::ErrorCode::ERR_DECODE_MISSING_FIELD);
}

// ============================================================================
// Authentication and Role Binding Tests
// ============================================================================

TEST_F(AttackHandlerTest, UnboundClientRejected) {
  const uint64_t unbound_client = 99999;
  auto payload = BuildAttackPayload(33333, mir2::proto::EntityType::MONSTER);
  ASSERT_FALSE(payload.empty());

  executor_->Spawn(handler_->HandleMessage(MakeCtx(unbound_client), payload.data(), payload.size()));
  RunIo();

  EXPECT_EQ(combat_service_.attack_calls, 0);
  auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);

  mir2::common::AttackResponse rsp;
  ASSERT_TRUE(DecodeResponse(responses[0].payload, &rsp));
  EXPECT_EQ(rsp.code, mir2::proto::ErrorCode::ERR_INVALID_ACTION);
  EXPECT_EQ(rsp.attacker_id, 0u);  // unbound → attacker_id = 0
}

TEST_F(AttackHandlerTest, CorrectRoleIdPassedToCombatService) {
  const uint64_t client_a = 1001;
  const uint64_t role_a = 2001;
  const uint64_t client_b = 1002;
  const uint64_t role_b = 2002;

  role_store_.BindClientRole(client_a, role_a);
  role_store_.BindClientRole(client_b, role_b);

  auto payload = BuildAttackPayload(5555, mir2::proto::EntityType::MONSTER);
  ASSERT_FALSE(payload.empty());

  // Client A attacks
  executor_->Spawn(handler_->HandleMessage(MakeCtx(client_a), payload.data(), payload.size()));
  RunIo();
  EXPECT_EQ(combat_service_.last_attacker_id, role_a);

  response_sender_->Clear();
  combat_service_.attack_calls = 0;

  // Client B attacks
  executor_->Spawn(handler_->HandleMessage(MakeCtx(client_b), payload.data(), payload.size()));
  RunIo();
  EXPECT_EQ(combat_service_.last_attacker_id, role_b);
}

TEST_F(AttackHandlerTest, HandleHotUnboundClientRejected) {
  const uint64_t unbound_client = 77777;
  const uint16_t monster_type = static_cast<uint16_t>(mir2::proto::EntityType::MONSTER);

  executor_->Spawn(handler_->HandleHot(MakeCtx(unbound_client), 33333, monster_type));
  RunIo();

  EXPECT_EQ(combat_service_.attack_calls, 0);
  EXPECT_EQ(response_sender_->ResponseCount(), 1u);
}

// ============================================================================
// Payload Error Tests
// ============================================================================

TEST_F(AttackHandlerTest, EmptyPayloadRejected) {
  executor_->Spawn(handler_->HandleMessage(MakeCtx(), nullptr, 0));
  RunIo();

  EXPECT_EQ(combat_service_.attack_calls, 0);
  auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);

  mir2::common::AttackResponse rsp;
  ASSERT_TRUE(DecodeResponse(responses[0].payload, &rsp));
  EXPECT_EQ(rsp.code, mir2::proto::ErrorCode::ERR_INVALID_ACTION);
}

TEST_F(AttackHandlerTest, NullPayloadWithNonZeroSizeRejected) {
  executor_->Spawn(handler_->HandleMessage(MakeCtx(), nullptr, 100));
  RunIo();

  EXPECT_EQ(combat_service_.attack_calls, 0);
  EXPECT_EQ(response_sender_->ResponseCount(), 1u);
}

TEST_F(AttackHandlerTest, MalformedPayloadRejected) {
  std::vector<uint8_t> garbage = {0xFF, 0xFE, 0xFD, 0xFC};

  executor_->Spawn(handler_->HandleMessage(MakeCtx(), garbage.data(), garbage.size()));
  RunIo();

  EXPECT_EQ(combat_service_.attack_calls, 0);
  EXPECT_EQ(response_sender_->ResponseCount(), 1u);
}

// ============================================================================
// Combat Service Error Propagation
// ============================================================================

TEST_F(AttackHandlerTest, TargetNotFoundPropagated) {
  combat_service_.SetResult(mir2::common::ErrorCode::kTargetNotFound, 0, 0, false);

  auto payload = BuildAttackPayload(11111, mir2::proto::EntityType::MONSTER);
  ASSERT_FALSE(payload.empty());

  executor_->Spawn(handler_->HandleMessage(MakeCtx(), payload.data(), payload.size()));
  RunIo();

  EXPECT_EQ(combat_service_.attack_calls, 1);  // service still called

  auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);

  mir2::common::AttackResponse rsp;
  ASSERT_TRUE(DecodeResponse(responses[0].payload, &rsp));
  EXPECT_EQ(rsp.code, mir2::proto::ErrorCode::ERR_TARGET_NOT_FOUND);
}

TEST_F(AttackHandlerTest, TargetOutOfRangePropagated) {
  combat_service_.SetResult(mir2::common::ErrorCode::kTargetOutOfRange, 0, 0, false);

  auto payload = BuildAttackPayload(22222, mir2::proto::EntityType::MONSTER);
  ASSERT_FALSE(payload.empty());

  executor_->Spawn(handler_->HandleMessage(MakeCtx(), payload.data(), payload.size()));
  RunIo();

  auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);

  mir2::common::AttackResponse rsp;
  ASSERT_TRUE(DecodeResponse(responses[0].payload, &rsp));
  EXPECT_EQ(rsp.code, mir2::proto::ErrorCode::ERR_TARGET_OUT_OF_RANGE);
}

TEST_F(AttackHandlerTest, TargetDeadPropagated) {
  combat_service_.SetResult(mir2::common::ErrorCode::kTargetDead, 0, 0, false);

  auto payload = BuildAttackPayload(33333, mir2::proto::EntityType::MONSTER);
  ASSERT_FALSE(payload.empty());

  executor_->Spawn(handler_->HandleMessage(MakeCtx(), payload.data(), payload.size()));
  RunIo();

  auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);

  mir2::common::AttackResponse rsp;
  ASSERT_TRUE(DecodeResponse(responses[0].payload, &rsp));
  EXPECT_EQ(rsp.code, mir2::proto::ErrorCode::ERR_TARGET_DEAD);
}

// ============================================================================
// Response Content Verification
// ============================================================================

TEST_F(AttackHandlerTest, ResponseContainsCorrectIds) {
  const uint64_t target_id = 87654;
  auto payload = BuildAttackPayload(target_id, mir2::proto::EntityType::MONSTER);
  ASSERT_FALSE(payload.empty());

  executor_->Spawn(handler_->HandleMessage(MakeCtx(), payload.data(), payload.size()));
  RunIo();

  auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].client_id, test_client_id_);

  mir2::common::AttackResponse rsp;
  ASSERT_TRUE(DecodeResponse(responses[0].payload, &rsp));
  EXPECT_EQ(rsp.attacker_id, test_role_id_);
  EXPECT_EQ(rsp.target_id, target_id);
}

TEST_F(AttackHandlerTest, ResponseSentToCorrectClient) {
  auto payload = BuildAttackPayload(11111, mir2::proto::EntityType::MONSTER);
  ASSERT_FALSE(payload.empty());

  executor_->Spawn(handler_->HandleMessage(MakeCtx(), payload.data(), payload.size()));
  RunIo();

  auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].client_id, test_client_id_);
}

// ============================================================================
// Multiple Attack Tests
// ============================================================================

TEST_F(AttackHandlerTest, MultipleAttacksProcessedSequentially) {
  auto payload1 = BuildAttackPayload(11111, mir2::proto::EntityType::MONSTER);
  auto payload2 = BuildAttackPayload(22222, mir2::proto::EntityType::PLAYER);
  ASSERT_FALSE(payload1.empty());
  ASSERT_FALSE(payload2.empty());

  executor_->Spawn(handler_->HandleMessage(MakeCtx(), payload1.data(), payload1.size()));
  RunIo();

  executor_->Spawn(handler_->HandleMessage(MakeCtx(), payload2.data(), payload2.size()));
  RunIo();

  EXPECT_EQ(combat_service_.attack_calls, 2);
  EXPECT_EQ(response_sender_->ResponseCount(), 2u);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(AttackHandlerTest, MaxUint64TargetId) {
  const uint64_t max_target = UINT64_MAX;
  auto payload = BuildAttackPayload(max_target, mir2::proto::EntityType::MONSTER);
  ASSERT_FALSE(payload.empty());

  executor_->Spawn(handler_->HandleMessage(MakeCtx(), payload.data(), payload.size()));
  RunIo();

  EXPECT_EQ(combat_service_.attack_calls, 1);
  EXPECT_EQ(combat_service_.last_target_id, max_target);
}

TEST_F(AttackHandlerTest, MaxUint64ClientAndRoleIds) {
  const uint64_t max_client = UINT64_MAX - 1;
  const uint64_t max_role = UINT64_MAX;
  role_store_.BindClientRole(max_client, max_role);

  auto payload = BuildAttackPayload(12345, mir2::proto::EntityType::MONSTER);
  ASSERT_FALSE(payload.empty());

  executor_->Spawn(handler_->HandleMessage(MakeCtx(max_client), payload.data(), payload.size()));
  RunIo();

  EXPECT_EQ(combat_service_.last_attacker_id, max_role);
}

TEST_F(AttackHandlerTest, ZeroDamageResult) {
  combat_service_.SetResult(mir2::common::ErrorCode::kOk, 0, 1000, false);

  auto payload = BuildAttackPayload(44444, mir2::proto::EntityType::MONSTER);
  ASSERT_FALSE(payload.empty());

  executor_->Spawn(handler_->HandleMessage(MakeCtx(), payload.data(), payload.size()));
  RunIo();

  auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);

  mir2::common::AttackResponse rsp;
  ASSERT_TRUE(DecodeResponse(responses[0].payload, &rsp));
  EXPECT_EQ(rsp.code, mir2::proto::ErrorCode::ERR_OK);
  EXPECT_EQ(rsp.damage, 0);
}

}  // namespace
}  // namespace mir2::logic::test
