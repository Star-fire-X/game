/**
 * @file item_handler.h
 * @brief Item handler for logic layer.
 */

#ifndef MIR2_LOGIC_HANDLERS_ITEM_ITEM_HANDLER_H_
#define MIR2_LOGIC_HANDLERS_ITEM_ITEM_HANDLER_H_

#include <cstddef>
#include <cstdint>

#include "logic/handler_context.h"
#include "logic/task.h"
#include "server/common/error_codes.h"

namespace mir2::proto {
class PickupItemReq;
class UseItemReq;
class DropItemReq;
class EquipReq;
class UnequipReq;
}  // namespace mir2::proto

namespace mir2::logic {

class CoroutineExecutor;
class ResponseSender;

/**
 * @brief Use item result
 */
struct ItemUseResult {
  mir2::common::ErrorCode code = mir2::common::ErrorCode::kOk;
  uint16_t slot = 0;
  uint32_t item_id = 0;
  uint32_t remaining = 0;
};

/**
 * @brief Drop item result
 */
struct ItemDropResult {
  mir2::common::ErrorCode code = mir2::common::ErrorCode::kOk;
  uint32_t item_id = 0;
  uint32_t count = 0;
};

/**
 * @brief Equip item result
 */
struct ItemEquipResult {
  mir2::common::ErrorCode code = mir2::common::ErrorCode::kOk;
  uint16_t slot = 0;
  uint32_t item_id = 0;
};

/**
 * @brief Unequip item result
 */
struct ItemUnequipResult {
  mir2::common::ErrorCode code = mir2::common::ErrorCode::kOk;
  uint16_t slot = 0;
  uint32_t item_id = 0;
};

/**
 * @brief Pickup item result
 */
struct ItemPickupResult {
  mir2::common::ErrorCode code = mir2::common::ErrorCode::kOk;
  uint32_t item_id = 0;
};

/**
 * @brief Inventory service interface
 */
class InventoryService {
 public:
  virtual ~InventoryService() = default;
  virtual ItemPickupResult PickupItem(uint64_t character_id, uint32_t item_id) = 0;
  virtual ItemUseResult UseItem(uint64_t character_id, uint16_t slot, uint32_t item_id) = 0;
  virtual ItemDropResult DropItem(uint64_t character_id,
                                  uint16_t slot,
                                  uint32_t item_id,
                                  uint32_t count) = 0;
  virtual ItemEquipResult EquipItem(uint64_t character_id,
                                    uint16_t slot,
                                    uint32_t item_id) = 0;
  virtual ItemUnequipResult UnequipItem(uint64_t character_id, uint16_t slot) = 0;
};

class ItemHandler {
 public:
  ItemHandler(CoroutineExecutor& executor,
              ResponseSender& response_sender,
              InventoryService& service);

  Task<void> HandleMessage(HandlerContext ctx, const uint8_t* payload, size_t payload_size);

 private:
  Task<void> HandlePickup(HandlerContext ctx, const mir2::proto::PickupItemReq* req);
  Task<void> HandleUse(HandlerContext ctx, const mir2::proto::UseItemReq* req);
  Task<void> HandleDrop(HandlerContext ctx, const mir2::proto::DropItemReq* req);
  Task<void> HandleEquip(HandlerContext ctx, const mir2::proto::EquipReq* req);
  Task<void> HandleUnequip(HandlerContext ctx, const mir2::proto::UnequipReq* req);
  Task<void> SendPickupError(HandlerContext ctx, mir2::common::ErrorCode code);
  Task<void> SendUseError(HandlerContext ctx, mir2::common::ErrorCode code);
  Task<void> SendDropError(HandlerContext ctx, mir2::common::ErrorCode code);
  Task<void> SendEquipError(HandlerContext ctx, mir2::common::ErrorCode code);
  Task<void> SendUnequipError(HandlerContext ctx, mir2::common::ErrorCode code);

  CoroutineExecutor& executor_;
  ResponseSender& response_sender_;
  InventoryService& service_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLERS_ITEM_ITEM_HANDLER_H_
