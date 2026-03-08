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

#include "common/enums.h"
#include "ecs/components/character_components.h"
#include "logic/coroutine_executor.h"
#include "logic/handlers/ranking/ranking_handler.h"
#include "logic/mock_response_sender.h"
#include "logic/services/ranking_service.h"
#include "ranking_generated.h"

namespace mir2::logic::test {
namespace {

std::vector<uint8_t> BuildRankingReqPayload(mir2::proto::RankingType type,
                                            uint32_t page,
                                            uint32_t page_size) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateRankingReq(builder, type, page, page_size);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildMyRankReqPayload(mir2::proto::RankingType type) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateRankingMyRankReq(builder, type);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

class RankingHandlerTest : public ::testing::Test {
 protected:
  struct Player {
    uint64_t character_id = 0;
    uint64_t client_id = 0;
    entt::entity entity = entt::null;
  };

  void SetUp() override {
    executor_ = std::make_unique<CoroutineExecutor>(io_context_, 1);
    response_sender_ = std::make_unique<MockResponseSender>();
    ranking_service_ = std::make_unique<RankingService>(registry_);
    handler_ = std::make_unique<RankingHandler>(
        *response_sender_, *ranking_service_, registry_);
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
                      int level) {
    const entt::entity entity = registry_.create();

    auto& identity = registry_.emplace<ecs::CharacterIdentityComponent>(entity);
    identity.id = static_cast<uint32_t>(character_id);
    identity.account_id = static_cast<ecs::AccountId>(character_id);
    identity.name = name;

    auto& attributes = registry_.emplace<ecs::CharacterAttributesComponent>(entity);
    attributes.level = level;
    attributes.gold = level * 100;
    attributes.pk_level = level / 5;

    return Player{character_id, client_id, entity};
  }

  std::optional<CapturedResponse> FindResponse(uint64_t client_id,
                                               uint16_t msg_id) const {
    const auto responses = response_sender_->GetCapturedResponses();
    auto it = std::find_if(
        responses.begin(),
        responses.end(),
        [client_id, msg_id](const CapturedResponse& response) {
          return response.client_id == client_id && response.msg_id == msg_id;
        });
    if (it == responses.end()) {
      return std::nullopt;
    }
    return *it;
  }

  asio::io_context io_context_;
  entt::registry registry_;
  std::unique_ptr<CoroutineExecutor> executor_;
  std::unique_ptr<MockResponseSender> response_sender_;
  std::unique_ptr<RankingService> ranking_service_;
  std::unique_ptr<RankingHandler> handler_;
};

TEST_F(RankingHandlerTest, RankingRequestReturnsSortedEntries) {
  const auto low = CreatePlayer(1001, 5001, "Low", 10);
  const auto high = CreatePlayer(1002, 5002, "High", 30);

  HandlerContext ctx;
  ctx.client_id = low.client_id;
  ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kRankingReq);
  ctx.entity = low.entity;
  Dispatch(ctx, BuildRankingReqPayload(mir2::proto::RankingType::LEVEL, 1, 20));

  const auto rsp = FindResponse(low.client_id,
                                static_cast<uint16_t>(mir2::common::MsgId::kRankingRsp));
  ASSERT_TRUE(rsp.has_value());
  flatbuffers::Verifier verifier(rsp->payload.data(), rsp->payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::RankingRsp>(nullptr));
  const auto* root = flatbuffers::GetRoot<mir2::proto::RankingRsp>(rsp->payload.data());
  ASSERT_NE(root, nullptr);
  ASSERT_TRUE(root->success());
  ASSERT_NE(root->entries(), nullptr);
  ASSERT_EQ(root->entries()->size(), 2u);
  EXPECT_EQ(root->entries()->Get(0)->entity_id(), high.character_id);
  EXPECT_EQ(root->entries()->Get(0)->value(), 30);
  EXPECT_EQ(root->entries()->Get(1)->entity_id(), low.character_id);
}

TEST_F(RankingHandlerTest, MyRankReturnsCurrentPlayerRank) {
  const auto left = CreatePlayer(2001, 6001, "Left", 12);
  const auto right = CreatePlayer(2002, 6002, "Right", 24);

  HandlerContext ctx;
  ctx.client_id = left.client_id;
  ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kRankingMyRankReq);
  ctx.entity = left.entity;
  Dispatch(ctx, BuildMyRankReqPayload(mir2::proto::RankingType::LEVEL));

  const auto rsp = FindResponse(left.client_id,
                                static_cast<uint16_t>(mir2::common::MsgId::kRankingMyRankRsp));
  ASSERT_TRUE(rsp.has_value());
  flatbuffers::Verifier verifier(rsp->payload.data(), rsp->payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::RankingMyRankRsp>(nullptr));
  const auto* root =
      flatbuffers::GetRoot<mir2::proto::RankingMyRankRsp>(rsp->payload.data());
  ASSERT_NE(root, nullptr);
  ASSERT_TRUE(root->success());
  EXPECT_EQ(root->rank(), 2u);
  EXPECT_EQ(root->value(), 12);

  (void)right;
}

}  // namespace
}  // namespace mir2::logic::test
