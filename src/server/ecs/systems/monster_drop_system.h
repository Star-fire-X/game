/**
 * @file monster_drop_system.h
 * @brief 怪物掉落系统
 */

#ifndef MIR2_ECS_SYSTEMS_MONSTER_DROP_SYSTEM_H
#define MIR2_ECS_SYSTEMS_MONSTER_DROP_SYSTEM_H

#include <entt/entt.hpp>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include "game/entity/monster_drop_config.h"

namespace mir2::ecs {

class EventBus;

/**
 * @brief 怪物掉落系统
 */
class MonsterDropSystem {
public:
    MonsterDropSystem();
    explicit MonsterDropSystem(entt::registry& registry, EventBus& event_bus);
    ~MonsterDropSystem();

    void OnMonsterDeath(entt::entity monster, entt::entity killer);
    void LoadDropTables(const std::string& config_path);

    // 订阅EntityDeathEvent事件
    void SubscribeToDeathEvents();

private:
    entt::registry* registry_ = nullptr;   ///< 缓存registry以供死亡回调使用
    EventBus* event_bus_ = nullptr;
    std::unordered_map<uint32_t, game::entity::MonsterDropTable> drop_tables_;
    uint32_t cached_loot_map_id_ = 1;      ///< 缓存掉落地图ID以创建地面物品

    std::vector<game::entity::DropItem> SelectDropItems(
        const game::entity::MonsterDropTable& table);
    void CreateLootEntity(entt::registry& registry,
                         const game::entity::DropItem& item,
                         int32_t x, int32_t y);
};

}  // namespace mir2::ecs

#endif  // MIR2_ECS_SYSTEMS_MONSTER_DROP_SYSTEM_H
