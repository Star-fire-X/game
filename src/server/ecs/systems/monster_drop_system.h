/**
 * @file monster_drop_system.h
 * @brief 怪物掉落系统
 */

#ifndef MIR2_ECS_SYSTEMS_MONSTER_DROP_SYSTEM_H_
#define MIR2_ECS_SYSTEMS_MONSTER_DROP_SYSTEM_H_

#include <entt/entt.hpp>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include "game/entity/monster_drop_config.h"
#include "ecs/components/party_component.h"

namespace mir2::ecs {

class EventBus;

/**
 * @brief 怪物掉落系统
 */
class MonsterDropSystem {
public:
    /// 伤害贡献记录
    using DamageContributors = std::unordered_map<entt::entity, int32_t>;

    MonsterDropSystem();
    explicit MonsterDropSystem(entt::registry& registry, EventBus& event_bus);
    ~MonsterDropSystem();

    void OnMonsterDeath(entt::entity monster, entt::entity killer);
    void OnMonsterDeath(entt::entity monster, entt::entity killer,
                        const DamageContributors& damage_contributors);
    void LoadDropTables(const std::string& config_path);
    void SubscribeToDeathEvents();

private:
    entt::registry* registry_ = nullptr;
    EventBus* event_bus_ = nullptr;
    std::unordered_map<uint32_t, game::entity::MonsterDropTable> drop_tables_;
    uint32_t cached_loot_map_id_ = 1;

    std::vector<game::entity::DropItem> SelectDropItems(
        const game::entity::MonsterDropTable& table);
    std::vector<game::entity::DropItem> SelectDropItemsForBoss(
        const game::entity::MonsterDropTable& table);
    void CreateLootEntity(entt::registry& registry,
                         const game::entity::DropItem& item,
                         int32_t x, int32_t y,
                         entt::entity owner = entt::null);

    /// 获取小队内在范围内的成员
    std::vector<entt::entity> GetPartyMembersInRange(
        entt::entity player, int32_t x, int32_t y, int32_t range);

    /// 根据掉落模式分配物品
    entt::entity SelectLootOwner(const std::vector<entt::entity>& candidates,
                                  const DamageContributors& damage_contributors,
                                  LootMode mode);
};

}  // namespace mir2::ecs

#endif  // MIR2_ECS_SYSTEMS_MONSTER_DROP_SYSTEM_H_
