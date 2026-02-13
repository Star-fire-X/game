#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include <asio/io_context.hpp>
#include <entt/entt.hpp>
#include <flatbuffers/flatbuffers.h>

#include "ecs/components/character_components.h"
#include "ecs/components/guild_component.h"
#include "ecs/event_bus.h"
#include "ecs/systems/guild_system.h"
#include "game/guild/guild_manager.h"
#include "guild_generated.h"
#include "logic/coroutine_executor.h"
#include "logic/handlers/guild/guild_handler.h"
#include "logic/mock_response_sender.h"
#include "logic/services/client_registry.h"
#include "logic/services/player_presence_service.h"
#include "server/common/error_codes.h"

namespace mir2::logic::test {
namespace {

std::vector<uint8_t> BuildCreateGuildPayload(const std::string& name) {
  flatbuffers::FlatBufferBuilder builder;
  const auto name_offset = builder.CreateString(name);
  const auto request = mir2::proto::CreateCreateGuildRequest(builder, name_offset);
  builder.Finish(request);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildJoinGuildPayload(uint32_t guild_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto request = mir2::proto::CreateJoinGuildRequest(builder, guild_id);
  builder.Finish(request);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildLeaveGuildPayload() {
  flatbuffers::FlatBufferBuilder builder;
  const auto request = mir2::proto::CreateLeaveGuildRequest(builder);
  builder.Finish(request);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildDeclareWarPayload(uint32_t target_guild_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto request =
      mir2::proto::CreateDeclareWarRequest(builder, target_guild_id);
  builder.Finish(request);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

class GuildHandlerTest : public ::testing::Test {
 protected:
  struct TestPlayer {
    uint64_t id = 0;
    entt::entity entity = entt::null;
  };

  void SetUp() override {
    executor_ = std::make_unique<CoroutineExecutor>(io_context_, 1);
    response_sender_ = std::make_unique<MockResponseSender>();
    event_bus_ = std::make_unique<ecs::EventBus>(registry_);
    guild_mgr_ = &game::guild::GuildManager::Instance();
    guild_mgr_->Clear(registry_);
    guild_system_ = std::make_unique<ecs::GuildSystem>(*event_bus_, *guild_mgr_);
    player_presence_service_ = std::make_unique<PlayerPresenceService>(registry_);
    handler_ = std::make_unique<GuildHandler>(*executor_,
                                              *response_sender_,
                                              client_registry_,
                                              *player_presence_service_,
                                              *guild_system_,
                                              registry_);
  }

  void TearDown() override {
    if (guild_mgr_) {
      guild_mgr_->Clear(registry_);
    }
    registry_.clear();
  }

  void RunIoContext() {
    io_context_.run();
    io_context_.restart();
  }

  TestPlayer CreateOnlinePlayer(uint64_t id,
                                const std::string& name,
                                int gold = 0) {
    const entt::entity entity = registry_.create();

    auto& identity = registry_.emplace<ecs::CharacterIdentityComponent>(entity);
    identity.id = static_cast<uint32_t>(id);
    identity.account_id = "acc" + std::to_string(id);
    identity.name = name;

    auto& state = registry_.emplace<ecs::CharacterStateComponent>(entity);
    state.is_online = true;

    auto& attributes =
        registry_.emplace<ecs::CharacterAttributesComponent>(entity);
    attributes.gold = gold;

    registry_.emplace<ecs::ChatPreferenceComponent>(entity);
    client_registry_.Track(id);
    return TestPlayer{id, entity};
  }

  uint32_t CreateGuildAs(entt::entity leader, const std::string& guild_name) {
    const int result = guild_system_->CreateGuild(registry_, leader, guild_name);
    EXPECT_EQ(result, 0);
    const auto* member = registry_.try_get<ecs::GuildMemberComponent>(leader);
    EXPECT_NE(member, nullptr);
    return member ? member->guild_id : 0;
  }

  asio::io_context io_context_;
  entt::registry registry_;
  std::unique_ptr<CoroutineExecutor> executor_;
  std::unique_ptr<MockResponseSender> response_sender_;
  std::unique_ptr<ecs::EventBus> event_bus_;
  game::guild::GuildManager* guild_mgr_ = nullptr;
  std::unique_ptr<ecs::GuildSystem> guild_system_;
  std::unique_ptr<PlayerPresenceService> player_presence_service_;
  ClientRegistry client_registry_;
  std::unique_ptr<GuildHandler> handler_;
};

TEST_F(GuildHandlerTest, HandleCreateGuildSuccess) {
  const int initial_gold = static_cast<int>(ecs::GUILD_CREATE_FEE) + 500;
  auto leader = CreateOnlinePlayer(1001u, "Leader", initial_gold);

  HandlerContext ctx;
  ctx.client_id = leader.id;
  ctx.msg_id = static_cast<uint16_t>(mir2::proto::GuildMessageType::CREATE);
  const auto payload = BuildCreateGuildPayload("Knights");

  ASSERT_TRUE(executor_->Spawn(handler_->HandleMessage(ctx, payload.data(), payload.size())));
  RunIoContext();

  const auto* member = registry_.try_get<ecs::GuildMemberComponent>(leader.entity);
  ASSERT_NE(member, nullptr);
  EXPECT_NE(member->guild_id, 0u);

  auto* guild = guild_mgr_->GetGuild(member->guild_id, registry_);
  ASSERT_NE(guild, nullptr);
  EXPECT_EQ(guild->guild_name, "Knights");
  EXPECT_EQ(guild->members.size(), 1u);
  EXPECT_TRUE(guild->IsLeader(leader.entity));

  const auto& attributes = registry_.get<ecs::CharacterAttributesComponent>(leader.entity);
  EXPECT_EQ(attributes.gold, initial_gold - static_cast<int>(ecs::GUILD_CREATE_FEE));

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].client_id, leader.id);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::proto::GuildMessageType::CREATE));

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::CreateGuildResponse>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::CreateGuildResponse>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_TRUE(rsp->success());
}

TEST_F(GuildHandlerTest, HandleCreateGuildInvalidPayload) {
  HandlerContext ctx;
  ctx.client_id = 999u;
  ctx.msg_id = static_cast<uint16_t>(mir2::proto::GuildMessageType::CREATE);
  const std::vector<uint8_t> payload = {0x01, 0x02, 0x03};

  ASSERT_TRUE(executor_->Spawn(handler_->HandleMessage(ctx, payload.data(), payload.size())));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::CreateGuildResponse>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::CreateGuildResponse>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_FALSE(rsp->success());
  EXPECT_EQ(rsp->error_code(),
            static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
  EXPECT_EQ(guild_mgr_->GuildCount(), 0u);
}

TEST_F(GuildHandlerTest, HandleJoinAndLeaveGuildSuccess) {
  const int leader_gold = static_cast<int>(ecs::GUILD_CREATE_FEE) + 500;
  auto leader = CreateOnlinePlayer(2001u, "Leader", leader_gold);
  auto member = CreateOnlinePlayer(2002u, "Member", 0);

  const uint32_t guild_id = CreateGuildAs(leader.entity, "Adventurers");
  ASSERT_NE(guild_id, 0u);

  const auto join_payload = BuildJoinGuildPayload(guild_id);
  HandlerContext join_ctx;
  join_ctx.client_id = member.id;
  join_ctx.msg_id = static_cast<uint16_t>(mir2::proto::GuildMessageType::JOIN);

  ASSERT_TRUE(
      executor_->Spawn(handler_->HandleMessage(join_ctx, join_payload.data(), join_payload.size())));
  RunIoContext();

  const auto* member_comp =
      registry_.try_get<ecs::GuildMemberComponent>(member.entity);
  ASSERT_NE(member_comp, nullptr);
  EXPECT_EQ(member_comp->guild_id, guild_id);

  auto* guild = guild_mgr_->GetGuild(guild_id, registry_);
  ASSERT_NE(guild, nullptr);
  EXPECT_EQ(guild->members.size(), 2u);
  EXPECT_TRUE(guild->IsMember(member.entity));

  const auto leave_payload = BuildLeaveGuildPayload();
  join_ctx.msg_id = static_cast<uint16_t>(mir2::proto::GuildMessageType::LEAVE);
  ASSERT_TRUE(executor_->Spawn(
      handler_->HandleMessage(join_ctx, leave_payload.data(), leave_payload.size())));
  RunIoContext();

  EXPECT_EQ(registry_.try_get<ecs::GuildMemberComponent>(member.entity), nullptr);
  guild = guild_mgr_->GetGuild(guild_id, registry_);
  ASSERT_NE(guild, nullptr);
  EXPECT_EQ(guild->members.size(), 1u);
  EXPECT_TRUE(guild->IsMember(leader.entity));
}

TEST_F(GuildHandlerTest, HandleDeclareAndCancelWarSuccess) {
  const int war_capital =
      static_cast<int>(ecs::GUILD_CREATE_FEE + ecs::GUILD_WAR_FEE) + 500;
  auto leader_a = CreateOnlinePlayer(3001u, "LeaderA", war_capital);
  auto leader_b = CreateOnlinePlayer(3002u, "LeaderB",
                                     static_cast<int>(ecs::GUILD_CREATE_FEE) + 500);

  const uint32_t guild_a = CreateGuildAs(leader_a.entity, "GuildA");
  const uint32_t guild_b = CreateGuildAs(leader_b.entity, "GuildB");
  ASSERT_NE(guild_a, 0u);
  ASSERT_NE(guild_b, 0u);

  HandlerContext ctx;
  ctx.client_id = leader_a.id;
  ctx.msg_id = static_cast<uint16_t>(mir2::proto::GuildMessageType::DECLARE_WAR);
  const auto war_payload = BuildDeclareWarPayload(guild_b);

  ASSERT_TRUE(executor_->Spawn(handler_->HandleMessage(ctx, war_payload.data(), war_payload.size())));
  RunIoContext();
  EXPECT_TRUE(guild_mgr_->IsAtWar(guild_a, guild_b));

  ctx.msg_id = static_cast<uint16_t>(mir2::proto::GuildMessageType::CANCEL_WAR);
  ASSERT_TRUE(executor_->Spawn(handler_->HandleMessage(ctx, war_payload.data(), war_payload.size())));
  RunIoContext();
  EXPECT_FALSE(guild_mgr_->IsAtWar(guild_a, guild_b));
}

}  // namespace
}  // namespace mir2::logic::test
