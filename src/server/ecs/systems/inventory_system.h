/**
 * @file inventory_system.h
 * @brief ECS 物品/装备/技能逻辑系统
 */

#ifndef LEGEND2_SERVER_ECS_SYSTEMS_INVENTORY_SYSTEM_H
#define LEGEND2_SERVER_ECS_SYSTEMS_INVENTORY_SYSTEM_H

#include <optional>

#include <entt/entt.hpp>

#include "ecs/components/equipment_component.h"
#include "ecs/components/item_component.h"
#include "ecs/components/skill_component.h"
#include "ecs/world.h"

namespace mir2::ecs {

class EventBus;

/**
 * @brief 角色物品与技能管理系统
 */
class InventorySystem : public System {
 public:
    InventorySystem();

    void Update(entt::registry& registry, float delta_time) override;

    // 添加物品到背包
    static std::optional<entt::entity> AddItem(
        entt::registry& registry, entt::entity character,
        uint32_t item_id, int count, EventBus* event_bus = nullptr);

    // 统计背包/装备中指定物品的总数量
    static int CountItem(entt::registry& registry,
                         entt::entity character,
                         uint32_t item_id);

    // 检查背包/装备中是否存在指定数量的物品
    static bool HasItem(entt::registry& registry,
                        entt::entity character,
                        uint32_t item_id,
                        int count);

    // 装备物品
    static bool EquipItem(entt::registry& registry,
                         entt::entity character, entt::entity item,
                         EventBus* event_bus = nullptr);

    // 卸下装备
    static bool UnequipItem(entt::registry& registry,
                            entt::entity character, int slot_index,
                            EventBus* event_bus = nullptr);

    // 使用物品
    static bool UseItem(entt::registry& registry,
                        entt::entity character, entt::entity item,
                        int count = 1, EventBus* event_bus = nullptr);

    // 丢弃物品
    static bool DropItem(entt::registry& registry,
                         entt::entity character, entt::entity item,
                         EventBus* event_bus = nullptr);

    // 拾取物品
    static bool PickupItem(entt::registry& registry,
                           entt::entity character, entt::entity ground_item,
                           EventBus* event_bus = nullptr);

    // 学习技能
    static std::optional<entt::entity> LearnSkill(
        entt::registry& registry, entt::entity character,
        uint32_t skill_id, int level = 1, EventBus* event_bus = nullptr);

    // 升级技能
    static bool UpgradeSkill(entt::registry& registry,
                             entt::entity character, uint32_t skill_id,
                             int levels = 1, EventBus* event_bus = nullptr);

 private:
    static int FindFreeSlot(entt::registry& registry, entt::entity character);
    static entt::entity FindItemInSlot(entt::registry& registry,
                                       entt::entity character, int slot_index);
    static entt::entity FindSkill(entt::registry& registry,
                                  entt::entity character, uint32_t skill_id);
};

}  // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_SYSTEMS_INVENTORY_SYSTEM_H
