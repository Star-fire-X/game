#include "logic/handlers/auction/auction_handler.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <pqxx/pqxx>

#include "auction_generated.h"
#include "common/enums.h"
#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "log/logger.h"
#include "logic/handlers/handler_error_utils.h"
#include "logic/response_sender.h"
#include "logic/services/client_registry.h"
#include "logic/services/session_role_store.h"
#include "storage_engine/backends/postgres/pg_connection_pool.h"

namespace mir2::logic {

namespace {

constexpr uint32_t kDefaultPageSize = 20;
constexpr uint32_t kMaxPageSize = 100;
constexpr uint16_t kMaxInventorySlots = 256;
constexpr uint32_t kDefaultDurationSec = 24 * 60 * 60;
constexpr uint32_t kMinDurationSec = 60;
constexpr uint32_t kMaxDurationSec = 7 * 24 * 60 * 60;
constexpr int kDbStatusActive = 0;
constexpr int kDbStatusSold = 1;
constexpr int kDbStatusCancelled = 2;
constexpr int kDbStatusExpired = 3;
constexpr size_t kPendingRecoveryBatchSize = 32;

struct PersistedListingRow {
  AuctionHandler::ListingState listing;
  uint64_t buyer_character_id = 0;
  uint64_t version = 0;
  int status = kDbStatusActive;
  bool item_returned = false;
};

struct PendingReturnDelivery {
  uint64_t listing_id = 0;
  entt::entity seller_entity = entt::null;
  uint32_t item_id = 0;
  uint32_t count = 0;
};

struct AppliedReturnDelivery {
  PendingReturnDelivery delivery;
  entt::entity item_entity = entt::null;
};

const char* kListingColumns =
    "listing_id, seller_character_id, buyer_character_id, item_id, item_count, unit_price, "
    "status, item_returned, version, "
    "CAST(EXTRACT(EPOCH FROM created_at) * 1000 AS BIGINT) AS created_at_ms, "
    "CAST(EXTRACT(EPOCH FROM expires_at) * 1000 AS BIGINT) AS expires_at_ms";

uint64_t NowMs() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch())
                                   .count());
}

uint32_t ClampToUint32(uint64_t value) {
  if (value > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
    return std::numeric_limits<uint32_t>::max();
  }
  return static_cast<uint32_t>(value);
}

uint32_t ComputeTotalPrice(const AuctionHandler::ListingState& listing) {
  return ClampToUint32(static_cast<uint64_t>(listing.count) *
                       static_cast<uint64_t>(listing.unit_price));
}

void ApplyDbStatus(AuctionHandler::ListingState* listing, int status) {
  if (listing == nullptr) {
    return;
  }
  listing->sold = status == kDbStatusSold;
  listing->cancelled = status == kDbStatusCancelled || status == kDbStatusExpired;
}

std::optional<PersistedListingRow> ParsePersistedListingRow(const pqxx::row& row) {
  PersistedListingRow parsed;
  parsed.listing.listing_id = row["listing_id"].as<uint64_t>(0);
  if (parsed.listing.listing_id == 0) {
    return std::nullopt;
  }
  parsed.listing.seller_character_id = row["seller_character_id"].as<uint64_t>(0);
  parsed.listing.item_id = row["item_id"].as<uint32_t>(0);
  parsed.listing.count = row["item_count"].as<uint32_t>(0);
  parsed.listing.unit_price = row["unit_price"].as<uint32_t>(0);
  parsed.listing.created_at_ms = row["created_at_ms"].as<uint64_t>(0);
  parsed.listing.expires_at_ms = row["expires_at_ms"].as<uint64_t>(0);
  parsed.status = row["status"].as<int>(kDbStatusActive);
  parsed.item_returned = row["item_returned"].as<bool>(false);
  parsed.version = row["version"].as<uint64_t>(0);
  parsed.buyer_character_id = row["buyer_character_id"].as<uint64_t>(0);
  ApplyDbStatus(&parsed.listing, parsed.status);
  return parsed;
}

uint16_t CountFreeInventorySlots(const entt::registry& registry, entt::entity owner) {
  if (owner == entt::null || !registry.valid(owner)) {
    return 0;
  }

  std::array<bool, kMaxInventorySlots> occupied{};
  auto view = registry.view<mir2::ecs::InventoryOwnerComponent>();
  for (const entt::entity entity : view) {
    const auto& owner_component = view.get<mir2::ecs::InventoryOwnerComponent>(entity);
    if (owner_component.owner != owner || owner_component.slot_index < 0 ||
        owner_component.slot_index >= static_cast<int>(kMaxInventorySlots)) {
      continue;
    }
    occupied[static_cast<size_t>(owner_component.slot_index)] = true;
  }

  uint16_t free_slots = 0;
  for (bool used : occupied) {
    if (!used) {
      ++free_slots;
    }
  }
  return free_slots;
}

flatbuffers::Offset<mir2::proto::AuctionListing> BuildListingOffset(
    flatbuffers::FlatBufferBuilder& builder,
    const AuctionHandler::ListingState& listing) {
  return mir2::proto::CreateAuctionListing(builder,
                                           listing.listing_id,
                                           static_cast<uint32_t>(listing.seller_character_id),
                                           listing.item_id,
                                           listing.count,
                                           listing.unit_price,
                                           ComputeTotalPrice(listing),
                                           listing.created_at_ms,
                                           listing.expires_at_ms,
                                           listing.sold,
                                           listing.cancelled);
}

}  // namespace

AuctionHandler::AuctionHandler(ResponseSender& response_sender,
                               ClientRegistry& client_registry,
                               entt::registry& ecs_registry,
                               RoleStore* role_store,
                               std::shared_ptr<mir2::db::PgConnectionPool> db_pool)
    : response_sender_(response_sender),
      client_registry_(client_registry),
      ecs_registry_(ecs_registry),
      role_store_(role_store),
      db_pool_(std::move(db_pool)) {
  BootstrapPersistence();
}

Task<void> AuctionHandler::HandleMessage(HandlerContext ctx,
                                         const uint8_t* payload,
                                         size_t payload_size) {
  try {
    if (!payload || payload_size == 0) {
      SYSLOG_WARN("AuctionHandler ignored empty payload (client_id={}, msg_id={})",
                  ctx.client_id,
                  ctx.msg_id);
      co_return;
    }

    switch (static_cast<mir2::common::MsgId>(ctx.msg_id)) {
      case mir2::common::MsgId::kAuctionListReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::AuctionListReq>(nullptr)) {
          co_await SendListRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, {}, 0);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::AuctionListReq>(payload);
        co_await HandleList(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kAuctionSellReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::AuctionSellReq>(nullptr)) {
          co_await SendSellRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::AuctionSellReq>(payload);
        co_await HandleSell(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kAuctionBuyReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::AuctionBuyReq>(nullptr)) {
          co_await SendBuyRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::AuctionBuyReq>(payload);
        co_await HandleBuy(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kAuctionCancelReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::AuctionCancelReq>(nullptr)) {
          co_await SendCancelRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::AuctionCancelReq>(payload);
        co_await HandleCancel(std::move(ctx), req);
        co_return;
      }
      default:
        SYSLOG_WARN("AuctionHandler unsupported msg_id={} client_id={}",
                    ctx.msg_id,
                    ctx.client_id);
        co_return;
    }
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("AuctionHandler exception msg_id={} client_id={} error={}",
                 ctx.msg_id,
                 ctx.client_id,
                 ex.what());
    co_return;
  } catch (...) {
    SYSLOG_ERROR("AuctionHandler exception msg_id={} client_id={} error=unknown",
                 ctx.msg_id,
                 ctx.client_id);
    co_return;
  }
}

Task<void> AuctionHandler::HandleList(HandlerContext ctx,
                                      const mir2::proto::AuctionListReq* req) {
  if (PersistenceEnabled()) {
    co_await HandleListPersistent(std::move(ctx), req);
    co_return;
  }

  if (!req) {
    co_await SendListRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, {}, 0);
    co_return;
  }

  SweepExpiredListings();

  uint32_t page = req->page();
  if (page == 0) {
    page = 1;
  }
  uint32_t page_size = req->page_size();
  if (page_size == 0) {
    page_size = kDefaultPageSize;
  }
  page_size = std::clamp(page_size, 1u, kMaxPageSize);

  const bool seller_only = req->seller_only();
  const uint64_t requester_character_id = GetCharacterId(ctx.entity).value_or(0);
  if (seller_only && requester_character_id == 0) {
    co_await SendListRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, {}, 0);
    co_return;
  }

  std::vector<ListingState> all;
  all.reserve(listings_.size());
  for (const auto& [_, listing] : listings_) {
    if (listing.sold || listing.cancelled) {
      continue;
    }
    if (seller_only && listing.seller_character_id != requester_character_id) {
      continue;
    }
    all.push_back(listing);
  }

  std::sort(all.begin(), all.end(), [](const ListingState& lhs, const ListingState& rhs) {
    return lhs.listing_id > rhs.listing_id;
  });

  const uint32_t total_count = ClampToUint32(all.size());
  const uint64_t offset = static_cast<uint64_t>(page - 1) * page_size;
  std::vector<ListingState> page_data;
  if (offset < all.size()) {
    const uint64_t limit = std::min<uint64_t>(all.size(), offset + page_size);
    page_data.assign(all.begin() + static_cast<std::ptrdiff_t>(offset),
                     all.begin() + static_cast<std::ptrdiff_t>(limit));
  }

  co_await SendListRsp(
      ctx.client_id, true, mir2::common::ErrorCode::kOk, page_data, total_count);
}

Task<void> AuctionHandler::HandleListPersistent(HandlerContext ctx,
                                                const mir2::proto::AuctionListReq* req) {
  if (!req) {
    co_await SendListRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, {}, 0);
    co_return;
  }

  BootstrapPersistence();
  SweepExpiredListingsPersistent();

  uint32_t page = req->page() == 0 ? 1 : req->page();
  uint32_t page_size = req->page_size() == 0 ? kDefaultPageSize : req->page_size();
  page_size = std::clamp(page_size, 1u, kMaxPageSize);

  const bool seller_only = req->seller_only();
  const uint64_t requester_character_id = GetCharacterId(ctx.entity).value_or(0);
  if (seller_only && requester_character_id == 0) {
    co_await SendListRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, {}, 0);
    co_return;
  }

  if (requester_character_id != 0) {
    RecoverPendingReturnsForCharacter(requester_character_id);
  }

  std::vector<ListingState> listings;
  uint32_t total_count = 0;
  bool query_ok = true;

  try {
    auto conn = db_pool_ ? db_pool_->Acquire() : nullptr;
    if (!conn || !db_pool_) {
      co_await SendListRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kUnknown, {}, 0);
      co_return;
    }
    db::PgConnectionGuard guard(*db_pool_, conn);
    pqxx::read_transaction txn(*conn);

    uint64_t total = 0;
    if (seller_only) {
      const pqxx::result count_result = txn.exec(
          "SELECT COUNT(*) FROM auction_listings "
          "WHERE status = $1 AND expires_at > NOW() AND seller_character_id = $2",
          pqxx::params{kDbStatusActive, requester_character_id});
      if (!count_result.empty()) {
        total = count_result[0][0].as<uint64_t>(0);
      }
    } else {
      const pqxx::result count_result = txn.exec(
          "SELECT COUNT(*) FROM auction_listings "
          "WHERE status = $1 AND expires_at > NOW()",
          pqxx::params{kDbStatusActive});
      if (!count_result.empty()) {
        total = count_result[0][0].as<uint64_t>(0);
      }
    }
    total_count = ClampToUint32(total);

    const uint64_t offset = static_cast<uint64_t>(page - 1) * page_size;
    const std::string list_sql = std::string("SELECT ") + kListingColumns +
                                 " FROM auction_listings "
                                 "WHERE status = $1 AND expires_at > NOW()" +
                                 (seller_only ? " AND seller_character_id = $2 " : " ") +
                                 "ORDER BY listing_id DESC "
                                 "LIMIT $" + (seller_only ? "3" : "2") +
                                 " OFFSET $" + (seller_only ? "4" : "3");

    pqxx::result rows;
    if (seller_only) {
      rows = txn.exec(
          list_sql,
          pqxx::params{kDbStatusActive, requester_character_id, page_size, offset});
    } else {
      rows = txn.exec(list_sql, pqxx::params{kDbStatusActive, page_size, offset});
    }

    listings.reserve(rows.size());
    for (const auto& row : rows) {
      const auto parsed = ParsePersistedListingRow(row);
      if (!parsed.has_value()) {
        continue;
      }
      listings.push_back(parsed->listing);
    }

    txn.commit();
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("AuctionHandler persistent list failed (client_id={}): {}",
                 ctx.client_id,
                 ex.what());
    query_ok = false;
  }

  if (!query_ok) {
    co_await SendListRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kUnknown, {}, 0);
    co_return;
  }

  co_await SendListRsp(
      ctx.client_id, true, mir2::common::ErrorCode::kOk, listings, total_count);
}

Task<void> AuctionHandler::HandleSell(HandlerContext ctx,
                                      const mir2::proto::AuctionSellReq* req) {
  if (PersistenceEnabled()) {
    co_await HandleSellPersistent(std::move(ctx), req);
    co_return;
  }

  if (!req || ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity)) {
    co_await SendSellRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
    co_return;
  }

  const auto seller_character_id = GetCharacterId(ctx.entity);
  if (!seller_character_id.has_value() || req->item_id() == 0 || req->count() == 0 ||
      req->unit_price() == 0) {
    co_await SendSellRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
    co_return;
  }

  const entt::entity item_entity = FindInventoryItem(
      ctx.entity, req->inventory_slot(), req->item_id(), req->count());
  if (item_entity == entt::null) {
    co_await SendSellRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTargetNotFound, 0);
    co_return;
  }

  auto* item_component = ecs_registry_.try_get<mir2::ecs::ItemComponent>(item_entity);
  if (!item_component || item_component->count < static_cast<int>(req->count())) {
    co_await SendSellRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTargetNotFound, 0);
    co_return;
  }

  item_component->count -= static_cast<int>(req->count());
  if (item_component->count <= 0) {
    ecs_registry_.destroy(item_entity);
  }
  MarkItemsDirty(ctx.entity);

  uint32_t duration_sec = req->duration_sec();
  if (duration_sec == 0) {
    duration_sec = kDefaultDurationSec;
  }
  duration_sec = std::clamp(duration_sec, kMinDurationSec, kMaxDurationSec);

  ListingState listing;
  listing.listing_id = next_listing_id_++;
  listing.seller_character_id = *seller_character_id;
  listing.item_id = req->item_id();
  listing.count = req->count();
  listing.unit_price = req->unit_price();
  listing.created_at_ms = NowMs();
  listing.expires_at_ms =
      listing.created_at_ms + static_cast<uint64_t>(duration_sec) * 1000;
  listing.sold = false;
  listing.cancelled = false;

  listings_[listing.listing_id] = listing;

  co_await SendSellRsp(
      ctx.client_id, true, mir2::common::ErrorCode::kOk, listing.listing_id);
  co_await SendNotify(ctx.client_id, mir2::proto::AuctionNotifyType::LISTED, listing);
}

Task<void> AuctionHandler::HandleSellPersistent(HandlerContext ctx,
                                                const mir2::proto::AuctionSellReq* req) {
  if (!req || ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity)) {
    co_await SendSellRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
    co_return;
  }

  BootstrapPersistence();

  const auto seller_character_id = GetCharacterId(ctx.entity);
  if (!seller_character_id.has_value() || req->item_id() == 0 || req->count() == 0 ||
      req->unit_price() == 0) {
    co_await SendSellRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
    co_return;
  }

  SweepExpiredListingsPersistent();
  RecoverPendingReturnsForCharacter(*seller_character_id);

  const entt::entity item_entity =
      FindInventoryItem(ctx.entity, req->inventory_slot(), req->item_id(), req->count());
  if (item_entity == entt::null) {
    co_await SendSellRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTargetNotFound, 0);
    co_return;
  }

  auto* item_component = ecs_registry_.try_get<mir2::ecs::ItemComponent>(item_entity);
  if (!item_component || item_component->count < static_cast<int>(req->count())) {
    co_await SendSellRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTargetNotFound, 0);
    co_return;
  }

  uint32_t duration_sec = req->duration_sec();
  if (duration_sec == 0) {
    duration_sec = kDefaultDurationSec;
  }
  duration_sec = std::clamp(duration_sec, kMinDurationSec, kMaxDurationSec);

  ListingState listing;
  listing.seller_character_id = *seller_character_id;
  listing.item_id = req->item_id();
  listing.count = req->count();
  listing.unit_price = req->unit_price();
  listing.created_at_ms = NowMs();
  listing.expires_at_ms =
      listing.created_at_ms + static_cast<uint64_t>(duration_sec) * 1000;
  listing.sold = false;
  listing.cancelled = false;

  const int original_count = item_component->count;
  item_component->count -= static_cast<int>(req->count());
  bool persisted = true;

  try {
    auto conn = db_pool_ ? db_pool_->Acquire() : nullptr;
    if (!conn || !db_pool_) {
      item_component->count = original_count;
      co_await SendSellRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kUnknown, 0);
      co_return;
    }
    db::PgConnectionGuard guard(*db_pool_, conn);
    pqxx::work txn(*conn);

    const pqxx::result inserted = txn.exec(
        "INSERT INTO auction_listings "
        "(seller_character_id, buyer_character_id, item_id, item_count, unit_price, "
        "status, item_returned, version, created_at, expires_at, updated_at) "
        "VALUES ($1, 0, $2, $3, $4, $5, FALSE, 0, "
        "TIMESTAMPTZ 'epoch' + ($6::BIGINT * INTERVAL '1 millisecond'), "
        "TIMESTAMPTZ 'epoch' + ($7::BIGINT * INTERVAL '1 millisecond'), NOW()) "
        "RETURNING listing_id",
        pqxx::params{listing.seller_character_id,
                     listing.item_id,
                     listing.count,
                     listing.unit_price,
                     kDbStatusActive,
                     listing.created_at_ms,
                     listing.expires_at_ms});

    if (inserted.empty()) {
      item_component->count = original_count;
      co_await SendSellRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kUnknown, 0);
      co_return;
    }

    listing.listing_id = inserted[0]["listing_id"].as<uint64_t>(0);
    txn.commit();
  } catch (const std::exception& ex) {
    item_component->count = original_count;
    SYSLOG_ERROR("AuctionHandler persistent sell failed (client_id={}): {}",
                 ctx.client_id,
                 ex.what());
    persisted = false;
  }

  if (!persisted) {
    co_await SendSellRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kUnknown, 0);
    co_return;
  }

  if (item_component->count <= 0) {
    ecs_registry_.destroy(item_entity);
  }

  MarkItemsDirty(ctx.entity);
  co_await SendSellRsp(
      ctx.client_id, true, mir2::common::ErrorCode::kOk, listing.listing_id);
  co_await SendNotify(ctx.client_id, mir2::proto::AuctionNotifyType::LISTED, listing);
}

Task<void> AuctionHandler::HandleBuy(HandlerContext ctx,
                                     const mir2::proto::AuctionBuyReq* req) {
  if (PersistenceEnabled()) {
    co_await HandleBuyPersistent(std::move(ctx), req);
    co_return;
  }

  if (!req || ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity)) {
    co_await SendBuyRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
    co_return;
  }

  SweepExpiredListings();

  auto listing_it = listings_.find(req->listing_id());
  if (listing_it == listings_.end()) {
    co_await SendBuyRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kAuctionNotFound, req->listing_id());
    co_return;
  }

  ListingState& listing = listing_it->second;
  if (listing.sold) {
    co_await SendBuyRsp(ctx.client_id,
                        false,
                        mir2::common::ErrorCode::kAuctionAlreadySold,
                        listing.listing_id);
    co_return;
  }
  if (listing.cancelled || listing.expires_at_ms <= NowMs()) {
    co_await SendBuyRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kAuctionNotFound, listing.listing_id);
    co_return;
  }

  const auto buyer_character_id = GetCharacterId(ctx.entity);
  if (!buyer_character_id.has_value() || *buyer_character_id == listing.seller_character_id) {
    co_await SendBuyRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, listing.listing_id);
    co_return;
  }

  auto* buyer_attributes = ecs_registry_.try_get<mir2::ecs::CharacterAttributesComponent>(ctx.entity);
  if (!buyer_attributes) {
    co_await SendBuyRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, listing.listing_id);
    co_return;
  }

  const auto seller_entity = FindOnlineEntityByCharacterId(listing.seller_character_id);
  if (!seller_entity.has_value()) {
    co_await SendBuyRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTargetNotFound, listing.listing_id);
    co_return;
  }

  auto* seller_attributes =
      ecs_registry_.try_get<mir2::ecs::CharacterAttributesComponent>(*seller_entity);
  if (!seller_attributes) {
    co_await SendBuyRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, listing.listing_id);
    co_return;
  }

  const uint32_t total_price = ComputeTotalPrice(listing);
  if (buyer_attributes->gold < static_cast<int>(total_price)) {
    co_await SendBuyRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kAuctionBidTooLow, listing.listing_id);
    co_return;
  }

  buyer_attributes->gold -= static_cast<int>(total_price);
  seller_attributes->gold += static_cast<int>(total_price);
  MarkAttributesDirty(ctx.entity);
  MarkAttributesDirty(*seller_entity);

  AddInventoryItem(ctx.entity, listing.item_id, listing.count);
  MarkItemsDirty(ctx.entity);

  listing.sold = true;

  co_await SendBuyRsp(
      ctx.client_id, true, mir2::common::ErrorCode::kOk, listing.listing_id);
  co_await SendNotify(ctx.client_id, mir2::proto::AuctionNotifyType::BOUGHT, listing);

  const auto seller_client_id = GetClientIdByCharacterId(listing.seller_character_id);
  if (seller_client_id.has_value()) {
    co_await SendNotify(*seller_client_id, mir2::proto::AuctionNotifyType::SOLD, listing);
  }
}

Task<void> AuctionHandler::HandleBuyPersistent(HandlerContext ctx,
                                               const mir2::proto::AuctionBuyReq* req) {
  if (!req || ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity)) {
    co_await SendBuyRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
    co_return;
  }

  const auto buyer_character_id = GetCharacterId(ctx.entity);
  if (!buyer_character_id.has_value()) {
    co_await SendBuyRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, req->listing_id());
    co_return;
  }

  BootstrapPersistence();
  SweepExpiredListingsPersistent();
  RecoverPendingReturnsForCharacter(*buyer_character_id);

  auto* buyer_attributes = ecs_registry_.try_get<mir2::ecs::CharacterAttributesComponent>(ctx.entity);
  if (!buyer_attributes) {
    co_await SendBuyRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, req->listing_id());
    co_return;
  }

  std::optional<PersistedListingRow> persisted;
  auto failure_code = mir2::common::ErrorCode::kUnknown;
  auto seller_client_id = std::optional<uint64_t>{};

  int buyer_gold_before = buyer_attributes->gold;
  int seller_gold_before = 0;
  auto* seller_attributes = static_cast<mir2::ecs::CharacterAttributesComponent*>(nullptr);
  entt::entity seller_entity = entt::null;
  entt::entity granted_item = entt::null;
  bool ecs_mutated = false;

  try {
    auto conn = db_pool_ ? db_pool_->Acquire() : nullptr;
    if (!conn || !db_pool_) {
      co_await SendBuyRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kUnknown, req->listing_id());
      co_return;
    }
    db::PgConnectionGuard guard(*db_pool_, conn);
    pqxx::work txn(*conn);

    const std::string select_sql =
        std::string("SELECT ") + kListingColumns +
        " FROM auction_listings WHERE listing_id = $1 FOR UPDATE";
    const pqxx::result rows = txn.exec(select_sql, pqxx::params{req->listing_id()});
    if (rows.empty()) {
      co_await SendBuyRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kAuctionNotFound, req->listing_id());
      co_return;
    }

    persisted = ParsePersistedListingRow(rows[0]);
    if (!persisted.has_value()) {
      co_await SendBuyRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kAuctionNotFound, req->listing_id());
      co_return;
    }

    auto listing = persisted->listing;
    if (persisted->status == kDbStatusSold) {
      co_await SendBuyRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kAuctionAlreadySold, listing.listing_id);
      co_return;
    }
    if (persisted->status != kDbStatusActive) {
      co_await SendBuyRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kAuctionNotFound, listing.listing_id);
      co_return;
    }

    if (listing.expires_at_ms <= NowMs()) {
      txn.exec(
          "UPDATE auction_listings "
          "SET status = $2, cancelled_at = NOW(), updated_at = NOW(), version = version + 1 "
          "WHERE listing_id = $1 AND status = $3",
          pqxx::params{listing.listing_id, kDbStatusExpired, kDbStatusActive});
      txn.commit();
      RecoverPendingReturnsForCharacter(listing.seller_character_id);
      co_await SendBuyRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kAuctionNotFound, listing.listing_id);
      co_return;
    }

    if (*buyer_character_id == listing.seller_character_id) {
      co_await SendBuyRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, listing.listing_id);
      co_return;
    }

    const auto seller_entity_opt = FindOnlineEntityByCharacterId(listing.seller_character_id);
    if (!seller_entity_opt.has_value()) {
      co_await SendBuyRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kTargetNotFound, listing.listing_id);
      co_return;
    }
    seller_entity = *seller_entity_opt;

    seller_attributes =
        ecs_registry_.try_get<mir2::ecs::CharacterAttributesComponent>(seller_entity);
    if (!seller_attributes) {
      co_await SendBuyRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, listing.listing_id);
      co_return;
    }

    const uint32_t total_price = ComputeTotalPrice(listing);
    if (buyer_attributes->gold < static_cast<int>(total_price)) {
      co_await SendBuyRsp(
          ctx.client_id,
          false,
          mir2::common::ErrorCode::kAuctionBidTooLow,
          listing.listing_id);
      co_return;
    }

    buyer_gold_before = buyer_attributes->gold;
    seller_gold_before = seller_attributes->gold;

    granted_item = AddInventoryItem(
        ctx.entity, listing.item_id, listing.count, listing.listing_id);
    if (granted_item == entt::null) {
      co_await SendBuyRsp(
          ctx.client_id,
          false,
          mir2::common::ErrorCode::kInvalidAction,
          listing.listing_id);
      co_return;
    }

    buyer_attributes->gold -= static_cast<int>(total_price);
    seller_attributes->gold += static_cast<int>(total_price);
    ecs_mutated = true;

    const pqxx::result updated = txn.exec(
        "UPDATE auction_listings "
        "SET status = $2, buyer_character_id = $3, sold_at = NOW(), "
        "item_returned = TRUE, updated_at = NOW(), version = version + 1 "
        "WHERE listing_id = $1 AND status = $4 AND version = $5 "
        "RETURNING listing_id",
        pqxx::params{listing.listing_id,
                     kDbStatusSold,
                     *buyer_character_id,
                     kDbStatusActive,
                     persisted->version});

    if (updated.empty()) {
      if (ecs_registry_.valid(granted_item)) {
        ecs_registry_.destroy(granted_item);
      }
      buyer_attributes->gold = buyer_gold_before;
      seller_attributes->gold = seller_gold_before;

      const pqxx::result state_rows =
          txn.exec("SELECT status FROM auction_listings WHERE listing_id = $1",
                   pqxx::params{listing.listing_id});
      if (!state_rows.empty() && state_rows[0]["status"].as<int>(kDbStatusActive) ==
                                    kDbStatusSold) {
        failure_code = mir2::common::ErrorCode::kAuctionAlreadySold;
      } else {
        failure_code = mir2::common::ErrorCode::kAuctionNotFound;
      }
      co_await SendBuyRsp(ctx.client_id, false, failure_code, listing.listing_id);
      co_return;
    }

    txn.commit();

    MarkAttributesDirty(ctx.entity);
    MarkAttributesDirty(seller_entity);
    MarkItemsDirty(ctx.entity);

    listing.sold = true;
    listing.cancelled = false;
    co_await SendBuyRsp(
        ctx.client_id, true, mir2::common::ErrorCode::kOk, listing.listing_id);
    co_await SendNotify(ctx.client_id, mir2::proto::AuctionNotifyType::BOUGHT, listing);

    seller_client_id = GetClientIdByCharacterId(listing.seller_character_id);
    if (seller_client_id.has_value()) {
      co_await SendNotify(*seller_client_id, mir2::proto::AuctionNotifyType::SOLD, listing);
    }
    co_return;
  } catch (const std::exception& ex) {
    if (ecs_mutated) {
      if (granted_item != entt::null && ecs_registry_.valid(granted_item)) {
        ecs_registry_.destroy(granted_item);
      }
      if (seller_attributes != nullptr) {
        seller_attributes->gold = seller_gold_before;
      }
      buyer_attributes->gold = buyer_gold_before;
    }

    SYSLOG_ERROR("AuctionHandler persistent buy failed (client_id={} listing_id={}): {}",
                 ctx.client_id,
                 req->listing_id(),
                 ex.what());
    failure_code = mir2::common::ErrorCode::kUnknown;
  }

  co_await SendBuyRsp(ctx.client_id, false, failure_code, req->listing_id());
  co_return;
}

Task<void> AuctionHandler::HandleCancel(HandlerContext ctx,
                                        const mir2::proto::AuctionCancelReq* req) {
  if (PersistenceEnabled()) {
    co_await HandleCancelPersistent(std::move(ctx), req);
    co_return;
  }

  if (!req || ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity)) {
    co_await SendCancelRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
    co_return;
  }

  SweepExpiredListings();

  auto listing_it = listings_.find(req->listing_id());
  if (listing_it == listings_.end()) {
    co_await SendCancelRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kAuctionNotFound, req->listing_id());
    co_return;
  }

  ListingState& listing = listing_it->second;
  const auto seller_character_id = GetCharacterId(ctx.entity);
  if (!seller_character_id.has_value() || *seller_character_id != listing.seller_character_id) {
    co_await SendCancelRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, listing.listing_id);
    co_return;
  }

  if (listing.sold) {
    co_await SendCancelRsp(ctx.client_id,
                           false,
                           mir2::common::ErrorCode::kAuctionAlreadySold,
                           listing.listing_id);
    co_return;
  }

  if (listing.cancelled) {
    co_await SendCancelRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kAuctionNotFound, listing.listing_id);
    co_return;
  }

  listing.cancelled = true;
  AddInventoryItem(ctx.entity, listing.item_id, listing.count);
  MarkItemsDirty(ctx.entity);

  co_await SendCancelRsp(
      ctx.client_id, true, mir2::common::ErrorCode::kOk, listing.listing_id);
  co_await SendNotify(ctx.client_id, mir2::proto::AuctionNotifyType::CANCELLED, listing);
}

Task<void> AuctionHandler::HandleCancelPersistent(HandlerContext ctx,
                                                  const mir2::proto::AuctionCancelReq* req) {
  if (!req || ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity)) {
    co_await SendCancelRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
    co_return;
  }

  const auto seller_character_id = GetCharacterId(ctx.entity);
  if (!seller_character_id.has_value()) {
    co_await SendCancelRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, req->listing_id());
    co_return;
  }

  BootstrapPersistence();
  SweepExpiredListingsPersistent();
  RecoverPendingReturnsForCharacter(*seller_character_id);

  entt::entity restored_item = entt::null;
  bool success = false;
  uint64_t success_listing_id = req->listing_id();
  ListingState notify_listing;

  try {
    auto conn = db_pool_ ? db_pool_->Acquire() : nullptr;
    if (!conn || !db_pool_) {
      co_await SendCancelRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kUnknown, req->listing_id());
      co_return;
    }
    db::PgConnectionGuard guard(*db_pool_, conn);
    pqxx::work txn(*conn);

    const std::string select_sql =
        std::string("SELECT ") + kListingColumns +
        " FROM auction_listings WHERE listing_id = $1 FOR UPDATE";
    const pqxx::result rows = txn.exec(select_sql, pqxx::params{req->listing_id()});
    if (rows.empty()) {
      co_await SendCancelRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kAuctionNotFound, req->listing_id());
      co_return;
    }

    const auto persisted = ParsePersistedListingRow(rows[0]);
    if (!persisted.has_value()) {
      co_await SendCancelRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kAuctionNotFound, req->listing_id());
      co_return;
    }

    auto listing = persisted->listing;
    if (listing.seller_character_id != *seller_character_id) {
      co_await SendCancelRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, listing.listing_id);
      co_return;
    }

    if (persisted->status == kDbStatusSold) {
      co_await SendCancelRsp(ctx.client_id,
                             false,
                             mir2::common::ErrorCode::kAuctionAlreadySold,
                             listing.listing_id);
      co_return;
    }

    if (persisted->status != kDbStatusActive) {
      co_await SendCancelRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kAuctionNotFound, listing.listing_id);
      co_return;
    }

    if (listing.expires_at_ms <= NowMs()) {
      txn.exec(
          "UPDATE auction_listings "
          "SET status = $2, cancelled_at = NOW(), updated_at = NOW(), version = version + 1 "
          "WHERE listing_id = $1 AND status = $3",
          pqxx::params{listing.listing_id, kDbStatusExpired, kDbStatusActive});
      txn.commit();
      RecoverPendingReturnsForCharacter(*seller_character_id);
      co_await SendCancelRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kAuctionNotFound, listing.listing_id);
      co_return;
    }

    restored_item =
        AddInventoryItem(ctx.entity, listing.item_id, listing.count, listing.listing_id);
    if (restored_item == entt::null) {
      co_await SendCancelRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, listing.listing_id);
      co_return;
    }

    const pqxx::result updated = txn.exec(
        "UPDATE auction_listings "
        "SET status = $2, cancelled_at = NOW(), item_returned = TRUE, "
        "updated_at = NOW(), version = version + 1 "
        "WHERE listing_id = $1 AND status = $3 AND version = $4 "
        "RETURNING listing_id",
        pqxx::params{listing.listing_id,
                     kDbStatusCancelled,
                     kDbStatusActive,
                     persisted->version});

    if (updated.empty()) {
      if (ecs_registry_.valid(restored_item)) {
        ecs_registry_.destroy(restored_item);
      }
      co_await SendCancelRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kAuctionNotFound, listing.listing_id);
      co_return;
    }

    txn.commit();

    MarkItemsDirty(ctx.entity);
    listing.sold = false;
    listing.cancelled = true;
    success = true;
    success_listing_id = listing.listing_id;
    notify_listing = listing;
  } catch (const std::exception& ex) {
    if (restored_item != entt::null && ecs_registry_.valid(restored_item)) {
      ecs_registry_.destroy(restored_item);
    }
    SYSLOG_ERROR("AuctionHandler persistent cancel failed (client_id={} listing_id={}): {}",
                 ctx.client_id,
                 req->listing_id(),
                 ex.what());
  }

  if (success) {
    co_await SendCancelRsp(
        ctx.client_id, true, mir2::common::ErrorCode::kOk, success_listing_id);
    co_await SendNotify(ctx.client_id, mir2::proto::AuctionNotifyType::CANCELLED, notify_listing);
    co_return;
  }

  co_await SendCancelRsp(
      ctx.client_id, false, mir2::common::ErrorCode::kUnknown, req->listing_id());
  co_return;
}

Task<void> AuctionHandler::SendListRsp(uint64_t client_id,
                                       bool success,
                                       mir2::common::ErrorCode code,
                                       const std::vector<ListingState>& listings,
                                       uint32_t total_count) {
  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<mir2::proto::AuctionListing>> listing_offsets;
  listing_offsets.reserve(listings.size());
  for (const auto& listing : listings) {
    listing_offsets.push_back(BuildListingOffset(builder, listing));
  }

  const auto listings_offset = builder.CreateVector(listing_offsets);
  const auto rsp = mir2::proto::CreateAuctionListRsp(
      builder, success, static_cast<int>(ToProtoError(code)), total_count, listings_offset);
  builder.Finish(rsp);

  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kAuctionListRsp),
      std::move(payload));
}

Task<void> AuctionHandler::SendSellRsp(uint64_t client_id,
                                       bool success,
                                       mir2::common::ErrorCode code,
                                       uint64_t listing_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateAuctionSellRsp(
      builder, success, static_cast<int>(ToProtoError(code)), listing_id);
  builder.Finish(rsp);

  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kAuctionSellRsp),
      std::move(payload));
}

Task<void> AuctionHandler::SendBuyRsp(uint64_t client_id,
                                      bool success,
                                      mir2::common::ErrorCode code,
                                      uint64_t listing_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateAuctionBuyRsp(
      builder, success, static_cast<int>(ToProtoError(code)), listing_id);
  builder.Finish(rsp);

  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kAuctionBuyRsp),
      std::move(payload));
}

Task<void> AuctionHandler::SendCancelRsp(uint64_t client_id,
                                         bool success,
                                         mir2::common::ErrorCode code,
                                         uint64_t listing_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateAuctionCancelRsp(
      builder, success, static_cast<int>(ToProtoError(code)), listing_id);
  builder.Finish(rsp);

  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kAuctionCancelRsp),
      std::move(payload));
}

Task<void> AuctionHandler::SendNotify(uint64_t client_id,
                                      mir2::proto::AuctionNotifyType notify_type,
                                      const ListingState& listing) {
  if (client_id == 0 || !client_registry_.Contains(client_id)) {
    co_return;
  }

  flatbuffers::FlatBufferBuilder builder;
  const auto listing_offset = BuildListingOffset(builder, listing);
  const auto notify = mir2::proto::CreateAuctionNotify(builder, notify_type, listing_offset);
  builder.Finish(notify);

  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kAuctionNotify),
      std::move(payload));
}

std::optional<uint64_t> AuctionHandler::GetCharacterId(entt::entity entity) const {
  if (entity == entt::null || !ecs_registry_.valid(entity)) {
    return std::nullopt;
  }
  const auto* identity = ecs_registry_.try_get<mir2::ecs::CharacterIdentityComponent>(entity);
  if (!identity || identity->id == 0) {
    return std::nullopt;
  }
  return identity->id;
}

std::optional<entt::entity> AuctionHandler::FindOnlineEntityByCharacterId(
    uint64_t character_id) const {
  if (character_id == 0) {
    return std::nullopt;
  }

  auto view = ecs_registry_.view<mir2::ecs::CharacterIdentityComponent,
                                 mir2::ecs::CharacterStateComponent>();
  for (const entt::entity entity : view) {
    const auto& identity = view.get<mir2::ecs::CharacterIdentityComponent>(entity);
    const auto& state = view.get<mir2::ecs::CharacterStateComponent>(entity);
    if (!state.is_online) {
      continue;
    }
    if (identity.id == character_id) {
      return entity;
    }
  }
  return std::nullopt;
}

std::optional<uint64_t> AuctionHandler::GetClientIdByCharacterId(uint64_t character_id) const {
  if (!role_store_ || character_id == 0) {
    return std::nullopt;
  }

  const auto client_id = role_store_->GetClientIdByRoleId(character_id);
  if (!client_id.has_value() || !client_registry_.Contains(*client_id)) {
    return std::nullopt;
  }
  return client_id;
}

entt::entity AuctionHandler::FindInventoryItem(entt::entity owner,
                                               uint16_t slot,
                                               uint32_t item_id,
                                               uint32_t count) const {
  auto view = ecs_registry_.view<mir2::ecs::ItemComponent, mir2::ecs::InventoryOwnerComponent>();
  for (const entt::entity entity : view) {
    const auto& item = view.get<mir2::ecs::ItemComponent>(entity);
    const auto& owner_component = view.get<mir2::ecs::InventoryOwnerComponent>(entity);
    if (owner_component.owner != owner) {
      continue;
    }
    if (owner_component.slot_index != slot) {
      continue;
    }
    if (item.item_id != item_id) {
      continue;
    }
    if (item.count < static_cast<int>(count)) {
      continue;
    }
    return entity;
  }
  return entt::null;
}

uint16_t AuctionHandler::FindFreeInventorySlot(entt::entity owner) const {
  std::array<bool, kMaxInventorySlots> used_slots{};

  auto view = ecs_registry_.view<mir2::ecs::InventoryOwnerComponent>();
  for (const entt::entity entity : view) {
    const auto& owner_component = view.get<mir2::ecs::InventoryOwnerComponent>(entity);
    if (owner_component.owner != owner) {
      continue;
    }
    if (owner_component.slot_index < 0 ||
        owner_component.slot_index >= static_cast<int>(kMaxInventorySlots)) {
      continue;
    }
    used_slots[static_cast<size_t>(owner_component.slot_index)] = true;
  }

  for (uint16_t slot = 0; slot < kMaxInventorySlots; ++slot) {
    if (!used_slots[slot]) {
      return slot;
    }
  }
  return std::numeric_limits<uint16_t>::max();
}

entt::entity AuctionHandler::AddInventoryItem(entt::entity owner,
                                              uint32_t item_id,
                                              uint32_t count,
                                              std::optional<uint64_t> instance_id) {
  const uint16_t slot = FindFreeInventorySlot(owner);
  if (slot == std::numeric_limits<uint16_t>::max()) {
    return entt::null;
  }

  const entt::entity item_entity = ecs_registry_.create();
  auto& item_component = ecs_registry_.emplace<mir2::ecs::ItemComponent>(item_entity);
  item_component.item_id = item_id;
  item_component.count = static_cast<int>(count);
  item_component.instance_id =
      instance_id.value_or(static_cast<uint64_t>(entt::to_integral(item_entity)));

  auto& owner_component = ecs_registry_.emplace<mir2::ecs::InventoryOwnerComponent>(item_entity);
  owner_component.owner = owner;
  owner_component.slot_index = slot;
  return item_entity;
}

void AuctionHandler::MarkItemsDirty(entt::entity entity) {
  if (entity == entt::null || !ecs_registry_.valid(entity)) {
    return;
  }
  auto* dirty = ecs_registry_.try_get<mir2::ecs::DirtyComponent>(entity);
  if (!dirty) {
    dirty = &ecs_registry_.emplace<mir2::ecs::DirtyComponent>(entity);
  }
  dirty->items_dirty = true;
}

void AuctionHandler::MarkAttributesDirty(entt::entity entity) {
  if (entity == entt::null || !ecs_registry_.valid(entity)) {
    return;
  }
  auto* dirty = ecs_registry_.try_get<mir2::ecs::DirtyComponent>(entity);
  if (!dirty) {
    dirty = &ecs_registry_.emplace<mir2::ecs::DirtyComponent>(entity);
  }
  dirty->attributes_dirty = true;
}

void AuctionHandler::SweepExpiredListings() {
  if (PersistenceEnabled()) {
    SweepExpiredListingsPersistent();
    return;
  }

  const uint64_t now_ms = NowMs();

  for (auto& [_, listing] : listings_) {
    if (listing.sold || listing.cancelled) {
      continue;
    }
    if (listing.expires_at_ms > now_ms) {
      continue;
    }

    listing.cancelled = true;
    const auto seller_entity = FindOnlineEntityByCharacterId(listing.seller_character_id);
    if (!seller_entity.has_value()) {
      continue;
    }
    const entt::entity returned = AddInventoryItem(*seller_entity, listing.item_id, listing.count);
    if (returned != entt::null) {
      MarkItemsDirty(*seller_entity);
    }
  }
}

void AuctionHandler::SweepExpiredListingsPersistent() {
  if (!PersistenceEnabled()) {
    return;
  }

  std::vector<AppliedReturnDelivery> provisional_deliveries;
  std::vector<AppliedReturnDelivery> committed_deliveries;
  bool txn_committed = false;
  try {
    auto conn = db_pool_->Acquire();
    if (!conn) {
      return;
    }
    db::PgConnectionGuard guard(*db_pool_, conn);
    pqxx::work txn(*conn);

    const std::string update_sql =
        std::string("UPDATE auction_listings ") +
        "SET status = $1, cancelled_at = NOW(), updated_at = NOW(), version = version + 1 "
        "WHERE status = $2 AND expires_at <= NOW() "
        "RETURNING " + kListingColumns;

    const pqxx::result expired =
        txn.exec(update_sql, pqxx::params{kDbStatusExpired, kDbStatusActive});

    std::unordered_map<entt::entity, uint16_t> remaining_slots_by_seller;
    std::vector<PendingReturnDelivery> pending_returns;

    for (const auto& row : expired) {
      const auto parsed = ParsePersistedListingRow(row);
      if (!parsed.has_value()) {
        continue;
      }
      const auto seller_entity = FindOnlineEntityByCharacterId(parsed->listing.seller_character_id);
      if (!seller_entity.has_value()) {
        continue;
      }

      auto slot_it = remaining_slots_by_seller.find(*seller_entity);
      if (slot_it == remaining_slots_by_seller.end()) {
        slot_it =
            remaining_slots_by_seller.emplace(
                *seller_entity, CountFreeInventorySlots(ecs_registry_, *seller_entity))
                .first;
      }
      if (slot_it->second == 0) {
        continue;
      }
      --slot_it->second;

      PendingReturnDelivery delivery;
      delivery.listing_id = parsed->listing.listing_id;
      delivery.seller_entity = *seller_entity;
      delivery.item_id = parsed->listing.item_id;
      delivery.count = parsed->listing.count;
      pending_returns.push_back(delivery);
    }

    for (const auto& delivery : pending_returns) {
      const entt::entity item = AddInventoryItem(
          delivery.seller_entity, delivery.item_id, delivery.count, delivery.listing_id);
      if (item == entt::null) {
        continue;
      }
      provisional_deliveries.push_back(AppliedReturnDelivery{
          .delivery = delivery,
          .item_entity = item,
      });
    }

    for (const auto& applied : provisional_deliveries) {
      const pqxx::result marked = txn.exec(
          "UPDATE auction_listings "
          "SET item_returned = TRUE, updated_at = NOW(), version = version + 1 "
          "WHERE listing_id = $1 AND item_returned = FALSE "
          "RETURNING listing_id",
          pqxx::params{applied.delivery.listing_id});
      if (marked.empty()) {
        if (ecs_registry_.valid(applied.item_entity)) {
          ecs_registry_.destroy(applied.item_entity);
        }
        continue;
      }
      committed_deliveries.push_back(applied);
    }

    txn.commit();
    txn_committed = true;
  } catch (const std::exception& ex) {
    if (!txn_committed) {
      for (const auto& applied : provisional_deliveries) {
        if (ecs_registry_.valid(applied.item_entity)) {
          ecs_registry_.destroy(applied.item_entity);
        }
      }
    }
    SYSLOG_ERROR("AuctionHandler persistent sweep failed: {}", ex.what());
    return;
  }

  std::vector<entt::entity> dirty_entities;
  dirty_entities.reserve(committed_deliveries.size());
  for (const auto& applied : committed_deliveries) {
    dirty_entities.push_back(applied.delivery.seller_entity);
  }
  std::sort(dirty_entities.begin(), dirty_entities.end());
  dirty_entities.erase(std::unique(dirty_entities.begin(), dirty_entities.end()),
                       dirty_entities.end());
  for (const entt::entity entity : dirty_entities) {
    MarkItemsDirty(entity);
  }
}

void AuctionHandler::RecoverPendingReturnsForCharacter(uint64_t character_id) {
  if (!PersistenceEnabled() || character_id == 0) {
    return;
  }

  const auto seller_entity = FindOnlineEntityByCharacterId(character_id);
  if (!seller_entity.has_value()) {
    return;
  }

  std::vector<AppliedReturnDelivery> provisional_deliveries;
  std::vector<AppliedReturnDelivery> committed_deliveries;
  bool txn_committed = false;
  try {
    auto conn = db_pool_->Acquire();
    if (!conn) {
      return;
    }
    db::PgConnectionGuard guard(*db_pool_, conn);
    pqxx::work txn(*conn);

    const std::string select_sql =
        std::string("SELECT ") + kListingColumns +
        " FROM auction_listings "
        "WHERE seller_character_id = $1 AND item_returned = FALSE "
        "AND status IN ($2, $3) "
        "ORDER BY listing_id ASC "
        "LIMIT $4 FOR UPDATE SKIP LOCKED";

    const pqxx::result pending_rows =
        txn.exec(select_sql,
                 pqxx::params{character_id,
                              kDbStatusCancelled,
                              kDbStatusExpired,
                              static_cast<uint32_t>(kPendingRecoveryBatchSize)});

    uint16_t remaining_slots = CountFreeInventorySlots(ecs_registry_, *seller_entity);
    std::vector<PendingReturnDelivery> pending_returns;
    for (const auto& row : pending_rows) {
      if (remaining_slots == 0) {
        break;
      }
      const auto parsed = ParsePersistedListingRow(row);
      if (!parsed.has_value()) {
        continue;
      }
      PendingReturnDelivery delivery;
      delivery.listing_id = parsed->listing.listing_id;
      delivery.seller_entity = *seller_entity;
      delivery.item_id = parsed->listing.item_id;
      delivery.count = parsed->listing.count;
      pending_returns.push_back(delivery);
      --remaining_slots;
    }

    for (const auto& delivery : pending_returns) {
      const entt::entity item = AddInventoryItem(
          delivery.seller_entity, delivery.item_id, delivery.count, delivery.listing_id);
      if (item == entt::null) {
        continue;
      }
      provisional_deliveries.push_back(AppliedReturnDelivery{
          .delivery = delivery,
          .item_entity = item,
      });
    }

    for (const auto& applied : provisional_deliveries) {
      const pqxx::result marked = txn.exec(
          "UPDATE auction_listings "
          "SET item_returned = TRUE, updated_at = NOW(), version = version + 1 "
          "WHERE listing_id = $1 AND item_returned = FALSE "
          "RETURNING listing_id",
          pqxx::params{applied.delivery.listing_id});
      if (marked.empty()) {
        if (ecs_registry_.valid(applied.item_entity)) {
          ecs_registry_.destroy(applied.item_entity);
        }
        continue;
      }
      committed_deliveries.push_back(applied);
    }

    txn.commit();
    txn_committed = true;
  } catch (const std::exception& ex) {
    if (!txn_committed) {
      for (const auto& applied : provisional_deliveries) {
        if (ecs_registry_.valid(applied.item_entity)) {
          ecs_registry_.destroy(applied.item_entity);
        }
      }
    }
    SYSLOG_ERROR("AuctionHandler pending return recovery failed (character_id={}): {}",
                 character_id,
                 ex.what());
    return;
  }

  if (!committed_deliveries.empty()) {
    MarkItemsDirty(*seller_entity);
  }
}

bool AuctionHandler::PersistenceEnabled() const {
  return db_pool_ != nullptr && db_pool_->IsReady();
}

void AuctionHandler::BootstrapPersistence() {
  if (!PersistenceEnabled() || persistence_bootstrapped_) {
    return;
  }

  try {
    auto conn = db_pool_->Acquire();
    if (!conn) {
      return;
    }
    db::PgConnectionGuard guard(*db_pool_, conn);
    pqxx::work txn(*conn);

    txn.exec(
        "CREATE TABLE IF NOT EXISTS auction_listings ("
        "listing_id BIGSERIAL PRIMARY KEY, "
        "seller_character_id BIGINT NOT NULL, "
        "buyer_character_id BIGINT NOT NULL DEFAULT 0, "
        "item_id INTEGER NOT NULL, "
        "item_count INTEGER NOT NULL CHECK (item_count > 0), "
        "unit_price INTEGER NOT NULL CHECK (unit_price > 0), "
        "status SMALLINT NOT NULL DEFAULT 0 CHECK (status BETWEEN 0 AND 3), "
        "item_returned BOOLEAN NOT NULL DEFAULT FALSE, "
        "version BIGINT NOT NULL DEFAULT 0, "
        "created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(), "
        "expires_at TIMESTAMPTZ NOT NULL, "
        "sold_at TIMESTAMPTZ, "
        "cancelled_at TIMESTAMPTZ, "
        "updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW())");
    txn.exec(
        "CREATE INDEX IF NOT EXISTS idx_auction_status_expires "
        "ON auction_listings (status, expires_at)");
    txn.exec(
        "CREATE INDEX IF NOT EXISTS idx_auction_seller_status "
        "ON auction_listings (seller_character_id, status)");
    txn.exec(
        "CREATE INDEX IF NOT EXISTS idx_auction_pending_return "
        "ON auction_listings (seller_character_id, item_returned, status)");

    txn.commit();
    persistence_bootstrapped_ = true;
    SweepExpiredListingsPersistent();
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("AuctionHandler persistence bootstrap failed: {}", ex.what());
  }
}

}  // namespace mir2::logic
