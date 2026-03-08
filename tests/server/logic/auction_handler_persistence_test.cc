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

#include "auction_generated.h"
#include "common/enums.h"
#include "config/config_manager.h"
#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "logic/coroutine_executor.h"
#include "logic/handlers/auction/auction_handler.h"
#include "logic/mock_response_sender.h"
#include "logic/services/client_registry.h"
#include "logic/services/session_role_store.h"
#include "storage_engine/backends/postgres/pg_connection_pool.h"

namespace mir2::logic::test {
namespace {

constexpr int kDbStatusActive = 0;
constexpr int kDbStatusCancelled = 2;
constexpr int kDbStatusExpired = 3;

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
  cfg.port = static_cast<uint16_t>(
      std::stoi(EnvOrDefault("MIR2_DB_PORT", "5432")));
  cfg.user = EnvOrDefault("MIR2_DB_USER", "mir2");
  cfg.password = EnvOrDefault("MIR2_DB_PASSWORD", "mir2_password");
  cfg.database = EnvOrDefault("MIR2_DB_NAME", "mir2_game");
  cfg.pool_size = 2;
  return cfg;
}

std::vector<uint8_t> BuildAuctionListReqPayload(uint32_t page,
                                                uint32_t page_size,
                                                bool seller_only) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateAuctionListReq(builder, page, page_size, seller_only);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildAuctionSellReqPayload(uint16_t slot,
                                                uint32_t item_id,
                                                uint32_t count,
                                                uint32_t unit_price,
                                                uint32_t duration_sec) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req =
      mir2::proto::CreateAuctionSellReq(builder, slot, item_id, count, unit_price, duration_sec);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildAuctionBuyReqPayload(uint64_t listing_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateAuctionBuyReq(builder, listing_id);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

class AuctionHandlerPersistenceTest : public ::testing::Test {
 protected:
  struct PersistedListingState {
    int status = -1;
    bool item_returned = false;
    uint64_t version = 0;
  };

  struct Player {
    uint64_t character_id = 0;
    uint64_t client_id = 0;
    entt::entity entity = entt::null;
  };

  void SetUp() override {
    const auto db_config = BuildDbConfigFromEnv();
    db_pool_ = std::make_shared<db::PgConnectionPool>();
    if (!db_pool_->Initialize(db_config)) {
      GTEST_SKIP() << "PostgreSQL unavailable for auction persistence tests";
    }

    executor_ = std::make_unique<CoroutineExecutor>(io_context_, 1);
    response_sender_ = std::make_unique<MockResponseSender>();
    handler_ = std::make_unique<AuctionHandler>(
        *response_sender_, client_registry_, registry_, &role_store_, db_pool_);
    CleanupAuctionTable();
  }

  void TearDown() override {
    CleanupAuctionTable();
  }

  void CleanupAuctionTable() {
    if (!db_pool_ || !db_pool_->IsReady()) {
      return;
    }

    try {
      auto conn = db_pool_->Acquire();
      if (!conn) {
        return;
      }
      db::PgConnectionGuard guard(*db_pool_, conn);
      pqxx::work txn(*conn);
      txn.exec("DELETE FROM auction_listings");
      txn.commit();
    } catch (...) {
    }
  }

  void RecreateHandler() {
    handler_.reset();
    response_sender_ = std::make_unique<MockResponseSender>();
    handler_ = std::make_unique<AuctionHandler>(
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
                      int gold) {
    const entt::entity entity = registry_.create();

    auto& identity = registry_.emplace<ecs::CharacterIdentityComponent>(entity);
    identity.id = static_cast<uint32_t>(character_id);
    identity.account_id = static_cast<ecs::AccountId>(character_id);
    identity.name = name;

    auto& state = registry_.emplace<ecs::CharacterStateComponent>(entity);
    state.is_online = true;

    auto& attributes = registry_.emplace<ecs::CharacterAttributesComponent>(entity);
    attributes.gold = gold;

    client_registry_.Track(client_id);
    role_store_.BindClientRole(client_id, character_id);

    return Player{character_id, client_id, entity};
  }

  void CreateInventoryItem(entt::entity owner,
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
  }

  int CountOwnedItem(entt::entity owner, uint32_t item_id) const {
    int total = 0;
    auto view = registry_.view<ecs::ItemComponent, ecs::InventoryOwnerComponent>();
    for (const entt::entity entity : view) {
      const auto& item = view.get<ecs::ItemComponent>(entity);
      const auto& owner_comp = view.get<ecs::InventoryOwnerComponent>(entity);
      if (owner_comp.owner == owner && item.item_id == item_id && owner_comp.slot_index >= 0) {
        total += item.count;
      }
    }
    return total;
  }

  uint64_t InsertPersistedListing(uint64_t seller_character_id,
                                  uint32_t item_id,
                                  uint32_t item_count,
                                  uint32_t unit_price,
                                  int status,
                                  bool item_returned,
                                  int64_t created_offset_ms,
                                  int64_t expires_offset_ms) {
    auto conn = db_pool_->Acquire();
    EXPECT_NE(conn, nullptr);
    db::PgConnectionGuard guard(*db_pool_, conn);
    pqxx::work txn(*conn);
    const pqxx::result rows = txn.exec(
        "INSERT INTO auction_listings "
        "(seller_character_id, buyer_character_id, item_id, item_count, unit_price, "
        "status, item_returned, version, created_at, expires_at, cancelled_at, updated_at) "
        "VALUES ($1, 0, $2, $3, $4, $5::SMALLINT, $6, 0, "
        "NOW() + ($7::BIGINT * INTERVAL '1 millisecond'), "
        "NOW() + ($8::BIGINT * INTERVAL '1 millisecond'), "
        "CASE WHEN $5::SMALLINT IN (2, 3) THEN NOW() ELSE NULL END, NOW()) "
        "RETURNING listing_id",
        pqxx::params{seller_character_id,
                     item_id,
                     item_count,
                     unit_price,
                     status,
                     item_returned,
                     created_offset_ms,
                     expires_offset_ms});
    txn.commit();
    if (rows.empty()) {
      return 0;
    }
    return rows[0]["listing_id"].as<uint64_t>(0);
  }

  std::optional<PersistedListingState> ReadPersistedListingState(uint64_t listing_id) {
    auto conn = db_pool_->Acquire();
    if (!conn) {
      return std::nullopt;
    }
    db::PgConnectionGuard guard(*db_pool_, conn);
    pqxx::read_transaction txn(*conn);
    const pqxx::result rows = txn.exec(
        "SELECT status, item_returned, version FROM auction_listings WHERE listing_id = $1",
        pqxx::params{listing_id});
    txn.commit();
    if (rows.empty()) {
      return std::nullopt;
    }
    PersistedListingState state;
    state.status = rows[0]["status"].as<int>(-1);
    state.item_returned = rows[0]["item_returned"].as<bool>(false);
    state.version = rows[0]["version"].as<uint64_t>(0);
    return state;
  }

  std::optional<CapturedResponse> FindResponse(uint64_t client_id,
                                               uint16_t msg_id) const {
    const auto responses = response_sender_->GetCapturedResponses();
    const auto it = std::find_if(
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
  std::shared_ptr<db::PgConnectionPool> db_pool_;
  std::unique_ptr<CoroutineExecutor> executor_;
  std::unique_ptr<MockResponseSender> response_sender_;
  ClientRegistry client_registry_;
  RoleStore role_store_;
  std::unique_ptr<AuctionHandler> handler_;
};

TEST_F(AuctionHandlerPersistenceTest, ListingPersistsAcrossHandlerRecreation) {
  const auto seller = CreatePlayer(4101, 8101, "Seller", 1000);
  const auto buyer = CreatePlayer(4102, 8102, "Buyer", 1000);
  CreateInventoryItem(seller.entity, 2, 88001, 2);

  HandlerContext sell_ctx;
  sell_ctx.client_id = seller.client_id;
  sell_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kAuctionSellReq);
  sell_ctx.entity = seller.entity;
  Dispatch(sell_ctx, BuildAuctionSellReqPayload(2, 88001, 1, 120, 3600));

  const auto sell_rsp = FindResponse(
      seller.client_id, static_cast<uint16_t>(mir2::common::MsgId::kAuctionSellRsp));
  ASSERT_TRUE(sell_rsp.has_value());
  flatbuffers::Verifier sell_verifier(sell_rsp->payload.data(), sell_rsp->payload.size());
  ASSERT_TRUE(sell_verifier.VerifyBuffer<mir2::proto::AuctionSellRsp>(nullptr));
  const auto* sell_root = flatbuffers::GetRoot<mir2::proto::AuctionSellRsp>(sell_rsp->payload.data());
  ASSERT_NE(sell_root, nullptr);
  ASSERT_TRUE(sell_root->success());
  const uint64_t listing_id = sell_root->listing_id();
  ASSERT_NE(listing_id, 0u);

  RecreateHandler();

  HandlerContext list_ctx;
  list_ctx.client_id = buyer.client_id;
  list_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kAuctionListReq);
  list_ctx.entity = buyer.entity;
  Dispatch(list_ctx, BuildAuctionListReqPayload(1, 20, false));

  const auto list_rsp = FindResponse(
      buyer.client_id, static_cast<uint16_t>(mir2::common::MsgId::kAuctionListRsp));
  ASSERT_TRUE(list_rsp.has_value());
  flatbuffers::Verifier list_verifier(list_rsp->payload.data(), list_rsp->payload.size());
  ASSERT_TRUE(list_verifier.VerifyBuffer<mir2::proto::AuctionListRsp>(nullptr));
  const auto* list_root = flatbuffers::GetRoot<mir2::proto::AuctionListRsp>(list_rsp->payload.data());
  ASSERT_NE(list_root, nullptr);
  ASSERT_TRUE(list_root->success());
  ASSERT_NE(list_root->listings(), nullptr);
  ASSERT_EQ(list_root->listings()->size(), 1u);
  EXPECT_EQ(list_root->listings()->Get(0)->listing_id(), listing_id);
}

TEST_F(AuctionHandlerPersistenceTest, DoubleBuyRejectsSecondBuyer) {
  const auto seller = CreatePlayer(4201, 8201, "Seller", 1000);
  const auto buyer_a = CreatePlayer(4202, 8202, "BuyerA", 5000);
  const auto buyer_b = CreatePlayer(4203, 8203, "BuyerB", 5000);
  CreateInventoryItem(seller.entity, 4, 99001, 1);

  HandlerContext sell_ctx;
  sell_ctx.client_id = seller.client_id;
  sell_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kAuctionSellReq);
  sell_ctx.entity = seller.entity;
  Dispatch(sell_ctx, BuildAuctionSellReqPayload(4, 99001, 1, 333, 3600));

  const auto sell_rsp = FindResponse(
      seller.client_id, static_cast<uint16_t>(mir2::common::MsgId::kAuctionSellRsp));
  ASSERT_TRUE(sell_rsp.has_value());
  flatbuffers::Verifier sell_verifier(sell_rsp->payload.data(), sell_rsp->payload.size());
  ASSERT_TRUE(sell_verifier.VerifyBuffer<mir2::proto::AuctionSellRsp>(nullptr));
  const auto* sell_root = flatbuffers::GetRoot<mir2::proto::AuctionSellRsp>(sell_rsp->payload.data());
  ASSERT_NE(sell_root, nullptr);
  ASSERT_TRUE(sell_root->success());
  const uint64_t listing_id = sell_root->listing_id();

  response_sender_->Clear();

  HandlerContext buy_a_ctx;
  buy_a_ctx.client_id = buyer_a.client_id;
  buy_a_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kAuctionBuyReq);
  buy_a_ctx.entity = buyer_a.entity;
  Dispatch(buy_a_ctx, BuildAuctionBuyReqPayload(listing_id));

  const auto buy_a_rsp = FindResponse(
      buyer_a.client_id, static_cast<uint16_t>(mir2::common::MsgId::kAuctionBuyRsp));
  ASSERT_TRUE(buy_a_rsp.has_value());
  flatbuffers::Verifier buy_a_verifier(buy_a_rsp->payload.data(), buy_a_rsp->payload.size());
  ASSERT_TRUE(buy_a_verifier.VerifyBuffer<mir2::proto::AuctionBuyRsp>(nullptr));
  const auto* buy_a_root = flatbuffers::GetRoot<mir2::proto::AuctionBuyRsp>(buy_a_rsp->payload.data());
  ASSERT_NE(buy_a_root, nullptr);
  ASSERT_TRUE(buy_a_root->success());

  response_sender_->Clear();

  HandlerContext buy_b_ctx;
  buy_b_ctx.client_id = buyer_b.client_id;
  buy_b_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kAuctionBuyReq);
  buy_b_ctx.entity = buyer_b.entity;
  Dispatch(buy_b_ctx, BuildAuctionBuyReqPayload(listing_id));

  const auto buy_b_rsp = FindResponse(
      buyer_b.client_id, static_cast<uint16_t>(mir2::common::MsgId::kAuctionBuyRsp));
  ASSERT_TRUE(buy_b_rsp.has_value());
  flatbuffers::Verifier buy_b_verifier(buy_b_rsp->payload.data(), buy_b_rsp->payload.size());
  ASSERT_TRUE(buy_b_verifier.VerifyBuffer<mir2::proto::AuctionBuyRsp>(nullptr));
  const auto* buy_b_root = flatbuffers::GetRoot<mir2::proto::AuctionBuyRsp>(buy_b_rsp->payload.data());
  ASSERT_NE(buy_b_root, nullptr);
  EXPECT_FALSE(buy_b_root->success());
  EXPECT_TRUE(
      buy_b_root->error_code() == static_cast<int>(mir2::proto::ErrorCode::ERR_AUCTION_ALREADY_SOLD) ||
      buy_b_root->error_code() == static_cast<int>(mir2::proto::ErrorCode::ERR_AUCTION_NOT_FOUND));
}

TEST_F(AuctionHandlerPersistenceTest, ExpiredListingSweepReturnsItemToOnlineSeller) {
  const auto seller = CreatePlayer(4301, 8301, "SweepSeller", 1000);
  const auto viewer = CreatePlayer(4302, 8302, "SweepViewer", 1000);
  ASSERT_EQ(CountOwnedItem(seller.entity, 70011), 0);

  const uint64_t listing_id = InsertPersistedListing(seller.character_id,
                                                     70011,
                                                     2,
                                                     88,
                                                     kDbStatusActive,
                                                     false,
                                                     -7 * 60 * 1000,
                                                     -1000);
  ASSERT_NE(listing_id, 0u);

  HandlerContext list_ctx;
  list_ctx.client_id = viewer.client_id;
  list_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kAuctionListReq);
  list_ctx.entity = viewer.entity;
  Dispatch(list_ctx, BuildAuctionListReqPayload(1, 20, false));

  EXPECT_EQ(CountOwnedItem(seller.entity, 70011), 2);
  const auto persisted = ReadPersistedListingState(listing_id);
  ASSERT_TRUE(persisted.has_value());
  EXPECT_EQ(persisted->status, kDbStatusExpired);
  EXPECT_TRUE(persisted->item_returned);
}

TEST_F(AuctionHandlerPersistenceTest, PendingCancelledReturnRecoveredOnSellerList) {
  const auto seller = CreatePlayer(4401, 8401, "RecoverSeller", 1000);
  ASSERT_EQ(CountOwnedItem(seller.entity, 80022), 0);

  const uint64_t listing_id = InsertPersistedListing(seller.character_id,
                                                     80022,
                                                     3,
                                                     99,
                                                     kDbStatusCancelled,
                                                     false,
                                                     -10 * 60 * 1000,
                                                     -5 * 60 * 1000);
  ASSERT_NE(listing_id, 0u);

  HandlerContext list_ctx;
  list_ctx.client_id = seller.client_id;
  list_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kAuctionListReq);
  list_ctx.entity = seller.entity;
  Dispatch(list_ctx, BuildAuctionListReqPayload(1, 20, true));

  EXPECT_EQ(CountOwnedItem(seller.entity, 80022), 3);
  const auto persisted = ReadPersistedListingState(listing_id);
  ASSERT_TRUE(persisted.has_value());
  EXPECT_EQ(persisted->status, kDbStatusCancelled);
  EXPECT_TRUE(persisted->item_returned);
}

}  // namespace
}  // namespace mir2::logic::test
