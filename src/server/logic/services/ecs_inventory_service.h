/**
 * @file ecs_inventory_service.h
 * @brief ECS inventory service adapter.
 */

#ifndef MIR2_LOGIC_SERVICES_ECS_INVENTORY_SERVICE_H_
#define MIR2_LOGIC_SERVICES_ECS_INVENTORY_SERVICE_H_

#include "logic/handlers/item/item_handler.h"

namespace mir2::ecs {
class RegistryManager;
class World;
}  // namespace mir2::ecs

namespace mir2::logic {

class EcsInventoryService : public InventoryService {
 public:
  explicit EcsInventoryService(mir2::ecs::RegistryManager& registry_manager);

  ItemPickupResult PickupItem(uint64_t character_id, uint32_t item_id) override;
  ItemUseResult UseItem(uint64_t character_id, uint16_t slot, uint32_t item_id) override;
  ItemDropResult DropItem(uint64_t character_id,
                          uint16_t slot,
                          uint32_t item_id,
                          uint32_t count) override;

 private:
  mir2::ecs::RegistryManager& registry_manager_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_SERVICES_ECS_INVENTORY_SERVICE_H_
