/**
 * @file auction_handler.h
 * @brief Auction handler for logic layer.
 */

#ifndef MIR2_LOGIC_HANDLERS_AUCTION_AUCTION_HANDLER_H_
#define MIR2_LOGIC_HANDLERS_AUCTION_AUCTION_HANDLER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

#include "logic/handler_context.h"
#include "logic/task.h"
#include "server/common/error_codes.h"

namespace mir2::proto {
class AuctionListReq;
class AuctionSellReq;
class AuctionBuyReq;
class AuctionCancelReq;
enum class AuctionNotifyType : uint8_t;
}  // namespace mir2::proto

namespace mir2::db {
class PgConnectionPool;
}  // namespace mir2::db

namespace mir2::logic {

class ClientRegistry;
class ResponseSender;
class RoleStore;

class AuctionHandler {
 public:
  struct ListingState {
    uint64_t listing_id = 0;
    uint64_t seller_character_id = 0;
    uint32_t item_id = 0;
    uint32_t count = 0;
    uint32_t unit_price = 0;
    uint64_t created_at_ms = 0;
    uint64_t expires_at_ms = 0;
    bool sold = false;
    bool cancelled = false;
  };

  AuctionHandler(ResponseSender& response_sender,
                 ClientRegistry& client_registry,
                 entt::registry& ecs_registry,
                 RoleStore* role_store,
                 std::shared_ptr<mir2::db::PgConnectionPool> db_pool = nullptr);

  Task<void> HandleMessage(HandlerContext ctx,
                           const uint8_t* payload,
                           size_t payload_size);

 private:
  Task<void> HandleList(HandlerContext ctx,
                        const mir2::proto::AuctionListReq* req);
  Task<void> HandleSell(HandlerContext ctx,
                        const mir2::proto::AuctionSellReq* req);
  Task<void> HandleBuy(HandlerContext ctx,
                       const mir2::proto::AuctionBuyReq* req);
  Task<void> HandleCancel(HandlerContext ctx,
                          const mir2::proto::AuctionCancelReq* req);
  Task<void> HandleListPersistent(HandlerContext ctx,
                                  const mir2::proto::AuctionListReq* req);
  Task<void> HandleSellPersistent(HandlerContext ctx,
                                  const mir2::proto::AuctionSellReq* req);
  Task<void> HandleBuyPersistent(HandlerContext ctx,
                                 const mir2::proto::AuctionBuyReq* req);
  Task<void> HandleCancelPersistent(HandlerContext ctx,
                                    const mir2::proto::AuctionCancelReq* req);

  Task<void> SendListRsp(uint64_t client_id,
                         bool success,
                         mir2::common::ErrorCode code,
                         const std::vector<ListingState>& listings,
                         uint32_t total_count);
  Task<void> SendSellRsp(uint64_t client_id,
                         bool success,
                         mir2::common::ErrorCode code,
                         uint64_t listing_id);
  Task<void> SendBuyRsp(uint64_t client_id,
                        bool success,
                        mir2::common::ErrorCode code,
                        uint64_t listing_id);
  Task<void> SendCancelRsp(uint64_t client_id,
                           bool success,
                           mir2::common::ErrorCode code,
                           uint64_t listing_id);
  Task<void> SendNotify(uint64_t client_id,
                        mir2::proto::AuctionNotifyType notify_type,
                        const ListingState& listing);

  std::optional<uint64_t> GetCharacterId(entt::entity entity) const;
  std::optional<entt::entity> FindOnlineEntityByCharacterId(uint64_t character_id) const;
  std::optional<uint64_t> GetClientIdByCharacterId(uint64_t character_id) const;
  entt::entity FindInventoryItem(entt::entity owner,
                                 uint16_t slot,
                                 uint32_t item_id,
                                 uint32_t count) const;
  uint16_t FindFreeInventorySlot(entt::entity owner) const;
  entt::entity AddInventoryItem(entt::entity owner,
                                uint32_t item_id,
                                uint32_t count,
                                std::optional<uint64_t> instance_id = std::nullopt);
  void MarkItemsDirty(entt::entity entity);
  void MarkAttributesDirty(entt::entity entity);
  void SweepExpiredListings();
  void SweepExpiredListingsPersistent();
  void RecoverPendingReturnsForCharacter(uint64_t character_id);
  bool PersistenceEnabled() const;
  void BootstrapPersistence();

  ResponseSender& response_sender_;
  ClientRegistry& client_registry_;
  entt::registry& ecs_registry_;
  RoleStore* role_store_ = nullptr;
  std::shared_ptr<mir2::db::PgConnectionPool> db_pool_;
  bool persistence_bootstrapped_ = false;

  uint64_t next_listing_id_ = 1;
  std::unordered_map<uint64_t, ListingState> listings_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLERS_AUCTION_AUCTION_HANDLER_H_
