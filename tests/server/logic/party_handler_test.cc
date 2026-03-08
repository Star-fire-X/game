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
#include "ecs/components/party_component.h"
#include "logic/coroutine_executor.h"
#include "logic/handlers/party/party_handler.h"
#include "logic/mock_response_sender.h"
#include "logic/services/client_registry.h"
#include "logic/services/session_role_store.h"
#include "party_generated.h"

namespace mir2::logic::test {
namespace {

std::vector<uint8_t> BuildInvitePayload(uint32_t target_character_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreatePartyInviteReq(builder, target_character_id);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildJoinPayload(uint64_t party_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreatePartyJoinReq(builder, party_id);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildLeavePayload() {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreatePartyLeaveReq(builder);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildKickPayload(uint32_t target_character_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreatePartyKickReq(builder, target_character_id);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

class PartyHandlerTest : public ::testing::Test {
 protected:
  struct Player {
    uint64_t character_id = 0;
    uint64_t client_id = 0;
    entt::entity entity = entt::null;
  };

  void SetUp() override {
    executor_ = std::make_unique<CoroutineExecutor>(io_context_, 1);
    response_sender_ = std::make_unique<MockResponseSender>();
    handler_ = std::make_unique<PartyHandler>(*response_sender_,
                                              client_registry_,
                                              registry_,
                                              &role_store_);
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
                      const std::string& name) {
    const entt::entity entity = registry_.create();

    auto& identity = registry_.emplace<ecs::CharacterIdentityComponent>(entity);
    identity.id = static_cast<uint32_t>(character_id);
    identity.account_id = static_cast<ecs::AccountId>(character_id);
    identity.name = name;

    auto& state = registry_.emplace<ecs::CharacterStateComponent>(entity);
    state.is_online = true;

    auto& attrs = registry_.emplace<ecs::CharacterAttributesComponent>(entity);
    attrs.hp = 100;
    attrs.max_hp = 120;

    client_registry_.Track(client_id);
    role_store_.BindClientRole(client_id, character_id);

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
  ClientRegistry client_registry_;
  RoleStore role_store_;
  std::unique_ptr<PartyHandler> handler_;
};

TEST_F(PartyHandlerTest, InviteCreatesPartyAndBroadcastsUpdate) {
  const auto leader = CreatePlayer(1001, 4001, "Leader");
  const auto member = CreatePlayer(1002, 4002, "Member");

  HandlerContext invite_ctx;
  invite_ctx.client_id = leader.client_id;
  invite_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kPartyInviteReq);
  invite_ctx.entity = leader.entity;
  Dispatch(invite_ctx, BuildInvitePayload(static_cast<uint32_t>(member.character_id)));

  const auto invite_rsp = FindResponse(
      leader.client_id, static_cast<uint16_t>(mir2::common::MsgId::kPartyInviteRsp));
  ASSERT_TRUE(invite_rsp.has_value());
  flatbuffers::Verifier invite_verifier(invite_rsp->payload.data(), invite_rsp->payload.size());
  ASSERT_TRUE(invite_verifier.VerifyBuffer<mir2::proto::PartyInviteRsp>(nullptr));
  const auto* invite_root =
      flatbuffers::GetRoot<mir2::proto::PartyInviteRsp>(invite_rsp->payload.data());
  ASSERT_NE(invite_root, nullptr);
  EXPECT_TRUE(invite_root->success());

  const auto* leader_member_comp =
      registry_.try_get<ecs::PartyMemberComponent>(leader.entity);
  const auto* member_comp = registry_.try_get<ecs::PartyMemberComponent>(member.entity);
  ASSERT_NE(leader_member_comp, nullptr);
  ASSERT_NE(member_comp, nullptr);
  EXPECT_NE(leader_member_comp->party_id, 0u);
  EXPECT_EQ(leader_member_comp->party_id, member_comp->party_id);

  const auto leader_update = FindResponse(
      leader.client_id, static_cast<uint16_t>(mir2::common::MsgId::kPartyUpdate));
  ASSERT_TRUE(leader_update.has_value());
  flatbuffers::Verifier update_verifier(
      leader_update->payload.data(), leader_update->payload.size());
  ASSERT_TRUE(update_verifier.VerifyBuffer<mir2::proto::PartyUpdate>(nullptr));
  const auto* update_root =
      flatbuffers::GetRoot<mir2::proto::PartyUpdate>(leader_update->payload.data());
  ASSERT_NE(update_root, nullptr);
  EXPECT_EQ(update_root->party_id(), leader_member_comp->party_id);
  ASSERT_NE(update_root->members(), nullptr);
  EXPECT_EQ(update_root->members()->size(), 2u);
}

TEST_F(PartyHandlerTest, LeaveSendsClearUpdateToLeaver) {
  const auto leader = CreatePlayer(2001, 5001, "Leader");
  const auto member = CreatePlayer(2002, 5002, "Member");

  HandlerContext invite_ctx;
  invite_ctx.client_id = leader.client_id;
  invite_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kPartyInviteReq);
  invite_ctx.entity = leader.entity;
  Dispatch(invite_ctx, BuildInvitePayload(static_cast<uint32_t>(member.character_id)));

  response_sender_->Clear();

  HandlerContext leave_ctx;
  leave_ctx.client_id = member.client_id;
  leave_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kPartyLeaveReq);
  leave_ctx.entity = member.entity;
  Dispatch(leave_ctx, BuildLeavePayload());

  const auto leave_rsp = FindResponse(
      member.client_id, static_cast<uint16_t>(mir2::common::MsgId::kPartyLeaveRsp));
  ASSERT_TRUE(leave_rsp.has_value());
  flatbuffers::Verifier leave_verifier(leave_rsp->payload.data(), leave_rsp->payload.size());
  ASSERT_TRUE(leave_verifier.VerifyBuffer<mir2::proto::PartyLeaveRsp>(nullptr));
  const auto* leave_root =
      flatbuffers::GetRoot<mir2::proto::PartyLeaveRsp>(leave_rsp->payload.data());
  ASSERT_NE(leave_root, nullptr);
  EXPECT_TRUE(leave_root->success());

  const auto clear_update = FindResponse(
      member.client_id, static_cast<uint16_t>(mir2::common::MsgId::kPartyUpdate));
  ASSERT_TRUE(clear_update.has_value());
  flatbuffers::Verifier clear_verifier(
      clear_update->payload.data(), clear_update->payload.size());
  ASSERT_TRUE(clear_verifier.VerifyBuffer<mir2::proto::PartyUpdate>(nullptr));
  const auto* clear_root =
      flatbuffers::GetRoot<mir2::proto::PartyUpdate>(clear_update->payload.data());
  ASSERT_NE(clear_root, nullptr);
  EXPECT_EQ(clear_root->party_id(), 0u);

  EXPECT_FALSE(registry_.any_of<ecs::PartyMemberComponent>(member.entity));
  EXPECT_TRUE(registry_.any_of<ecs::PartyMemberComponent>(leader.entity));
}

TEST_F(PartyHandlerTest, KickRejectedWhenRequesterIsNotLeader) {
  const auto leader = CreatePlayer(3001, 6001, "Leader");
  const auto member = CreatePlayer(3002, 6002, "Member");

  HandlerContext invite_ctx;
  invite_ctx.client_id = leader.client_id;
  invite_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kPartyInviteReq);
  invite_ctx.entity = leader.entity;
  Dispatch(invite_ctx, BuildInvitePayload(static_cast<uint32_t>(member.character_id)));

  response_sender_->Clear();

  HandlerContext kick_ctx;
  kick_ctx.client_id = member.client_id;
  kick_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kPartyKickReq);
  kick_ctx.entity = member.entity;
  Dispatch(kick_ctx, BuildKickPayload(static_cast<uint32_t>(leader.character_id)));

  const auto kick_rsp = FindResponse(
      member.client_id, static_cast<uint16_t>(mir2::common::MsgId::kPartyKickRsp));
  ASSERT_TRUE(kick_rsp.has_value());

  flatbuffers::Verifier kick_verifier(kick_rsp->payload.data(), kick_rsp->payload.size());
  ASSERT_TRUE(kick_verifier.VerifyBuffer<mir2::proto::PartyKickRsp>(nullptr));
  const auto* kick_root =
      flatbuffers::GetRoot<mir2::proto::PartyKickRsp>(kick_rsp->payload.data());
  ASSERT_NE(kick_root, nullptr);
  EXPECT_FALSE(kick_root->success());
  EXPECT_EQ(kick_root->error_code(),
            static_cast<int>(mir2::proto::ErrorCode::ERR_INVALID_ACTION));

  EXPECT_TRUE(registry_.any_of<ecs::PartyMemberComponent>(leader.entity));
  EXPECT_TRUE(registry_.any_of<ecs::PartyMemberComponent>(member.entity));
}

TEST_F(PartyHandlerTest, JoinAddsMemberToExistingParty) {
  const auto leader = CreatePlayer(4001, 7001, "Leader");
  const auto member = CreatePlayer(4002, 7002, "Member");
  const auto joiner = CreatePlayer(4003, 7003, "Joiner");

  HandlerContext invite_ctx;
  invite_ctx.client_id = leader.client_id;
  invite_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kPartyInviteReq);
  invite_ctx.entity = leader.entity;
  Dispatch(invite_ctx, BuildInvitePayload(static_cast<uint32_t>(member.character_id)));

  const auto* leader_member_comp =
      registry_.try_get<ecs::PartyMemberComponent>(leader.entity);
  ASSERT_NE(leader_member_comp, nullptr);
  const uint64_t party_id = leader_member_comp->party_id;

  response_sender_->Clear();

  HandlerContext join_ctx;
  join_ctx.client_id = joiner.client_id;
  join_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kPartyJoinReq);
  join_ctx.entity = joiner.entity;
  Dispatch(join_ctx, BuildJoinPayload(party_id));

  const auto join_rsp = FindResponse(
      joiner.client_id, static_cast<uint16_t>(mir2::common::MsgId::kPartyJoinRsp));
  ASSERT_TRUE(join_rsp.has_value());
  flatbuffers::Verifier join_verifier(join_rsp->payload.data(), join_rsp->payload.size());
  ASSERT_TRUE(join_verifier.VerifyBuffer<mir2::proto::PartyJoinRsp>(nullptr));
  const auto* join_root =
      flatbuffers::GetRoot<mir2::proto::PartyJoinRsp>(join_rsp->payload.data());
  ASSERT_NE(join_root, nullptr);
  EXPECT_TRUE(join_root->success());

  const auto* joiner_member_comp =
      registry_.try_get<ecs::PartyMemberComponent>(joiner.entity);
  ASSERT_NE(joiner_member_comp, nullptr);
  EXPECT_EQ(joiner_member_comp->party_id, party_id);
}

}  // namespace
}  // namespace mir2::logic::test
