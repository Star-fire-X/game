/**
 * @file monster_ai_system.cc
 * @brief 怪物AI系统实现
 */

#include "ecs/systems/monster_ai_system.h"
#include "ecs/components/character_components.h"
#include "ecs/components/monster_component.h"
#include "ecs/components/transform_component.h"
#include "ecs/event_bus.h"
#include "ecs/events/monster_events.h"
#include "game/entity/monster.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace mir2::ecs {

namespace {

constexpr float kRangedTooCloseFactor = 0.7f;
constexpr float kSummonIntervalSeconds = 6.0f;
constexpr float kGuardRadius = 10.0f;
constexpr float kGuardLeash = 12.0f;
constexpr float kMaxChaseDistance = 15.0f;
constexpr float kIdleToPatrolTime = 2.0f;
constexpr float kPatrolToIdleTime = 3.0f;
constexpr float kReturnToIdleTime = 1.0f;

float get_distance_to_position(entt::registry& registry,
                               entt::entity entity,
                               const mir2::common::Position& position) {
    auto* transform = registry.try_get<TransformComponent>(entity);
    if (!transform) {
        return 999999.0f;
    }

    int dx = transform->x - position.x;
    int dy = transform->y - position.y;
    return std::sqrt(static_cast<float>(dx * dx + dy * dy));
}

}  // namespace

MonsterAISystem::MonsterAISystem() = default;

MonsterAISystem::MonsterAISystem(entt::registry& registry, EventBus& event_bus)
    : registry_(&registry),
      event_bus_(&event_bus) {}

MonsterAISystem::~MonsterAISystem() = default;

void MonsterAISystem::Update(entt::registry& registry, float dt) {
    // 遍历所有拥有AI组件的怪物
    auto view = registry.view<MonsterAIComponent, MonsterAggroComponent>();
    
    for (auto entity : view) {
        auto& ai = view.get<MonsterAIComponent>(entity);
        auto& aggro = view.get<MonsterAggroComponent>(entity);
        
        // 仇恨衰减
        aggro.DecayHatred(dt);

        // 更新攻击冷却计时器
        ai.attack_cooldown_timer += dt;

        // 根据AI类型分发
        switch (ai.ai_type) {
            case MonsterAIType::kNormal:
                UpdateStateMachine(registry, entity, dt);
                break;
            case MonsterAIType::kAmbush:
                UpdateAmbushAI(registry, entity, dt);
                break;
            case MonsterAIType::kRanged:
                UpdateRangedAI(registry, entity, dt);
                break;
            case MonsterAIType::kSummoner:
                UpdateSummonerAI(registry, entity, dt);
                break;
            case MonsterAIType::kExplosive:
                UpdateExplosiveAI(registry, entity, dt);
                break;
            case MonsterAIType::kPoisonous:
                UpdatePoisonousAI(registry, entity, dt);
                break;
            case MonsterAIType::kGuard:
                UpdateGuardAI(registry, entity, dt);
                break;
            case MonsterAIType::kBossCowKing:
                UpdateBossCowKingAI(registry, entity, dt);
                break;
            default:
                UpdateStateMachine(registry, entity, dt);
                break;
        }
        
        ai.state_timer += dt;
    }
}

void MonsterAISystem::OnMonsterDamaged(entt::entity monster, entt::entity attacker, 
                                       int32_t damage) {
    if (!registry_ || monster == entt::null || attacker == entt::null) {
        return;
    }

    auto* aggro = registry_->try_get<MonsterAggroComponent>(monster);
    if (!aggro) {
        return;
    }

    aggro->AddHatred(attacker, damage);
}

void MonsterAISystem::UpdateStateMachine(entt::registry& registry,
                                         entt::entity entity,
                                         float dt) {
    auto& ai = registry.get<MonsterAIComponent>(entity);
    switch (ai.current_state) {
        case game::entity::MonsterState::kIdle:
            UpdateIdle(registry, entity, dt);
            break;
        case game::entity::MonsterState::kPatrol:
            UpdatePatrol(registry, entity, dt);
            break;
        case game::entity::MonsterState::kChase:
            UpdateChase(registry, entity, dt);
            break;
        case game::entity::MonsterState::kAttack:
            UpdateAttack(registry, entity, dt);
            break;
        case game::entity::MonsterState::kReturn:
            UpdateReturn(registry, entity, dt);
            break;
        default:
            break;
    }
}

void MonsterAISystem::UpdateIdle(entt::registry& registry, entt::entity entity, float dt) {
    auto& ai = registry.get<MonsterAIComponent>(entity);
    auto& aggro = registry.get<MonsterAggroComponent>(entity);
    
    // 检查是否有仇恨目标
    entt::entity target = SelectTarget(registry, entity);
    if (target != entt::null) {
        ai.target_entity = target;
        TransitionToState(registry, entity, static_cast<int>(game::entity::MonsterState::kChase));
        return;
    }
    
    // 空闲一段时间后进入巡逻
    if (ai.state_timer > kIdleToPatrolTime) {
        TransitionToState(registry, entity, static_cast<int>(game::entity::MonsterState::kPatrol));
    }
}

void MonsterAISystem::UpdatePatrol(entt::registry& registry, entt::entity entity, float dt) {
    auto& ai = registry.get<MonsterAIComponent>(entity);
    
    // 检查是否有仇恨目标
    entt::entity target = SelectTarget(registry, entity);
    if (target != entt::null) {
        ai.target_entity = target;
        TransitionToState(registry, entity, static_cast<int>(game::entity::MonsterState::kChase));
        return;
    }
    
    // 简单巡逻逻辑：定时切换回空闲
    if (ai.state_timer > kPatrolToIdleTime) {
        TransitionToState(registry, entity, static_cast<int>(game::entity::MonsterState::kIdle));
    }
}

void MonsterAISystem::UpdateChase(entt::registry& registry, entt::entity entity, float dt) {
    auto& ai = registry.get<MonsterAIComponent>(entity);
    auto& aggro = registry.get<MonsterAggroComponent>(entity);
    
    // 验证目标有效性
    if (!IsTargetValid(registry, ai.target_entity)) {
        ai.target_entity = SelectTarget(registry, entity);
        if (ai.target_entity == entt::null) {
            TransitionToState(registry, entity, static_cast<int>(game::entity::MonsterState::kReturn));
            return;
        }
    }
    
    // 检查是否进入攻击范围
    float distance = GetDistance(registry, entity, ai.target_entity);
    if (distance <= aggro.attack_range) {
        TransitionToState(registry, entity, static_cast<int>(game::entity::MonsterState::kAttack));
        return;
    }
    
    // 检查是否超出追击范围
    if (distance > kMaxChaseDistance) {
        TransitionToState(registry, entity, static_cast<int>(game::entity::MonsterState::kReturn));
    }
}

void MonsterAISystem::UpdateAttack(entt::registry& registry, entt::entity entity, float dt) {
    auto& ai = registry.get<MonsterAIComponent>(entity);
    auto& aggro = registry.get<MonsterAggroComponent>(entity);
    
    // 验证目标
    if (!IsTargetValid(registry, ai.target_entity)) {
        TransitionToState(registry, entity, static_cast<int>(game::entity::MonsterState::kChase));
        return;
    }
    
    // 检查攻击冷却
    if (ai.attack_cooldown_timer >= ai.attack_cooldown) {
        ai.attack_cooldown_timer = 0.0f;
        // 调用CombatSystem执行攻击
        CombatSystem::ExecuteAttack(registry, entity, ai.target_entity,
                                    combat_config_, event_bus_);
    }
    
    // 检查目标是否脱离攻击范围
    float distance = GetDistance(registry, entity, ai.target_entity);
    if (distance > aggro.attack_range) {
        TransitionToState(registry, entity, static_cast<int>(game::entity::MonsterState::kChase));
    }
}

void MonsterAISystem::UpdateReturn(entt::registry& registry, entt::entity entity, float dt) {
    auto& ai = registry.get<MonsterAIComponent>(entity);
    auto& aggro = registry.get<MonsterAggroComponent>(entity);
    
    // 清除仇恨
    aggro.Clear();
    ai.target_entity = entt::null;
    
    // 简化返回逻辑：直接切换到空闲
    if (ai.state_timer > kReturnToIdleTime) {
        TransitionToState(registry, entity, static_cast<int>(game::entity::MonsterState::kIdle));
    }
}

void MonsterAISystem::UpdateSpecialAI(entt::registry& registry, entt::entity entity, float dt,
                                      const AttackBehavior& attack_behavior) {
    auto& ai = registry.get<MonsterAIComponent>(entity);
    if (ai.current_state == game::entity::MonsterState::kAttack) {
        // 攻击状态由特定AI行为处理
        attack_behavior(registry, entity, dt);
        return;
    }

    // 其余状态使用标准状态机
    UpdateStateMachine(registry, entity, dt);
}

void MonsterAISystem::UpdateAmbushAI(entt::registry& registry, entt::entity entity, float dt) {
    auto& ai = registry.get<MonsterAIComponent>(entity);
    auto& aggro = registry.get<MonsterAggroComponent>(entity);

    // 伏击状态：隐藏等待目标靠近
    if (ai.is_hidden) {
        if (ai.current_state != game::entity::MonsterState::kIdle) {
            TransitionToState(registry, entity, static_cast<int>(game::entity::MonsterState::kIdle));
        }

        entt::entity target = SelectTarget(registry, entity);
        if (target == entt::null) {
            return;
        }

        float distance = GetDistance(registry, entity, target);
        if (distance <= static_cast<float>(aggro.aggro_range)) {
            ai.is_hidden = false;
            ai.target_entity = target;
            if (distance <= static_cast<float>(aggro.attack_range)) {
                TransitionToState(registry, entity, static_cast<int>(game::entity::MonsterState::kAttack));
            } else {
                TransitionToState(registry, entity, static_cast<int>(game::entity::MonsterState::kChase));
            }
        }
        return;
    }

    UpdateStateMachine(registry, entity, dt);
}

void MonsterAISystem::UpdateRangedAI(entt::registry& registry, entt::entity entity, float dt) {
    // 复用通用状态机，仅自定义攻击行为
    UpdateSpecialAI(registry, entity, dt,
        [this](entt::registry& registry, entt::entity entity, [[maybe_unused]] float dt) {
            auto& ai = registry.get<MonsterAIComponent>(entity);
            auto& aggro = registry.get<MonsterAggroComponent>(entity);

            float desired_distance = ai.preferred_distance > 0.0f
                ? ai.preferred_distance
                : static_cast<float>(aggro.attack_range);
            if (desired_distance <= 0.0f) {
                desired_distance = 1.0f;
            }
            const float too_close_distance = desired_distance * kRangedTooCloseFactor;

            if (!IsTargetValid(registry, ai.target_entity)) {
                TransitionToState(registry, entity,
                                  static_cast<int>(game::entity::MonsterState::kChase));
                return;
            }

            float distance = GetDistance(registry, entity, ai.target_entity);
            if (distance > desired_distance || distance < too_close_distance) {
                TransitionToState(registry, entity,
                                  static_cast<int>(game::entity::MonsterState::kChase));
                return;
            }

            if (ai.attack_cooldown_timer >= ai.attack_cooldown) {
                ai.attack_cooldown_timer = 0.0f;
                // 远程攻击
                CombatSystem::ExecuteAttack(registry, entity, ai.target_entity,
                                            combat_config_, event_bus_);
            }
        });
}

void MonsterAISystem::UpdateSummonerAI(entt::registry& registry,
                                       entt::entity entity,
                                       float dt) {
    // 复用通用状态机，仅自定义攻击行为
    UpdateSpecialAI(registry, entity, dt,
        [this](entt::registry& registry, entt::entity entity, [[maybe_unused]] float dt) {
            auto& ai = registry.get<MonsterAIComponent>(entity);
            auto& aggro = registry.get<MonsterAggroComponent>(entity);

            if (!IsTargetValid(registry, ai.target_entity)) {
                TransitionToState(registry, entity,
                                  static_cast<int>(game::entity::MonsterState::kChase));
                return;
            }

            float distance = GetDistance(registry, entity, ai.target_entity);
            if (distance > static_cast<float>(aggro.attack_range)) {
                TransitionToState(registry, entity,
                                  static_cast<int>(game::entity::MonsterState::kChase));
                return;
            }

            if (ai.attack_cooldown_timer >= ai.attack_cooldown) {
                ai.attack_cooldown_timer = 0.0f;
                // 召唤者依旧进行攻击
                CombatSystem::ExecuteAttack(registry, entity, ai.target_entity,
                                            combat_config_, event_bus_);
            }

            if (ai.state_timer >= kSummonIntervalSeconds && event_bus_) {
                // 触发召唤事件（具体召唤逻辑由事件处理器实现）
                events::MonsterSummonEvent event;
                event.summoner = entity;
                if (auto* transform = registry.try_get<TransformComponent>(entity)) {
                    event.position = {transform->x, transform->y};
                    event.map_id = transform->map_id;
                } else {
                    event.position = ai.return_position;
                }
                event_bus_->Publish(event);
                ai.state_timer = 0.0f;
            }
        });
}

void MonsterAISystem::UpdateExplosiveAI(entt::registry& registry,
                                        entt::entity entity,
                                        float dt) {
    // 复用通用状态机，仅自定义攻击行为
    UpdateSpecialAI(registry, entity, dt,
        [this](entt::registry& registry, entt::entity entity, [[maybe_unused]] float dt) {
            auto& ai = registry.get<MonsterAIComponent>(entity);
            auto& aggro = registry.get<MonsterAggroComponent>(entity);

            if (!IsTargetValid(registry, ai.target_entity)) {
                TransitionToState(registry, entity,
                                  static_cast<int>(game::entity::MonsterState::kChase));
                return;
            }

            float distance = GetDistance(registry, entity, ai.target_entity);
            if (distance > static_cast<float>(aggro.attack_range)) {
                TransitionToState(registry, entity,
                                  static_cast<int>(game::entity::MonsterState::kChase));
                return;
            }

            if (ai.attack_cooldown_timer >= ai.attack_cooldown) {
                ai.attack_cooldown_timer = 0.0f;
                // 自爆攻击：造成伤害后自身死亡
                CombatSystem::ExecuteAttack(registry, entity, ai.target_entity,
                                            combat_config_, event_bus_);
                CombatSystem::Die(registry, entity, event_bus_);
            }
        });
}

void MonsterAISystem::UpdatePoisonousAI(entt::registry& registry,
                                        entt::entity entity,
                                        float dt) {
    // 复用通用状态机，仅自定义攻击行为
    UpdateSpecialAI(registry, entity, dt,
        [this](entt::registry& registry, entt::entity entity, [[maybe_unused]] float dt) {
            auto& ai = registry.get<MonsterAIComponent>(entity);
            auto& aggro = registry.get<MonsterAggroComponent>(entity);

            if (!IsTargetValid(registry, ai.target_entity)) {
                TransitionToState(registry, entity,
                                  static_cast<int>(game::entity::MonsterState::kChase));
                return;
            }

            float distance = GetDistance(registry, entity, ai.target_entity);
            if (distance > static_cast<float>(aggro.attack_range)) {
                TransitionToState(registry, entity,
                                  static_cast<int>(game::entity::MonsterState::kChase));
                return;
            }

            if (ai.attack_cooldown_timer >= ai.attack_cooldown) {
                ai.attack_cooldown_timer = 0.0f;
                // 攻击后追加毒素伤害
                auto result = CombatSystem::ExecuteAttack(
                    registry, entity, ai.target_entity, combat_config_, event_bus_);
                if (result.success && !result.damage.is_miss && !result.target_died) {
                    int poison_damage = std::max(1, result.damage.final_damage / 5);
                    CombatSystem::TakeDamage(registry, ai.target_entity,
                                             poison_damage, event_bus_);
                }
            }
        });
}

void MonsterAISystem::UpdateGuardAI(entt::registry& registry,
                                    entt::entity entity,
                                    float dt) {
    auto& ai = registry.get<MonsterAIComponent>(entity);
    auto& aggro = registry.get<MonsterAggroComponent>(entity);

    // 超出守卫范围时强制返回并清空仇恨
    float distance_to_post = get_distance_to_position(registry, entity, ai.return_position);
    if (distance_to_post > kGuardLeash) {
        aggro.Clear();
        ai.target_entity = entt::null;
        TransitionToState(registry, entity, static_cast<int>(game::entity::MonsterState::kReturn));
        return;
    }

    if (ai.target_entity != entt::null) {
        if (!IsTargetValid(registry, ai.target_entity)) {
            aggro.Clear();
            ai.target_entity = entt::null;
            TransitionToState(registry, entity, static_cast<int>(game::entity::MonsterState::kReturn));
            return;
        }

        float target_distance = get_distance_to_position(
            registry, ai.target_entity, ai.return_position);
        if (target_distance > kGuardRadius) {
            aggro.Clear();
            ai.target_entity = entt::null;
            TransitionToState(registry, entity, static_cast<int>(game::entity::MonsterState::kReturn));
            return;
        }
    }

    if (ai.target_entity == entt::null) {
        entt::entity target = SelectTarget(registry, entity);
        if (target != entt::null) {
            float target_distance = get_distance_to_position(registry, target, ai.return_position);
            if (target_distance <= kGuardRadius) {
                ai.target_entity = target;
                TransitionToState(registry, entity, static_cast<int>(game::entity::MonsterState::kChase));
            }
        }
    }

    UpdateStateMachine(registry, entity, dt);
}

void MonsterAISystem::TransitionToState(entt::registry& registry, entt::entity entity, 
                                       int new_state) {
    auto& ai = registry.get<MonsterAIComponent>(entity);
    ai.current_state = static_cast<game::entity::MonsterState>(new_state);
    ai.state_timer = 0.0f;
}

entt::entity MonsterAISystem::SelectTarget(entt::registry& registry, entt::entity monster) {
    auto& aggro = registry.get<MonsterAggroComponent>(monster);
    
    // 优先选择仇恨值最高的目标
    entt::entity target = aggro.GetTargetByHatred();
    if (target != entt::null && IsTargetValid(registry, target)) {
        return target;
    }
    
    return entt::null;
}

bool MonsterAISystem::IsTargetValid(entt::registry& registry, entt::entity target) {
    if (target == entt::null) return false;
    if (!registry.valid(target)) {
        return false;
    }
    // 目标必须存活（HP > 0）
    auto* attributes = registry.try_get<CharacterAttributesComponent>(target);
    return attributes && attributes->hp > 0;
}

float MonsterAISystem::GetDistance(entt::registry& registry, entt::entity a, entt::entity b) {
    auto* transform_a = registry.try_get<TransformComponent>(a);
    auto* transform_b = registry.try_get<TransformComponent>(b);
    
    if (!transform_a || !transform_b) return 999999.0f;
    
    int dx = transform_a->x - transform_b->x;
    int dy = transform_a->y - transform_b->y;
    return std::sqrt(static_cast<float>(dx * dx + dy * dy));
}

void MonsterAISystem::UpdateBossCowKingAI(entt::registry& registry, entt::entity entity, float dt) {
    auto& ai = registry.get<MonsterAIComponent>(entity);
    auto& aggro = registry.get<MonsterAggroComponent>(entity);
    auto* attributes = registry.try_get<CharacterAttributesComponent>(entity);

    if (!attributes) return;

    // 更新疯狂模式计时器
    if (ai.is_crazy_mode) {
        ai.crazy_mode_timer -= dt;
        if (ai.crazy_mode_timer <= 0.0f) {
            ai.is_crazy_mode = false;
        }
    }

    // 更新瞬移冷却
    if (ai.teleport_cooldown > 0.0f) {
        ai.teleport_cooldown -= dt;
    }

    // 检查HP触发特殊机制
    const float hp_percent = static_cast<float>(attributes->hp) / static_cast<float>(attributes->max_hp);

    // HP低于30%进入疯狂模式
    if (hp_percent < 0.3f && !ai.is_crazy_mode) {
        ai.is_crazy_mode = true;
        ai.crazy_mode_timer = 15.0f; // 持续15秒
        ai.attack_cooldown *= 0.5f;  // 攻击速度翻倍
    }

    // HP低于50%且冷却完成时，有30%概率瞬移
    if (hp_percent < 0.5f && ai.teleport_cooldown <= 0.0f) {
        thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> dist(0, 99);
        if (dist(rng) < 30) {
            // TODO: 实现瞬移逻辑（需要TransformComponent）
            ai.teleport_cooldown = 10.0f; // 10秒冷却
        }
    }

    // 使用普通AI状态机
    UpdateStateMachine(registry, entity, dt);
}

}  // namespace mir2::ecs
