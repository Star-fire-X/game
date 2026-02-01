/**
 * @file storage_system.h
 * @brief ECS 仓库系统
 */

#ifndef LEGEND2_SERVER_ECS_SYSTEMS_STORAGE_SYSTEM_H
#define LEGEND2_SERVER_ECS_SYSTEMS_STORAGE_SYSTEM_H

#include <vector>

#include <entt/entt.hpp>

#include "ecs/components/item_component.h"
#include "ecs/components/storage_component.h"
#include "ecs/world.h"

namespace mir2::ecs {
class EventBus;
}  // namespace mir2::ecs

namespace mir2::ecs::events {
struct NpcOpenStorageEvent;
}  // namespace mir2::ecs::events

namespace mir2::ecs {

/**
 * @brief 角色仓库管理系统
 */
class StorageSystem : public System {
 public:
    StorageSystem();
    StorageSystem(entt::registry& registry, EventBus& event_bus);

    void Update(entt::registry& registry, float delta_time) override;

    // 从背包存入仓库
    static bool DepositItem(entt::registry& registry,
                            entt::entity character,
                            entt::entity item,
                            EventBus* event_bus = nullptr);

    // 从仓库取出到背包
    static bool WithdrawItem(entt::registry& registry,
                             entt::entity character,
                             int storage_slot,
                             EventBus* event_bus = nullptr);

    // 获取仓库物品列表
    static std::vector<entt::entity> GetStorageItems(entt::registry& registry,
                                                     entt::entity character);

 private:
    void RegisterHandlers(entt::registry& registry, EventBus& event_bus);
    void HandleOpenStorage(entt::registry& registry,
                           EventBus& event_bus,
                           const events::NpcOpenStorageEvent& event);

    bool handlers_registered_ = false;
};

}  // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_SYSTEMS_STORAGE_SYSTEM_H
