/**
 * @file item_handler.h
 * @brief 物品相关处理
 */

#ifndef LEGEND2_SERVER_HANDLERS_ITEM_HANDLER_H
#define LEGEND2_SERVER_HANDLERS_ITEM_HANDLER_H

#include "handlers/base_handler.h"

namespace legend2::handlers {

/**
 * @brief 使用物品结果
 */
struct ItemUseResult {
    mir2::common::ErrorCode code = mir2::common::ErrorCode::kOk;
    uint16_t slot = 0;
    uint32_t item_id = 0;
    uint32_t remaining = 0;
};

/**
 * @brief 丢弃物品结果
 */
struct ItemDropResult {
    mir2::common::ErrorCode code = mir2::common::ErrorCode::kOk;
    uint32_t item_id = 0;
    uint32_t count = 0;
};

/**
 * @brief 拾取物品结果
 */
struct ItemPickupResult {
    mir2::common::ErrorCode code = mir2::common::ErrorCode::kOk;
    uint32_t item_id = 0;
};

/**
 * @brief 背包服务接口
 */
class InventoryService {
public:
    virtual ~InventoryService() = default;
    virtual ItemPickupResult PickupItem(uint64_t character_id, uint32_t item_id) = 0;
    virtual ItemUseResult UseItem(uint64_t character_id, uint16_t slot, uint32_t item_id) = 0;
    virtual ItemDropResult DropItem(uint64_t character_id, uint16_t slot, uint32_t item_id, uint32_t count) = 0;
};

/**
 * @brief 物品Handler
 */
class ItemHandler : public BaseHandler {
public:
    explicit ItemHandler(InventoryService& service);

protected:
    void DoHandle(const HandlerContext& context,
                  uint16_t msg_id,
                  const std::vector<uint8_t>& payload,
                  ResponseCallback callback) override;

    void OnError(const HandlerContext& context,
                 uint16_t msg_id,
                 mir2::common::ErrorCode error_code,
                 ResponseCallback callback) override;

private:
    void HandlePickup(const HandlerContext& context,
                      const std::vector<uint8_t>& payload,
                      ResponseCallback callback);
    void HandleUse(const HandlerContext& context,
                   const std::vector<uint8_t>& payload,
                   ResponseCallback callback);
    void HandleDrop(const HandlerContext& context,
                    const std::vector<uint8_t>& payload,
                    ResponseCallback callback);

    InventoryService& service_;
};

}  // namespace legend2::handlers

#endif  // LEGEND2_SERVER_HANDLERS_ITEM_HANDLER_H
