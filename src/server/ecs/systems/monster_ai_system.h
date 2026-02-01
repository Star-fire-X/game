/**
 * @file monster_ai_system.h
 * @brief 怪物AI系统
 *
 * 管理怪物的AI状态机、仇恨系统和行为逻辑
 */

#ifndef MIR2_ECS_SYSTEMS_MONSTER_AI_SYSTEM_H
#define MIR2_ECS_SYSTEMS_MONSTER_AI_SYSTEM_H

#include <entt/entt.hpp>

#include <functional>

#include "ecs/systems/combat_system.h"

namespace mir2::ecs {

class EventBus;

/**
 * @brief 怪物AI系统
 *
 * 负责更新怪物的AI状态机，包括：
 * - 空闲、巡逻、追击、攻击、返回状态转移
 * - 仇恨管理和目标选择
 * - 与战斗系统集成
 */
class MonsterAISystem {
public:
    MonsterAISystem();
    explicit MonsterAISystem(entt::registry& registry, EventBus& event_bus);
    ~MonsterAISystem();

    /**
     * @brief 系统更新
     * @param registry ECS注册表
     * @param dt 时间增量（秒）
     */
    void Update(entt::registry& registry, float dt);

    /**
     * @brief 处理怪物受到伤害事件
     * @param monster 怪物实体
     * @param attacker 攻击者实体
     * @param damage 伤害值
     */
    void OnMonsterDamaged(entt::entity monster, entt::entity attacker, int32_t damage);

private:
    entt::registry* registry_ = nullptr;
    EventBus* event_bus_ = nullptr;

    using AttackBehavior = std::function<void(entt::registry&, entt::entity, float)>;  // 特殊攻击行为回调

    // 状态更新方法
    void UpdateStateMachine(entt::registry& registry, entt::entity entity, float dt);
    void UpdateIdle(entt::registry& registry, entt::entity entity, float dt);
    void UpdatePatrol(entt::registry& registry, entt::entity entity, float dt);
    void UpdateChase(entt::registry& registry, entt::entity entity, float dt);
    void UpdateAttack(entt::registry& registry, entt::entity entity, float dt);
    void UpdateReturn(entt::registry& registry, entt::entity entity, float dt);
    void UpdateSpecialAI(entt::registry& registry, entt::entity entity, float dt,
                         const AttackBehavior& attack_behavior);  // 通用特殊AI处理

    // 特殊AI更新方法
    void UpdateAmbushAI(entt::registry& registry, entt::entity entity, float dt);
    void UpdateRangedAI(entt::registry& registry, entt::entity entity, float dt);
    void UpdateSummonerAI(entt::registry& registry, entt::entity entity, float dt);
    void UpdateExplosiveAI(entt::registry& registry, entt::entity entity, float dt);
    void UpdatePoisonousAI(entt::registry& registry, entt::entity entity, float dt);
    void UpdateGuardAI(entt::registry& registry, entt::entity entity, float dt);
    void UpdateBossCowKingAI(entt::registry& registry, entt::entity entity, float dt);

    // 辅助方法
    void TransitionToState(entt::registry& registry, entt::entity entity, 
                          int new_state);
    entt::entity SelectTarget(entt::registry& registry, entt::entity monster);
    bool IsTargetValid(entt::registry& registry, entt::entity target);
    float GetDistance(entt::registry& registry, entt::entity a, entt::entity b);

    legend2::CombatConfig combat_config_{};
};

}  // namespace mir2::ecs

#endif  // MIR2_ECS_SYSTEMS_MONSTER_AI_SYSTEM_H
