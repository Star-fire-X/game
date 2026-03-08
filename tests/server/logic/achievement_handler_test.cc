#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <asio/io_context.hpp>
#include <entt/entt.hpp>
#include <flatbuffers/flatbuffers.h>

#include "achievement_generated.h"
#include "common/enums.h"
#include "ecs/components/achievement_component.h"
#include "ecs/components/character_components.h"
#include "logic/coroutine_executor.h"
#include "logic/handlers/achievement/achievement_handler.h"
#include "logic/mock_response_sender.h"

namespace mir2::logic::test {
namespace {

std::vector<uint8_t> BuildListReqPayload() {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateAchievementListReq(builder);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildClaimReqPayload(uint32_t achievement_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateAchievementClaimReq(builder, achievement_id);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

class AchievementHandlerTest : public ::testing::Test {
 protected:
  struct Player {
    uint64_t character_id = 0;
    uint64_t client_id = 0;
    entt::entity entity = entt::null;
  };

  void SetUp() override {
    executor_ = std::make_unique<CoroutineExecutor>(io_context_, 1);
    response_sender_ = std::make_unique<MockResponseSender>();
    handler_ = std::make_unique<AchievementHandler>(*response_sender_, registry_);
  }

  void RunIoContext() {
    io_context_.run();
    io_context_.restart();
  }

  void Dispatch(const HandlerContext& ctx, const std::vector<uint8_t>& payload) {
    ASSERT_TRUE(executor_->Spawn(handler_->HandleMessage(ctx, payload.data(), payload.size())));
    RunIoContext();
  }

  Player CreatePlayer(uint64_t character_id,
                      uint64_t client_id,
                      const std::string& name,
                      int gold = 0) {
    const entt::entity entity = registry_.create();
    auto& identity = registry_.emplace<ecs::CharacterIdentityComponent>(entity);
    identity.id = static_cast<uint32_t>(character_id);
    identity.account_id = static_cast<ecs::AccountId>(character_id);
    identity.name = name;

    auto& attrs = registry_.emplace<ecs::CharacterAttributesComponent>(entity);
    attrs.gold = gold;

    return Player{character_id, client_id, entity};
  }

  std::optional<CapturedResponse> FindLastResponse(uint64_t client_id,
                                                   uint16_t msg_id) const {
    const auto responses = response_sender_->GetCapturedResponses();
    auto it = std::find_if(
        responses.rbegin(),
        responses.rend(),
        [client_id, msg_id](const CapturedResponse& response) {
          return response.client_id == client_id && response.msg_id == msg_id;
        });
    if (it == responses.rend()) {
      return std::nullopt;
    }
    return *it;
  }

  asio::io_context io_context_;
  entt::registry registry_;
  std::unique_ptr<CoroutineExecutor> executor_;
  std::unique_ptr<MockResponseSender> response_sender_;
  std::unique_ptr<AchievementHandler> handler_;
};

TEST_F(AchievementHandlerTest, ListReturnsDefaultAchievements) {
  const auto player = CreatePlayer(1001, 7001, "Player");

  HandlerContext ctx;
  ctx.client_id = player.client_id;
  ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kAchievementListReq);
  ctx.entity = player.entity;
  Dispatch(ctx, BuildListReqPayload());

  const auto rsp = FindLastResponse(
      player.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kAchievementListRsp));
  ASSERT_TRUE(rsp.has_value());
  flatbuffers::Verifier verifier(rsp->payload.data(), rsp->payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::AchievementListRsp>(nullptr));
  const auto* root =
      flatbuffers::GetRoot<mir2::proto::AchievementListRsp>(rsp->payload.data());
  ASSERT_NE(root, nullptr);
  ASSERT_TRUE(root->success());
  ASSERT_NE(root->achievements(), nullptr);
  EXPECT_GE(root->achievements()->size(), 3u);
}

TEST_F(AchievementHandlerTest, ClaimCompletedAchievementGrantsGoldAndSendsUpdate) {
  const auto player = CreatePlayer(2001, 8001, "Player", 100);

  auto& component = registry_.emplace<ecs::AchievementComponent>(player.entity);
  ecs::AchievementProgress progress;
  progress.achievement_id = 9001;
  progress.progress = 10;
  progress.target = 10;
  progress.completed = true;
  progress.claimed = false;
  progress.reward_gold = 500;
  component.achievements.emplace(progress.achievement_id, progress);

  HandlerContext ctx;
  ctx.client_id = player.client_id;
  ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kAchievementClaimReq);
  ctx.entity = player.entity;
  Dispatch(ctx, BuildClaimReqPayload(9001));

  const auto claim_rsp = FindLastResponse(
      player.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kAchievementClaimRsp));
  ASSERT_TRUE(claim_rsp.has_value());
  flatbuffers::Verifier claim_verifier(
      claim_rsp->payload.data(), claim_rsp->payload.size());
  ASSERT_TRUE(claim_verifier.VerifyBuffer<mir2::proto::AchievementClaimRsp>(nullptr));
  const auto* claim_root =
      flatbuffers::GetRoot<mir2::proto::AchievementClaimRsp>(claim_rsp->payload.data());
  ASSERT_NE(claim_root, nullptr);
  ASSERT_TRUE(claim_root->success());
  EXPECT_EQ(claim_root->achievement_id(), 9001u);
  EXPECT_EQ(claim_root->reward_gold(), 500u);

  const auto* attrs = registry_.try_get<ecs::CharacterAttributesComponent>(player.entity);
  ASSERT_NE(attrs, nullptr);
  EXPECT_EQ(attrs->gold, 600);

  const auto update_rsp = FindLastResponse(
      player.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kAchievementUpdate));
  ASSERT_TRUE(update_rsp.has_value());
  flatbuffers::Verifier update_verifier(
      update_rsp->payload.data(), update_rsp->payload.size());
  ASSERT_TRUE(update_verifier.VerifyBuffer<mir2::proto::AchievementUpdate>(nullptr));
  const auto* update_root =
      flatbuffers::GetRoot<mir2::proto::AchievementUpdate>(update_rsp->payload.data());
  ASSERT_NE(update_root, nullptr);
  ASSERT_NE(update_root->achievement(), nullptr);
  EXPECT_TRUE(update_root->achievement()->claimed());
}

}  // namespace
}  // namespace mir2::logic::test
