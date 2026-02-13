/**
 * @file attribute_recalc_system.h
 * @brief 属性重算系统
 *
 * 统一管理角色属性的最终计算，汇总裸装基础、装备加成、
 * 奖励点数分配、buff/debuff 等各层修正。
 */

#ifndef MIR2_SERVER_ECS_SYSTEMS_ATTRIBUTE_RECALC_SYSTEM_H_
#define MIR2_SERVER_ECS_SYSTEMS_ATTRIBUTE_RECALC_SYSTEM_H_

#include "ecs/world.h"
#include <entt/entt.hpp>

namespace mir2::ecs {

class EventBus;

/**
 * @brief 属性重算系统
 *
 * 触发时机：升级、装备变更、奖励点数变更时调用 RecalcAll。
 * 执行顺序在 LevelUpSystem 之后 (kAttributeRecalc = 350)。
 */
class AttributeRecalcSystem : public System {
public:
    AttributeRecalcSystem();
    explicit AttributeRecalcSystem(entt::registry& registry, EventBus& event_bus);

    void Update(entt::registry& registry, float delta_time) override;

    /**
     * @brief 全量重算单个实体的所有属性
     *
     * 1. 裸装基础属性 (GrowthFormulas::CalcLevelAbility)
     * 2. 装备加成 (EquipmentBonusSystem::calculate_total_bonus)
     * 3. 奖励点数加成 (BonusPointComponent)
     * 4. 最终钳制 (hp <= max_hp, mp <= max_mp)
     * 5. 标记脏数据
     */
    static void RecalcAll(entt::registry& registry, entt::entity entity);

private:
    entt::registry* registry_ = nullptr;
    EventBus* event_bus_ = nullptr;
};

}  // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_SYSTEMS_ATTRIBUTE_RECALC_SYSTEM_H_
