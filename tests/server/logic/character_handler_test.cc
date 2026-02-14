/**
 * @file character_handler_test.cc
 * @brief Tests for logic CharacterHandler coroutine behavior.
 */

#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include <flatbuffers/flatbuffers.h>

#include <string>
#include <unordered_set>
#include <vector>

#include "common/enums.h"
#include "ecs/components/character_components.h"
#include "ecs/character_entity_manager.h"
#include "game_generated.h"
#include "logic/services/client_registry.h"
#include "logic/coroutine_executor.h"
#include "logic/handlers/character/character_handler.h"
#include "login_generated.h"
#include "logic/mock_response_sender.h"
#include "logic/services/session_role_store.h"
#include "system_generated.h"

namespace mir2::logic::test {
namespace {

std::vector<uint8_t> BuildRoleListReq(uint64_t account_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateRoleListReq(builder, account_id, 0);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildCreateRoleReq(
    const std::string& name,
    mir2::proto::Profession profession = mir2::proto::Profession::WARRIOR,
    mir2::proto::Gender gender = mir2::proto::Gender::MALE) {
  flatbuffers::FlatBufferBuilder builder;
  const auto name_offset = builder.CreateString(name);
  const auto req = mir2::proto::CreateCreateRoleReq(builder, name_offset, profession, gender);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildSelectRoleReq(uint64_t player_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateSelectRoleReq(builder, player_id);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::unordered_set<std::string> CollectRoleNames(const mir2::proto::RoleListRsp& rsp) {
  std::unordered_set<std::string> names;
  if (!rsp.roles()) {
    return names;
  }
  for (const auto* role : *rsp.roles()) {
    if (role && role->name()) {
      names.insert(role->name()->str());
    }
  }
  return names;
}

}  // namespace

class CharacterHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    executor_ = std::make_unique<CoroutineExecutor>(io_context_, 1);
    response_sender_ = std::make_unique<MockResponseSender>();
    entity_manager_ = std::make_unique<ecs::CharacterEntityManager>(ecs_registry_);
    role_store_ = std::make_unique<RoleStore>();
    handler_ = std::make_unique<CharacterHandler>(*response_sender_,
                                                   *entity_manager_,
                                                   *role_store_,
                                                   client_registry_);
  }

  void RunIoContext() {
    io_context_.run();
    io_context_.restart();
  }

  asio::io_context io_context_;
  entt::registry ecs_registry_;
  std::unique_ptr<CoroutineExecutor> executor_;
  std::unique_ptr<MockResponseSender> response_sender_;
  std::unique_ptr<ecs::CharacterEntityManager> entity_manager_;
  std::unique_ptr<RoleStore> role_store_;
  std::unique_ptr<CharacterHandler> handler_;
  ClientRegistry client_registry_;
};

// 角色列表请求成功返回角色列表与 OK。
TEST_F(CharacterHandlerTest, HandleRoleListSuccess) {
  const uint64_t account_id = 42;
  RoleRecord record;
  ASSERT_EQ(role_store_->CreateRole(account_id, "Alice", 1, 0, &record),
            mir2::common::ErrorCode::kOk);
  ASSERT_EQ(role_store_->CreateRole(account_id, "Bob", 1, 0, &record),
            mir2::common::ErrorCode::kOk);

  HandlerContext context;
  context.client_id = 1001;
  context.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kRoleListReq);
  role_store_->BindClientAccount(context.client_id, account_id);
  const auto payload = BuildRoleListReq(account_id);

  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].client_id, context.client_id);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kRoleListRsp));

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::RoleListRsp>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::RoleListRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_EQ(rsp->code(), mir2::proto::ErrorCode::ERR_OK);

  const auto names = CollectRoleNames(*rsp);
  EXPECT_EQ(names.size(), 2u);
  EXPECT_TRUE(names.count("Alice") > 0);
  EXPECT_TRUE(names.count("Bob") > 0);

  const auto bound_account = role_store_->GetAccountId(context.client_id);
  ASSERT_TRUE(bound_account.has_value());
  EXPECT_EQ(*bound_account, account_id);
}

// account_id 为空时返回账户不存在错误并保持未绑定。
TEST_F(CharacterHandlerTest, HandleRoleListEmptyAccount) {
  HandlerContext context;
  context.client_id = 2002;
  context.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kRoleListReq);
  const auto payload = BuildRoleListReq(0);

  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kRoleListRsp));

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::RoleListRsp>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::RoleListRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_EQ(rsp->code(), mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND);
  EXPECT_FALSE(role_store_->GetAccountId(context.client_id).has_value());
}

// 已认证账号与请求账号不匹配时应拒绝并保持原绑定。
TEST_F(CharacterHandlerTest, HandleRoleListRejectsMismatchedAccount) {
  const uint64_t bound_account_id = 300;
  const uint64_t requested_account_id = 301;

  RoleRecord record;
  ASSERT_EQ(role_store_->CreateRole(bound_account_id, "BoundRole", 1, 0, &record),
            mir2::common::ErrorCode::kOk);
  ASSERT_EQ(role_store_->CreateRole(requested_account_id, "OtherRole", 1, 0, &record),
            mir2::common::ErrorCode::kOk);

  HandlerContext context;
  context.client_id = 2003;
  context.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kRoleListReq);
  role_store_->BindClientAccount(context.client_id, bound_account_id);

  const auto payload = BuildRoleListReq(requested_account_id);
  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::RoleListRsp>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::RoleListRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_EQ(rsp->code(), mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND);

  const auto bound_account = role_store_->GetAccountId(context.client_id);
  ASSERT_TRUE(bound_account.has_value());
  EXPECT_EQ(*bound_account, bound_account_id);
}

// 请求 account_id 为 0 时，使用已认证绑定账号查询角色列表。
TEST_F(CharacterHandlerTest, HandleRoleListUsesAuthenticatedAccountWhenRequestIsZero) {
  const uint64_t account_id = 444;
  RoleRecord record;
  ASSERT_EQ(role_store_->CreateRole(account_id, "BoundOnlyRole", 1, 0, &record),
            mir2::common::ErrorCode::kOk);

  HandlerContext context;
  context.client_id = 2004;
  context.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kRoleListReq);
  role_store_->BindClientAccount(context.client_id, account_id);

  const auto payload = BuildRoleListReq(0);
  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::RoleListRsp>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::RoleListRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_EQ(rsp->code(), mir2::proto::ErrorCode::ERR_OK);

  const auto names = CollectRoleNames(*rsp);
  EXPECT_EQ(names.size(), 1u);
  EXPECT_TRUE(names.count("BoundOnlyRole") > 0);
}

// 成功创建角色后返回 CreateRoleRsp 与非零 player_id。
TEST_F(CharacterHandlerTest, HandleCreateRoleSuccess) {
  const uint64_t account_id = 77;
  HandlerContext context;
  context.client_id = 3003;
  context.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleReq);
  role_store_->BindClientAccount(context.client_id, account_id);

  const auto payload = BuildCreateRoleReq("Hero");
  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleRsp));

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::CreateRoleRsp>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::CreateRoleRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_EQ(rsp->code(), mir2::proto::ErrorCode::ERR_OK);
  EXPECT_GT(rsp->player_id(), 0u);

  const auto roles = role_store_->GetRoles(account_id);
  ASSERT_EQ(roles.size(), 1u);
  EXPECT_EQ(roles[0].name, "Hero");
}

TEST_F(CharacterHandlerTest, HandleCreateRolePreservesRequestedProfessionAndGender) {
  const uint64_t account_id = 78;
  HandlerContext context;
  context.client_id = 3004;
  context.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleReq);
  role_store_->BindClientAccount(context.client_id, account_id);

  const auto payload = BuildCreateRoleReq(
      "MageGirl", mir2::proto::Profession::WIZARD, mir2::proto::Gender::FEMALE);
  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  ASSERT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleRsp));

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::CreateRoleRsp>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::CreateRoleRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  ASSERT_EQ(rsp->code(), mir2::proto::ErrorCode::ERR_OK);
  ASSERT_GT(rsp->player_id(), 0u);

  const auto entity_opt = entity_manager_->TryGet(static_cast<uint32_t>(rsp->player_id()));
  ASSERT_TRUE(entity_opt.has_value());
  ASSERT_TRUE(ecs_registry_.valid(*entity_opt));

  const auto& identity = ecs_registry_.get<mir2::ecs::CharacterIdentityComponent>(*entity_opt);
  EXPECT_EQ(identity.name, "MageGirl");
  EXPECT_EQ(identity.char_class, mir2::common::CharacterClass::MAGE);
  EXPECT_EQ(identity.gender, mir2::common::Gender::FEMALE);
}

// 重名创建应返回非 OK（当前实现为 ERR_UNKNOWN）并保持 player_id=0。
TEST_F(CharacterHandlerTest, HandleCreateRoleDuplicateName) {
  const uint64_t account_id = 88;
  HandlerContext context;
  context.client_id = 4004;
  context.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleReq);
  role_store_->BindClientAccount(context.client_id, account_id);

  RoleRecord record;
  ASSERT_EQ(role_store_->CreateRole(account_id, "Hero", 1, 0, &record),
            mir2::common::ErrorCode::kOk);

  const auto payload = BuildCreateRoleReq("Hero");
  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::CreateRoleRsp>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::CreateRoleRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_NE(rsp->code(), mir2::proto::ErrorCode::ERR_OK);
  EXPECT_EQ(rsp->player_id(), 0u);

  const auto roles = role_store_->GetRoles(account_id);
  EXPECT_EQ(roles.size(), 1u);
}

// 选择角色成功应返回 SelectRoleRsp + EnterGameRsp 并绑定角色。
TEST_F(CharacterHandlerTest, HandleSelectRoleSuccess) {
  const uint64_t account_id = 99;
  HandlerContext context;
  context.client_id = 5005;
  context.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleReq);
  role_store_->BindClientAccount(context.client_id, account_id);

  RoleRecord record;
  ASSERT_EQ(role_store_->CreateRole(account_id, "Knight", 1, 0, &record),
            mir2::common::ErrorCode::kOk);

  const auto payload = BuildSelectRoleReq(record.player_id);
  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 2u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleRsp));
  EXPECT_EQ(responses[1].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kEnterGameRsp));

  flatbuffers::Verifier select_verifier(responses[0].payload.data(),
                                        responses[0].payload.size());
  ASSERT_TRUE(select_verifier.VerifyBuffer<mir2::proto::SelectRoleRsp>(nullptr));
  const auto* select_rsp =
      flatbuffers::GetRoot<mir2::proto::SelectRoleRsp>(responses[0].payload.data());
  ASSERT_NE(select_rsp, nullptr);
  EXPECT_EQ(select_rsp->code(), mir2::proto::ErrorCode::ERR_OK);
  EXPECT_EQ(select_rsp->player_id(), record.player_id);

  flatbuffers::Verifier enter_verifier(responses[1].payload.data(),
                                       responses[1].payload.size());
  ASSERT_TRUE(enter_verifier.VerifyBuffer<mir2::proto::EnterGameRsp>(nullptr));
  const auto* enter_rsp =
      flatbuffers::GetRoot<mir2::proto::EnterGameRsp>(responses[1].payload.data());
  ASSERT_NE(enter_rsp, nullptr);
  EXPECT_EQ(enter_rsp->code(), mir2::proto::ErrorCode::ERR_OK);
  ASSERT_NE(enter_rsp->player(), nullptr);
  EXPECT_EQ(enter_rsp->player()->id(), record.player_id);
  EXPECT_EQ(enter_rsp->player()->name()->str(), "Knight");

  const auto bound_role = role_store_->GetRoleId(context.client_id);
  ASSERT_TRUE(bound_role.has_value());
  EXPECT_EQ(*bound_role, record.player_id);

  EXPECT_TRUE(entity_manager_->TryGet(static_cast<uint32_t>(record.player_id)).has_value());
}

TEST_F(CharacterHandlerTest, HandleSelectRoleDuplicateLoginKicksOldClient) {
  const uint64_t account_id = 109;
  const uint64_t old_client_id = 5008;
  const uint64_t new_client_id = 5009;

  RoleRecord record;
  ASSERT_EQ(role_store_->CreateRole(account_id, "DupLoginRole", 1, 0, &record),
            mir2::common::ErrorCode::kOk);

  role_store_->BindClientAccount(old_client_id, account_id);
  role_store_->BindClientRole(old_client_id, record.player_id);
  client_registry_.Track(old_client_id);

  HandlerContext context;
  context.client_id = new_client_id;
  context.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleReq);
  role_store_->BindClientAccount(new_client_id, account_id);

  const auto payload = BuildSelectRoleReq(record.player_id);
  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 3u);
  EXPECT_EQ(responses[0].client_id, new_client_id);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleRsp));
  EXPECT_EQ(responses[1].client_id, old_client_id);
  EXPECT_EQ(responses[1].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kKick));
  EXPECT_EQ(responses[2].client_id, new_client_id);
  EXPECT_EQ(responses[2].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kEnterGameRsp));

  flatbuffers::Verifier kick_verifier(responses[1].payload.data(),
                                      responses[1].payload.size());
  ASSERT_TRUE(kick_verifier.VerifyBuffer<mir2::proto::Kick>(nullptr));
  const auto* kick_rsp =
      flatbuffers::GetRoot<mir2::proto::Kick>(responses[1].payload.data());
  ASSERT_NE(kick_rsp, nullptr);
  EXPECT_EQ(kick_rsp->reason(), mir2::proto::ErrorCode::ERR_KICK_DUPLICATE_LOGIN);

  EXPECT_FALSE(client_registry_.Contains(old_client_id));
  EXPECT_FALSE(role_store_->GetAccountId(old_client_id).has_value());
  EXPECT_FALSE(role_store_->GetRoleId(old_client_id).has_value());
  ASSERT_TRUE(role_store_->GetRoleId(new_client_id).has_value());
  EXPECT_EQ(*role_store_->GetRoleId(new_client_id), record.player_id);
}

// 未绑定账号时选择角色应失败且不绑定角色。
TEST_F(CharacterHandlerTest, HandleSelectRoleFailsWithoutAuthenticatedAccount) {
  const uint64_t account_id = 120;
  RoleRecord record;
  ASSERT_EQ(role_store_->CreateRole(account_id, "NoAuthRole", 1, 0, &record),
            mir2::common::ErrorCode::kOk);

  HandlerContext context;
  context.client_id = 5006;
  context.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleReq);

  const auto payload = BuildSelectRoleReq(record.player_id);
  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 2u);

  flatbuffers::Verifier select_verifier(responses[0].payload.data(),
                                        responses[0].payload.size());
  ASSERT_TRUE(select_verifier.VerifyBuffer<mir2::proto::SelectRoleRsp>(nullptr));
  const auto* select_rsp =
      flatbuffers::GetRoot<mir2::proto::SelectRoleRsp>(responses[0].payload.data());
  ASSERT_NE(select_rsp, nullptr);
  EXPECT_EQ(select_rsp->code(), mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND);

  flatbuffers::Verifier enter_verifier(responses[1].payload.data(),
                                       responses[1].payload.size());
  ASSERT_TRUE(enter_verifier.VerifyBuffer<mir2::proto::EnterGameRsp>(nullptr));
  const auto* enter_rsp =
      flatbuffers::GetRoot<mir2::proto::EnterGameRsp>(responses[1].payload.data());
  ASSERT_NE(enter_rsp, nullptr);
  EXPECT_EQ(enter_rsp->code(), mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND);
  EXPECT_EQ(enter_rsp->player(), nullptr);

  EXPECT_FALSE(role_store_->GetRoleId(context.client_id).has_value());
}

// 绑定账号与目标角色不一致时应拒绝登录。
TEST_F(CharacterHandlerTest, HandleSelectRoleRejectsCrossAccountRole) {
  const uint64_t bound_account_id = 130;
  const uint64_t other_account_id = 131;

  RoleRecord bound_record;
  ASSERT_EQ(role_store_->CreateRole(bound_account_id, "BoundRole", 1, 0, &bound_record),
            mir2::common::ErrorCode::kOk);
  RoleRecord other_record;
  ASSERT_EQ(role_store_->CreateRole(other_account_id, "OtherAccountRole", 1, 0, &other_record),
            mir2::common::ErrorCode::kOk);

  HandlerContext context;
  context.client_id = 5007;
  context.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleReq);
  role_store_->BindClientAccount(context.client_id, bound_account_id);

  const auto payload = BuildSelectRoleReq(other_record.player_id);
  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 2u);

  flatbuffers::Verifier select_verifier(responses[0].payload.data(),
                                        responses[0].payload.size());
  ASSERT_TRUE(select_verifier.VerifyBuffer<mir2::proto::SelectRoleRsp>(nullptr));
  const auto* select_rsp =
      flatbuffers::GetRoot<mir2::proto::SelectRoleRsp>(responses[0].payload.data());
  ASSERT_NE(select_rsp, nullptr);
  EXPECT_EQ(select_rsp->code(), mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND);

  flatbuffers::Verifier enter_verifier(responses[1].payload.data(),
                                       responses[1].payload.size());
  ASSERT_TRUE(enter_verifier.VerifyBuffer<mir2::proto::EnterGameRsp>(nullptr));
  const auto* enter_rsp =
      flatbuffers::GetRoot<mir2::proto::EnterGameRsp>(responses[1].payload.data());
  ASSERT_NE(enter_rsp, nullptr);
  EXPECT_EQ(enter_rsp->code(), mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND);
  EXPECT_EQ(enter_rsp->player(), nullptr);

  EXPECT_FALSE(role_store_->GetRoleId(context.client_id).has_value());
}

// 登出请求应解绑客户端并不发送响应。
TEST_F(CharacterHandlerTest, HandleLogout) {
  const uint64_t account_id = 101;
  const uint64_t player_id = 1001;

  HandlerContext context;
  context.client_id = 6006;
  context.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kLogout);
  role_store_->BindClientAccount(context.client_id, account_id);
  role_store_->BindClientRole(context.client_id, player_id);
  client_registry_.Track(context.client_id);
  entity_manager_->GetOrCreate(static_cast<uint32_t>(player_id));

  executor_->Spawn(handler_->HandleMessage(context, nullptr, 0));
  RunIoContext();

  EXPECT_EQ(response_sender_->ResponseCount(), 0u);
  EXPECT_FALSE(client_registry_.Contains(context.client_id));
  EXPECT_FALSE(role_store_->GetAccountId(context.client_id).has_value());
  EXPECT_FALSE(role_store_->GetRoleId(context.client_id).has_value());
}

}  // namespace mir2::logic::test
