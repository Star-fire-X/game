/**
 * @file level_up_system.h
 * @brief ECS 升级逻辑系统
 */

#ifndef LEGEND2_SERVER_ECS_SYSTEMS_LEVEL_UP_SYSTEM_H
#define LEGEND2_SERVER_ECS_SYSTEMS_LEVEL_UP_SYSTEM_H

#include "ecs/components/character_components.h"
#include "ecs/world.h"

#include <unordered_map>

namespace mir2::ecs {

class EventBus;

/**
 * @brief 角色升级逻辑系统
 */
class LevelUpSystem : public System {
 public:
    LevelUpSystem();

    void Update(entt::registry& registry, float delta_time) override;

    // 获得经验值并自动处理升级
    // @param exp_amount 经验值数量（必须 >= 0）
    // @return 是否发生升级
    static bool GainExperience(entt::registry& registry, entt::entity entity, int exp_amount,
                               EventBus* event_bus = nullptr);

    // 分配经验给召唤兽
    // @param owner 召唤者实体
    // @param exp_amount 总经验值
    // @param event_bus 事件总线
    static void DistributeExpToSummons(entt::registry& registry, entt::entity owner,
                                       int exp_amount, EventBus* event_bus = nullptr);

    // 分配经验给组队成员（预留接口）
    // @param killer 击杀者实体
    // @param exp_amount 总经验值
    // @param event_bus 事件总线
    static void DistributeExpToParty(entt::registry& registry, entt::entity killer,
                                     int exp_amount, EventBus* event_bus = nullptr);

    // 按伤害贡献分配经验
    // @param damage_contributors 伤害贡献者映射 (entity -> damage)
    // @param total_exp 总经验值
    // @param event_bus 事件总线
    static void DistributeExpByDamage(entt::registry& registry,
                                      const std::unordered_map<entt::entity, int>& damage_contributors,
                                      int total_exp, EventBus* event_bus = nullptr);

 private:
    static void ApplyLevelUpStats(mir2::common::CharacterClass char_class,
                                  CharacterAttributesComponent& attributes);

    // 检测并处理升级（支持连续升级）
    static void CheckLevelUp(entt::registry& registry, entt::entity entity,
                             EventBus* event_bus = nullptr);
};

}  // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_SYSTEMS_LEVEL_UP_SYSTEM_H
