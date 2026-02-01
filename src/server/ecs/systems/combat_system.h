/**
 * @file combat_system.h
 * @brief ECS 战斗逻辑系统
 */

#ifndef LEGEND2_SERVER_ECS_SYSTEMS_COMBAT_SYSTEM_H
#define LEGEND2_SERVER_ECS_SYSTEMS_COMBAT_SYSTEM_H

#include "server/combat/combat_core.h"
#include "ecs/components/character_components.h"
#include "ecs/components/combat_component.h"
#include "ecs/world.h"

#include <utility>

namespace mir2::ecs {

class EventBus;

/**
 * @brief 角色战斗逻辑系统
 */
class CombatSystem : public System {
 public:
    CombatSystem();

    void Update(entt::registry& registry, float delta_time) override;

    /// 角色受伤，返回实际伤害值
    static int TakeDamage(entt::registry& registry, entt::entity entity, int damage,
                          EventBus* event_bus = nullptr);
    /// 计算并应用伤害，返回伤害计算结果
    static legend2::DamageResult TakeDamageWithCalc(entt::registry& registry,
                                                    entt::entity attacker,
                                                    entt::entity target,
                                                    const legend2::CombatConfig& config,
                                                    EventBus* event_bus = nullptr);
    /// 治疗角色，返回实际治疗量
    static int Heal(entt::registry& registry, entt::entity entity, int amount);
    /// 恢复MP，返回实际恢复量
    static int RestoreMP(entt::registry& registry, entt::entity entity, int amount);
    /// 消耗MP，返回是否成功
    static bool ConsumeMP(entt::registry& registry, entt::entity entity, int amount);
    /// 角色死亡
    static void Die(entt::registry& registry, entt::entity entity,
                    EventBus* event_bus = nullptr);
    /// 角色复活
    static void Respawn(entt::registry& registry, entt::entity entity,
                        const mir2::common::Position& pos, float hp_percent = 1.0f,
                        float mp_percent = 1.0f, EventBus* event_bus = nullptr);

    /// 执行攻击（范围检测 + 伤害计算）
    static legend2::AttackResult ExecuteAttack(entt::registry& registry,
                                               entt::entity attacker,
                                               entt::entity target,
                                               const legend2::CombatConfig& config,
                                               EventBus* event_bus = nullptr);
    /// 执行带攻击类型的攻击（支持AOE与多段攻击）
    static legend2::AttackResult ProcessAttackWithType(entt::registry& registry,
                                                       entt::entity attacker,
                                                       entt::entity target,
                                                       const legend2::CombatConfig& config,
                                                       mir2::common::AttackType attack_type,
                                                       EventBus* event_bus = nullptr);

 private:
    using CombatGroup = decltype(std::declval<entt::registry&>().group<CombatComponent>(
        entt::get<CharacterAttributesComponent, CharacterStateComponent>));

    // 缓存的 group：频繁遍历的热路径使用 group，组件连续存储，缓存更友好。
    // 单线程模型下安全使用（Update 只在主线程调用）。
    CombatGroup combat_group_;
};

}  // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_SYSTEMS_COMBAT_SYSTEM_H
