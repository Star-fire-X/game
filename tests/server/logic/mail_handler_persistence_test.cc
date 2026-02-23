#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <asio/io_context.hpp>
#include <entt/entt.hpp>
#include <flatbuffers/flatbuffers.h>
#include <pqxx/pqxx>

#include "common/enums.h"
#include "config/config_manager.h"
#include "ecs/components/character_components.h"
#include "logic/coroutine_executor.h"
#include "logic/handlers/mail/mail_handler.h"
#include "logic/mock_response_sender.h"
#include "logic/services/client_registry.h"
#include "logic/services/session_role_store.h"
#include "mail_generated.h"
#include "storage_engine/backends/postgres/pg_connection_pool.h"

namespace mir2::logic::test {
namespace {

std::string EnvOrDefault(const char* key, const char* default_value) {
  const char* value = std::getenv(key);
  if (value != nullptr && value[0] != '\0') {
    return value;
  }
  return default_value;
}

config::DatabaseConfig BuildDbConfigFromEnv() {
  config::DatabaseConfig cfg;
  cfg.host = EnvOrDefault("MIR2_DB_HOST", "127.0.0.1");
  cfg.port = static_cast<uint16_t>(std::stoi(EnvOrDefault("MIR2_DB_PORT", "5432")));
  cfg.user = EnvOrDefault("MIR2_DB_USER", "mir2");
  cfg.password = EnvOrDefault("MIR2_DB_PASSWORD", "mir2_password");
  cfg.database = EnvOrDefault("MIR2_DB_NAME", "mir2_game");
  cfg.pool_size = 2;
  return cfg;
}

std::vector<uint8_t> BuildSendReqPayload(uint64_t target_character_id,
                                         const std::string& subject,
                                         const std::string& content,
                                         uint32_t gold = 0) {
  flatbuffers::FlatBufferBuilder builder;
  const auto subject_offset = builder.CreateString(subject);
  const auto content_offset = builder.CreateString(content);
  const auto req = mir2::proto::CreateMailSendReq(
      builder, target_character_id, subject_offset, content_offset, gold, 0, 0);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildListReqPayload() {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateMailListReq(builder);
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

class MailHandlerPersistenceTest : public ::testing::Test {
 protected:
  struct Player {
    uint64_t character_id = 0;
    uint64_t client_id = 0;
    entt::entity entity = entt::null;
  };

  void SetUp() override {
    const auto db_config = BuildDbConfigFromEnv();
    db_pool_ = std::make_shared<db::PgConnectionPool>();
    if (!db_pool_->Initialize(db_config)) {
      GTEST_SKIP() << "PostgreSQL unavailable for mail persistence tests";
    }

    executor_ = std::make_unique<CoroutineExecutor>(io_context_, 1);
    response_sender_ = std::make_unique<MockResponseSender>();
    handler_ = std::make_unique<MailHandler>(
        *response_sender_, client_registry_, registry_, &role_store_, db_pool_);
    CleanupMailTable();
  }

  void TearDown() override {
    CleanupMailTable();
  }

  void CleanupMailTable() {
    if (!db_pool_ || !db_pool_->IsReady()) {
      return;
    }
    try {
      const auto conn = db_pool_->Acquire();
      if (!conn) {
        return;
      }
      db::PgConnectionGuard guard(*db_pool_, conn);
      pqxx::work txn(*conn);
      txn.exec(
          "CREATE TABLE IF NOT EXISTS mails ("
          "id BIGSERIAL PRIMARY KEY, "
          "from_id BIGINT NOT NULL, "
          "to_id BIGINT NOT NULL, "
          "subject VARCHAR(128) NOT NULL, "
          "content TEXT NOT NULL, "
          "gold INTEGER NOT NULL DEFAULT 0, "
          "items JSONB NOT NULL DEFAULT '[]'::jsonb, "
          "is_read BOOLEAN NOT NULL DEFAULT FALSE, "
          "claimed BOOLEAN NOT NULL DEFAULT FALSE, "
          "send_time TIMESTAMPTZ NOT NULL DEFAULT NOW(), "
          "expire_time TIMESTAMPTZ NOT NULL)");
      txn.exec("DELETE FROM mails");
      txn.commit();
    } catch (...) {
    }
  }

  void RecreateHandler() {
    handler_.reset();
    response_sender_ = std::make_unique<MockResponseSender>();
    handler_ = std::make_unique<MailHandler>(
        *response_sender_, client_registry_, registry_, &role_store_, db_pool_);
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
  std::shared_ptr<db::PgConnectionPool> db_pool_;
  std::unique_ptr<CoroutineExecutor> executor_;
  std::unique_ptr<MockResponseSender> response_sender_;
  ClientRegistry client_registry_;
  RoleStore role_store_;
  std::unique_ptr<MailHandler> handler_;
};

TEST_F(MailHandlerPersistenceTest, MailPersistsAcrossHandlerRecreationAndCanBeClaimedDeleted) {
  const auto sender = CreatePlayer(5101, 9101, "Sender");
  const auto receiver = CreatePlayer(5102, 9102, "Receiver", 100);

  HandlerContext send_ctx;
  send_ctx.client_id = sender.client_id;
  send_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kMailSendReq);
  send_ctx.entity = sender.entity;
  Dispatch(send_ctx, BuildSendReqPayload(receiver.character_id, "Persist", "From DB", 250));

  const auto send_rsp = FindLastResponse(
      sender.client_id, static_cast<uint16_t>(mir2::common::MsgId::kMailSendRsp));
  ASSERT_TRUE(send_rsp.has_value());
  flatbuffers::Verifier send_verifier(send_rsp->payload.data(), send_rsp->payload.size());
  ASSERT_TRUE(send_verifier.VerifyBuffer<mir2::proto::MailSendRsp>(nullptr));
  const auto* send_root = flatbuffers::GetRoot<mir2::proto::MailSendRsp>(send_rsp->payload.data());
  ASSERT_NE(send_root, nullptr);
  ASSERT_TRUE(send_root->success());
  const uint64_t mail_id = send_root->mail_id();
  ASSERT_NE(mail_id, 0u);

  RecreateHandler();

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
  EXPECT_EQ(delete_root->mail_id(), mail_id);
}

}  // namespace
}  // namespace mir2::logic::test
