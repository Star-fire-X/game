/**
 * @file level_up_system.h
 * @brief ECS 升级逻辑系统（原版传奇2机制）
 */

#ifndef MIR2_SERVER_ECS_SYSTEMS_LEVEL_UP_SYSTEM_H_
#define MIR2_SERVER_ECS_SYSTEMS_LEVEL_UP_SYSTEM_H_

#include "ecs/components/character_components.h"
#include "ecs/event_bus.h"
#include "ecs/world.h"

#include <unordered_map>

namespace mir2::ecs {
namespace events {
struct EntityDeathEvent;
}  // namespace events

/**
 * @brief 角色升级逻辑系统（原版传奇2机制）
 *
 * 使用硬编码经验表、二次多项式职业成长公式、
 * 组队经验分配、等级差惩罚等原版机制。
 */
class LevelUpSystem : public System {
 public:
    LevelUpSystem();
    explicit LevelUpSystem(entt::registry& registry, EventBus& event_bus);

    void Update(entt::registry& registry, float delta_time) override;

    // 获得经验值并自动处理升级
    // @param exp_amount 经验值数量（必须 >= 0）
    // @return 是否发生升级
    static bool GainExperience(entt::registry& registry, entt::entity entity, int64_t exp_amount,
                               EventBus* event_bus = nullptr);

    // 分配经验给召唤兽
    static void DistributeExpToSummons(entt::registry& registry, entt::entity owner,
                                       int exp_amount, EventBus* event_bus = nullptr);

    // 分配经验给组队成员（实现原版组队经验分配）
    static void DistributeExpToParty(entt::registry& registry, entt::entity killer,
                                     int exp_amount, EventBus* event_bus = nullptr);

    // 按伤害贡献分配经验
    static void DistributeExpByDamage(entt::registry& registry,
                                      const std::unordered_map<entt::entity, int>& damage_contributors,
                                      int total_exp, EventBus* event_bus = nullptr);

    // 升级后重算等级裸装属性
    static void RecalcLevelAbilities(entt::registry& registry, entt::entity entity);

 private:
    void OnEntityDeath(events::EntityDeathEvent& event);

    entt::registry* registry_ = nullptr;
    EventBus* event_bus_ = nullptr;
    EventBus::Subscription death_subscription_;

    // 检测并处理升级（支持连续升级）
    static void CheckLevelUp(entt::registry& registry, entt::entity entity,
                             EventBus* event_bus = nullptr);
};

}  // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_SYSTEMS_LEVEL_UP_SYSTEM_H_
