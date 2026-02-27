#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <asio/io_context.hpp>
#include <entt/entt.hpp>
#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "logic/coroutine_executor.h"
#include "logic/handlers/mail/mail_handler.h"
#include "logic/mock_response_sender.h"
#include "logic/services/client_registry.h"
#include "logic/services/session_role_store.h"
#include "mail_generated.h"

namespace mir2::logic::test {
namespace {

std::vector<uint8_t> BuildSendReqPayload(uint64_t target_character_id,
                                         const std::string& subject,
                                         const std::string& content,
                                         uint32_t gold = 0,
                                         const std::vector<std::pair<uint32_t, uint32_t>>& items = {}) {
  flatbuffers::FlatBufferBuilder builder;
  const auto subject_offset = builder.CreateString(subject);
  const auto content_offset = builder.CreateString(content);
  std::vector<flatbuffers::Offset<mir2::proto::MailAttachmentItem>> item_offsets;
  item_offsets.reserve(items.size());
  for (const auto& [item_id, count] : items) {
    item_offsets.push_back(mir2::proto::CreateMailAttachmentItem(builder, item_id, count));
  }
  const auto items_offset = builder.CreateVector(item_offsets);
  const auto req = mir2::proto::CreateMailSendReq(
      builder, target_character_id, subject_offset, content_offset, gold, items_offset, 0);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildListReqPayload() {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateMailListReq(builder);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildReadReqPayload(uint64_t mail_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateMailReadReq(builder, mail_id);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildClaimReqPayload(uint64_t mail_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateMailClaimReq(builder, mail_id);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildDeleteReqPayload(uint64_t mail_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateMailDeleteReq(builder, mail_id);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

class MailHandlerTest : public ::testing::Test {
 protected:
  struct Player {
    uint64_t character_id = 0;
    uint64_t client_id = 0;
    entt::entity entity = entt::null;
  };

  void SetUp() override {
    executor_ = std::make_unique<CoroutineExecutor>(io_context_, 1);
    response_sender_ = std::make_unique<MockResponseSender>();
    handler_ = std::make_unique<MailHandler>(
        *response_sender_, client_registry_, registry_, &role_store_);
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

    auto& attributes = registry_.emplace<ecs::CharacterAttributesComponent>(entity);
    attributes.gold = gold;

    role_store_.BindClientRole(client_id, character_id);
    client_registry_.Track(client_id);

    return Player{character_id, client_id, entity};
  }

  entt::entity CreateInventoryItem(entt::entity owner,
                                   int slot,
                                   uint32_t item_id,
                                   int count) {
    const entt::entity item = registry_.create();
    auto& item_comp = registry_.emplace<ecs::ItemComponent>(item);
    item_comp.item_id = item_id;
    item_comp.count = count;
    item_comp.instance_id = static_cast<uint64_t>(entt::to_integral(item));

    auto& owner_comp = registry_.emplace<ecs::InventoryOwnerComponent>(item);
    owner_comp.owner = owner;
    owner_comp.slot_index = slot;
    return item;
  }

  int CountOwnedItem(entt::entity owner, uint32_t item_id) const {
    int total = 0;
    auto view = registry_.view<ecs::ItemComponent, ecs::InventoryOwnerComponent>();
    for (const entt::entity entity : view) {
      const auto& item = view.get<ecs::ItemComponent>(entity);
      const auto& owner_comp = view.get<ecs::InventoryOwnerComponent>(entity);
      if (owner_comp.owner == owner && owner_comp.slot_index >= 0 && item.item_id == item_id) {
        total += item.count;
      }
    }
    return total;
  }

  const ecs::ItemComponent* FindOwnedItem(entt::entity owner, uint32_t item_id) const {
    auto view = registry_.view<ecs::ItemComponent, ecs::InventoryOwnerComponent>();
    for (const entt::entity entity : view) {
      const auto& item = view.get<ecs::ItemComponent>(entity);
      const auto& owner_comp = view.get<ecs::InventoryOwnerComponent>(entity);
      if (owner_comp.owner == owner && owner_comp.slot_index >= 0 && item.item_id == item_id) {
        return &item;
      }
    }
    return nullptr;
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
  ClientRegistry client_registry_;
  RoleStore role_store_;
  std::unique_ptr<MailHandler> handler_;
};

TEST_F(MailHandlerTest, SendThenListAndReadMail) {
  const auto sender = CreatePlayer(1001, 4001, "Sender");
  const auto receiver = CreatePlayer(1002, 4002, "Receiver");

  HandlerContext send_ctx;
  send_ctx.client_id = sender.client_id;
  send_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kMailSendReq);
  send_ctx.entity = sender.entity;
  Dispatch(send_ctx, BuildSendReqPayload(receiver.character_id, "Hello", "World"));

  const auto send_rsp = FindLastResponse(
      sender.client_id, static_cast<uint16_t>(mir2::common::MsgId::kMailSendRsp));
  ASSERT_TRUE(send_rsp.has_value());
  flatbuffers::Verifier send_verifier(send_rsp->payload.data(), send_rsp->payload.size());
  ASSERT_TRUE(send_verifier.VerifyBuffer<mir2::proto::MailSendRsp>(nullptr));
  const auto* send_root = flatbuffers::GetRoot<mir2::proto::MailSendRsp>(send_rsp->payload.data());
  ASSERT_NE(send_root, nullptr);
  ASSERT_TRUE(send_root->success());
  const uint64_t mail_id = send_root->mail_id();

  const auto notify_rsp = FindLastResponse(
      receiver.client_id, static_cast<uint16_t>(mir2::common::MsgId::kMailNotify));
  ASSERT_TRUE(notify_rsp.has_value());
  flatbuffers::Verifier notify_verifier(notify_rsp->payload.data(), notify_rsp->payload.size());
  ASSERT_TRUE(notify_verifier.VerifyBuffer<mir2::proto::MailNotify>(nullptr));

  HandlerContext list_ctx;
  list_ctx.client_id = receiver.client_id;
  list_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kMailListReq);
  list_ctx.entity = receiver.entity;
  Dispatch(list_ctx, BuildListReqPayload());

  const auto list_rsp = FindLastResponse(
      receiver.client_id, static_cast<uint16_t>(mir2::common::MsgId::kMailListRsp));
  ASSERT_TRUE(list_rsp.has_value());
  flatbuffers::Verifier list_verifier(list_rsp->payload.data(), list_rsp->payload.size());
  ASSERT_TRUE(list_verifier.VerifyBuffer<mir2::proto::MailListRsp>(nullptr));
  const auto* list_root = flatbuffers::GetRoot<mir2::proto::MailListRsp>(list_rsp->payload.data());
  ASSERT_NE(list_root, nullptr);
  ASSERT_TRUE(list_root->success());
  ASSERT_NE(list_root->mails(), nullptr);
  ASSERT_EQ(list_root->mails()->size(), 1u);
  EXPECT_EQ(list_root->mails()->Get(0)->mail_id(), mail_id);
  EXPECT_FALSE(list_root->mails()->Get(0)->is_read());

  HandlerContext read_ctx;
  read_ctx.client_id = receiver.client_id;
  read_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kMailReadReq);
  read_ctx.entity = receiver.entity;
  Dispatch(read_ctx, BuildReadReqPayload(mail_id));

  const auto read_rsp = FindLastResponse(
      receiver.client_id, static_cast<uint16_t>(mir2::common::MsgId::kMailReadRsp));
  ASSERT_TRUE(read_rsp.has_value());
  flatbuffers::Verifier read_verifier(read_rsp->payload.data(), read_rsp->payload.size());
  ASSERT_TRUE(read_verifier.VerifyBuffer<mir2::proto::MailReadRsp>(nullptr));
  const auto* read_root = flatbuffers::GetRoot<mir2::proto::MailReadRsp>(read_rsp->payload.data());
  ASSERT_NE(read_root, nullptr);
  ASSERT_TRUE(read_root->success());
  ASSERT_NE(read_root->mail(), nullptr);
  EXPECT_EQ(read_root->mail()->mail_id(), mail_id);
  EXPECT_EQ(read_root->mail()->subject()->str(), "Hello");
}

TEST_F(MailHandlerTest, ClaimMailAddsGoldAndDeleteSucceeds) {
  const auto sender = CreatePlayer(2001, 5001, "Sender", 500);
  const auto receiver = CreatePlayer(2002, 5002, "Receiver", 100);

  HandlerContext send_ctx;
  send_ctx.client_id = sender.client_id;
  send_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kMailSendReq);
  send_ctx.entity = sender.entity;
  Dispatch(send_ctx, BuildSendReqPayload(receiver.character_id, "Gold", "Take it", 250));

  const auto send_rsp = FindLastResponse(
      sender.client_id, static_cast<uint16_t>(mir2::common::MsgId::kMailSendRsp));
  ASSERT_TRUE(send_rsp.has_value());
  flatbuffers::Verifier send_verifier(send_rsp->payload.data(), send_rsp->payload.size());
  ASSERT_TRUE(send_verifier.VerifyBuffer<mir2::proto::MailSendRsp>(nullptr));
  const auto* send_root = flatbuffers::GetRoot<mir2::proto::MailSendRsp>(send_rsp->payload.data());
  ASSERT_NE(send_root, nullptr);
  ASSERT_TRUE(send_root->success());
  const uint64_t mail_id = send_root->mail_id();

  HandlerContext claim_ctx;
  claim_ctx.client_id = receiver.client_id;
  claim_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kMailClaimReq);
  claim_ctx.entity = receiver.entity;
  Dispatch(claim_ctx, BuildClaimReqPayload(mail_id));

  const auto claim_rsp = FindLastResponse(
      receiver.client_id, static_cast<uint16_t>(mir2::common::MsgId::kMailClaimRsp));
  ASSERT_TRUE(claim_rsp.has_value());
  flatbuffers::Verifier claim_verifier(claim_rsp->payload.data(), claim_rsp->payload.size());
  ASSERT_TRUE(claim_verifier.VerifyBuffer<mir2::proto::MailClaimRsp>(nullptr));
  const auto* claim_root =
      flatbuffers::GetRoot<mir2::proto::MailClaimRsp>(claim_rsp->payload.data());
  ASSERT_NE(claim_root, nullptr);
  ASSERT_TRUE(claim_root->success());

  const auto* attrs = registry_.try_get<ecs::CharacterAttributesComponent>(receiver.entity);
  ASSERT_NE(attrs, nullptr);
  EXPECT_EQ(attrs->gold, 350);

  Dispatch(claim_ctx, BuildClaimReqPayload(mail_id));
  const auto claim_again_rsp = FindLastResponse(
      receiver.client_id, static_cast<uint16_t>(mir2::common::MsgId::kMailClaimRsp));
  ASSERT_TRUE(claim_again_rsp.has_value());
  flatbuffers::Verifier claim_again_verifier(
      claim_again_rsp->payload.data(), claim_again_rsp->payload.size());
  ASSERT_TRUE(claim_again_verifier.VerifyBuffer<mir2::proto::MailClaimRsp>(nullptr));
  const auto* claim_again_root =
      flatbuffers::GetRoot<mir2::proto::MailClaimRsp>(claim_again_rsp->payload.data());
  ASSERT_NE(claim_again_root, nullptr);
  EXPECT_FALSE(claim_again_root->success());
  EXPECT_EQ(claim_again_root->error_code(),
            static_cast<int>(mir2::proto::ErrorCode::ERR_MAIL_ALREADY_CLAIMED));

  HandlerContext delete_ctx;
  delete_ctx.client_id = receiver.client_id;
  delete_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kMailDeleteReq);
  delete_ctx.entity = receiver.entity;
  Dispatch(delete_ctx, BuildDeleteReqPayload(mail_id));

  const auto delete_rsp = FindLastResponse(
      receiver.client_id, static_cast<uint16_t>(mir2::common::MsgId::kMailDeleteRsp));
  ASSERT_TRUE(delete_rsp.has_value());
  flatbuffers::Verifier delete_verifier(delete_rsp->payload.data(), delete_rsp->payload.size());
  ASSERT_TRUE(delete_verifier.VerifyBuffer<mir2::proto::MailDeleteRsp>(nullptr));
  const auto* delete_root =
      flatbuffers::GetRoot<mir2::proto::MailDeleteRsp>(delete_rsp->payload.data());
  ASSERT_NE(delete_root, nullptr);
  EXPECT_TRUE(delete_root->success());
}

TEST_F(MailHandlerTest, SendAndClaimMailTransfersGoldAndItems) {
  const auto sender = CreatePlayer(3001, 6001, "Sender", 500);
  const auto receiver = CreatePlayer(3002, 6002, "Receiver", 100);
  CreateInventoryItem(sender.entity, 1, 93001, 4);
  CreateInventoryItem(sender.entity, 2, 93001, 3);

  HandlerContext send_ctx;
  send_ctx.client_id = sender.client_id;
  send_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kMailSendReq);
  send_ctx.entity = sender.entity;
  Dispatch(send_ctx,
           BuildSendReqPayload(receiver.character_id,
                               "Attach",
                               "Transfer",
                               120,
                               {{93001, 6}}));

  const auto send_rsp = FindLastResponse(
      sender.client_id, static_cast<uint16_t>(mir2::common::MsgId::kMailSendRsp));
  ASSERT_TRUE(send_rsp.has_value());
  flatbuffers::Verifier send_verifier(send_rsp->payload.data(), send_rsp->payload.size());
  ASSERT_TRUE(send_verifier.VerifyBuffer<mir2::proto::MailSendRsp>(nullptr));
  const auto* send_root = flatbuffers::GetRoot<mir2::proto::MailSendRsp>(send_rsp->payload.data());
  ASSERT_NE(send_root, nullptr);
  ASSERT_TRUE(send_root->success());
  const uint64_t mail_id = send_root->mail_id();

  const auto* sender_attrs = registry_.try_get<ecs::CharacterAttributesComponent>(sender.entity);
  ASSERT_NE(sender_attrs, nullptr);
  EXPECT_EQ(sender_attrs->gold, 380);
  EXPECT_EQ(CountOwnedItem(sender.entity, 93001), 1);

  HandlerContext claim_ctx;
  claim_ctx.client_id = receiver.client_id;
  claim_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kMailClaimReq);
  claim_ctx.entity = receiver.entity;
  Dispatch(claim_ctx, BuildClaimReqPayload(mail_id));

  const auto claim_rsp = FindLastResponse(
      receiver.client_id, static_cast<uint16_t>(mir2::common::MsgId::kMailClaimRsp));
  ASSERT_TRUE(claim_rsp.has_value());
  flatbuffers::Verifier claim_verifier(claim_rsp->payload.data(), claim_rsp->payload.size());
  ASSERT_TRUE(claim_verifier.VerifyBuffer<mir2::proto::MailClaimRsp>(nullptr));
  const auto* claim_root =
      flatbuffers::GetRoot<mir2::proto::MailClaimRsp>(claim_rsp->payload.data());
  ASSERT_NE(claim_root, nullptr);
  ASSERT_TRUE(claim_root->success());

  const auto* receiver_attrs =
      registry_.try_get<ecs::CharacterAttributesComponent>(receiver.entity);
  ASSERT_NE(receiver_attrs, nullptr);
  EXPECT_EQ(receiver_attrs->gold, 220);
  EXPECT_EQ(CountOwnedItem(receiver.entity, 93001), 6);
}

TEST_F(MailHandlerTest, SendMailFailsWhenSenderAssetsInsufficient) {
  const auto sender = CreatePlayer(3003, 6003, "Sender", 10);
  const auto receiver = CreatePlayer(3004, 6004, "Receiver", 0);

  HandlerContext send_ctx;
  send_ctx.client_id = sender.client_id;
  send_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kMailSendReq);
  send_ctx.entity = sender.entity;
  Dispatch(send_ctx,
           BuildSendReqPayload(receiver.character_id,
                               "Attach",
                               "Fail",
                               100,
                               {{94001, 1}}));

  const auto send_rsp = FindLastResponse(
      sender.client_id, static_cast<uint16_t>(mir2::common::MsgId::kMailSendRsp));
  ASSERT_TRUE(send_rsp.has_value());
  flatbuffers::Verifier send_verifier(send_rsp->payload.data(), send_rsp->payload.size());
  ASSERT_TRUE(send_verifier.VerifyBuffer<mir2::proto::MailSendRsp>(nullptr));
  const auto* send_root = flatbuffers::GetRoot<mir2::proto::MailSendRsp>(send_rsp->payload.data());
  ASSERT_NE(send_root, nullptr);
  EXPECT_FALSE(send_root->success());
  EXPECT_EQ(send_root->error_code(),
            static_cast<int>(mir2::proto::ErrorCode::ERR_MAIL_ATTACHMENT_INVALID));

  const auto* sender_attrs = registry_.try_get<ecs::CharacterAttributesComponent>(sender.entity);
  ASSERT_NE(sender_attrs, nullptr);
  EXPECT_EQ(sender_attrs->gold, 10);
}

TEST_F(MailHandlerTest, ClaimMailPreservesAttachmentItemInstanceState) {
  const auto sender = CreatePlayer(3011, 6011, "Sender", 0);
  const auto receiver = CreatePlayer(3012, 6012, "Receiver", 0);
  const entt::entity sender_item = CreateInventoryItem(sender.entity, 1, 93111, 2);

  auto* sender_item_component = registry_.try_get<ecs::ItemComponent>(sender_item);
  ASSERT_NE(sender_item_component, nullptr);
  sender_item_component->durability = 77;
  sender_item_component->max_durability = 120;
  sender_item_component->shape = 901;
  sender_item_component->looks = 4321;
  sender_item_component->std_mode = 25;
  sender_item_component->enhancement_level = 3;
  sender_item_component->luck = 2;
  sender_item_component->equip_slot = 4;
  sender_item_component->attack_bonus = 11;
  sender_item_component->defense_bonus = 9;
  sender_item_component->magic_attack_bonus = 7;
  sender_item_component->magic_defense_bonus = 5;
  sender_item_component->hp_bonus = 80;
  sender_item_component->mp_bonus = 40;
  sender_item_component->hit_rate_bonus = 6;
  sender_item_component->dodge_bonus = 8;
  sender_item_component->speed_bonus = 2;
  sender_item_component->lifesteal_percent = 4;
  sender_item_component->reflect_percent = 3;
  sender_item_component->elemental_damage = 13;
  sender_item_component->elemental_type = 2;

  HandlerContext send_ctx;
  send_ctx.client_id = sender.client_id;
  send_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kMailSendReq);
  send_ctx.entity = sender.entity;
  Dispatch(send_ctx,
           BuildSendReqPayload(receiver.character_id,
                               "State",
                               "Preserve",
                               0,
                               {{93111, 1}}));

  const auto send_rsp = FindLastResponse(
      sender.client_id, static_cast<uint16_t>(mir2::common::MsgId::kMailSendRsp));
  ASSERT_TRUE(send_rsp.has_value());
  flatbuffers::Verifier send_verifier(send_rsp->payload.data(), send_rsp->payload.size());
  ASSERT_TRUE(send_verifier.VerifyBuffer<mir2::proto::MailSendRsp>(nullptr));
  const auto* send_root = flatbuffers::GetRoot<mir2::proto::MailSendRsp>(send_rsp->payload.data());
  ASSERT_NE(send_root, nullptr);
  ASSERT_TRUE(send_root->success());
  const uint64_t mail_id = send_root->mail_id();

  HandlerContext claim_ctx;
  claim_ctx.client_id = receiver.client_id;
  claim_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kMailClaimReq);
  claim_ctx.entity = receiver.entity;
  Dispatch(claim_ctx, BuildClaimReqPayload(mail_id));

  const auto claim_rsp = FindLastResponse(
      receiver.client_id, static_cast<uint16_t>(mir2::common::MsgId::kMailClaimRsp));
  ASSERT_TRUE(claim_rsp.has_value());
  flatbuffers::Verifier claim_verifier(claim_rsp->payload.data(), claim_rsp->payload.size());
  ASSERT_TRUE(claim_verifier.VerifyBuffer<mir2::proto::MailClaimRsp>(nullptr));
  const auto* claim_root =
      flatbuffers::GetRoot<mir2::proto::MailClaimRsp>(claim_rsp->payload.data());
  ASSERT_NE(claim_root, nullptr);
  ASSERT_TRUE(claim_root->success());

  const auto* claimed_item = FindOwnedItem(receiver.entity, 93111);
  ASSERT_NE(claimed_item, nullptr);
  EXPECT_EQ(claimed_item->count, 1);
  EXPECT_EQ(claimed_item->durability, 77);
  EXPECT_EQ(claimed_item->max_durability, 120);
  EXPECT_EQ(claimed_item->shape, 901);
  EXPECT_EQ(claimed_item->looks, 4321);
  EXPECT_EQ(claimed_item->std_mode, 25);
  EXPECT_EQ(claimed_item->enhancement_level, 3);
  EXPECT_EQ(claimed_item->luck, 2);
  EXPECT_EQ(claimed_item->equip_slot, 4);
  EXPECT_EQ(claimed_item->attack_bonus, 11);
  EXPECT_EQ(claimed_item->defense_bonus, 9);
  EXPECT_EQ(claimed_item->magic_attack_bonus, 7);
  EXPECT_EQ(claimed_item->magic_defense_bonus, 5);
  EXPECT_EQ(claimed_item->hp_bonus, 80);
  EXPECT_EQ(claimed_item->mp_bonus, 40);
  EXPECT_EQ(claimed_item->hit_rate_bonus, 6);
  EXPECT_EQ(claimed_item->dodge_bonus, 8);
  EXPECT_EQ(claimed_item->speed_bonus, 2);
  EXPECT_EQ(claimed_item->lifesteal_percent, 4);
  EXPECT_EQ(claimed_item->reflect_percent, 3);
  EXPECT_EQ(claimed_item->elemental_damage, 13);
  EXPECT_EQ(claimed_item->elemental_type, 2);
}

}  // namespace
}  // namespace mir2::logic::test
