/**
 * @file trade_handler.h
 * @brief Trade handler for logic layer.
 */

#ifndef MIR2_LOGIC_HANDLERS_TRADE_TRADE_HANDLER_H_
#define MIR2_LOGIC_HANDLERS_TRADE_TRADE_HANDLER_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <entt/entt.hpp>

#include "logic/handler_context.h"
#include "logic/task.h"
#include "server/common/error_codes.h"

namespace mir2::ecs {
struct TradeComponent;
}  // namespace mir2::ecs

namespace mir2::proto {
class TradeReq;
class TradeAddItemReq;
class TradeSetGoldReq;
class TradeConfirmReq;
class TradeCancelReq;
}  // namespace mir2::proto

namespace mir2::logic {

class ClientRegistry;
class EcsInventoryService;
class ResponseSender;
class RoleStore;

class TradeHandler {
 public:
  TradeHandler(ResponseSender& response_sender,
               ClientRegistry& client_registry,
               entt::registry& ecs_registry,
               RoleStore* role_store,
               EcsInventoryService* inventory_service);

  Task<void> HandleMessage(HandlerContext ctx,
                           const uint8_t* payload,
                           size_t payload_size);

 private:
  struct TradeItemView {
    uint16_t inventory_slot = 0;
    uint32_t item_id = 0;
    uint32_t count = 0;
  };

  struct TradeRuntime {
    entt::entity self = entt::null;
    entt::entity partner = entt::null;
    uint64_t self_character_id = 0;
    uint64_t partner_character_id = 0;
    uint64_t self_client_id = 0;
    uint64_t partner_client_id = 0;
    uint64_t trade_id = 0;
    bool self_confirmed = false;
    bool partner_confirmed = false;
    uint32_t self_gold = 0;
    uint32_t partner_gold = 0;
    std::vector<TradeItemView> self_items;
    std::vector<TradeItemView> partner_items;
  };

  Task<void> HandleTradeRequest(HandlerContext ctx,
                                const mir2::proto::TradeReq* req);
  Task<void> HandleAddItem(HandlerContext ctx,
                           const mir2::proto::TradeAddItemReq* req);
  Task<void> HandleSetGold(HandlerContext ctx,
                           const mir2::proto::TradeSetGoldReq* req);
  Task<void> HandleConfirm(HandlerContext ctx,
                           const mir2::proto::TradeConfirmReq* req);
  Task<void> HandleCancel(HandlerContext ctx,
                          const mir2::proto::TradeCancelReq* req);

  Task<void> SendTradeRsp(uint64_t client_id,
                          bool success,
                          mir2::common::ErrorCode code,
                          uint64_t trade_id);
  Task<void> SendTradeAddItemRsp(uint64_t client_id,
                                 bool success,
                                 mir2::common::ErrorCode code);
  Task<void> SendTradeSetGoldRsp(uint64_t client_id,
                                 bool success,
                                 mir2::common::ErrorCode code);
  Task<void> SendTradeConfirmRsp(uint64_t client_id,
                                 bool success,
                                 mir2::common::ErrorCode code);
  Task<void> SendTradeCancelRsp(uint64_t client_id,
                                bool success,
                                mir2::common::ErrorCode code);
  Task<void> SendTradeUpdate(const TradeRuntime& runtime);
  Task<void> SendTradeComplete(const TradeRuntime& runtime,
                               bool success,
                               mir2::common::ErrorCode code);

  std::optional<TradeRuntime> ResolveRuntime(entt::entity self) const;
  std::optional<entt::entity> FindOnlineEntityByCharacterId(
      uint64_t character_id) const;
  std::optional<uint64_t> GetCharacterId(entt::entity entity) const;
  std::optional<uint64_t> GetClientIdByCharacterId(uint64_t character_id) const;
  std::vector<TradeItemView> CollectTradeItems(entt::entity owner,
                                               const mir2::ecs::TradeComponent& trade) const;
  entt::entity FindInventoryItem(entt::entity owner,
                                 uint16_t slot,
                                 uint32_t item_id,
                                 uint32_t count) const;
  void MarkTradeClosed(entt::entity entity) const;

  ResponseSender& response_sender_;
  ClientRegistry& client_registry_;
  entt::registry& ecs_registry_;
  RoleStore* role_store_ = nullptr;
  EcsInventoryService* inventory_service_ = nullptr;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLERS_TRADE_TRADE_HANDLER_H_
