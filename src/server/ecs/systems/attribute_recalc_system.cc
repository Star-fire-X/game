#include "ecs/systems/attribute_recalc_system.h"

#include "ecs/components/character_components.h"
#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/attribute_events.h"
#include "ecs/systems/growth_formulas.h"
#include "ecs/systems/equipment_bonus_system.h"

#include <algorithm>

namespace mir2::ecs {

AttributeRecalcSystem::AttributeRecalcSystem()
    : System(SystemPriority::kAttributeRecalc) {}

AttributeRecalcSystem::AttributeRecalcSystem(entt::registry& registry, EventBus& event_bus)
    : System(SystemPriority::kAttributeRecalc),
      registry_(&registry),
      event_bus_(&event_bus) {
    // 订阅升级事件 → 触发重算
    event_bus_->Subscribe<events::LevelUpEvent>(
        [this](events::LevelUpEvent& event) {
            if (registry_) {
                RecalcAll(*registry_, event.entity);
            }
        });
}

void AttributeRecalcSystem::Update(entt::registry& /*registry*/, float /*delta_time*/) {
    // 属性重算不在每帧执行，而是由事件驱动
    // (LevelUpEvent, 装备变更事件, 奖励点数变更事件)
}

void AttributeRecalcSystem::RecalcAll(entt::registry& registry, entt::entity entity) {
    auto* attributes = registry.try_get<CharacterAttributesComponent>(entity);
    auto* identity = registry.try_get<CharacterIdentityComponent>(entity);
    if (!attributes || !identity) {
        return;
    }

    // 1. 裸装基础属性
    NakedAbility base = GrowthFormulas::CalcLevelAbility(
        identity->char_class, attributes->level);

    int final_max_hp = base.max_hp;
    int final_max_mp = base.max_mp;
    int final_attack = base.dc_hi;
    int final_defense = base.ac_hi;
    int final_magic_attack = base.mc_hi;
    int final_magic_defense = base.mac_hi;
    int final_sc = base.sc_hi;
    int final_max_weight = base.max_weight;
    int final_max_wear_weight = base.max_wear_weight;
    int final_max_hand_weight = base.max_hand_weight;

    // 2. 装备加成（如果 EquipmentBonusSystem 存在）
    EquipmentBonusSystem equip_sys(registry);
    EquipmentBonus equip_bonus = equip_sys.calculate_total_bonus(entity);
    final_max_hp += equip_bonus.hp_bonus;
    final_max_mp += equip_bonus.mp_bonus;
    final_attack += equip_bonus.attack_bonus;
    final_defense += equip_bonus.defense_bonus;
    final_magic_attack += equip_bonus.magic_attack_bonus;
    final_magic_defense += equip_bonus.magic_defense_bonus;

    // 3. 奖励点数加成（从 attributes 中读取分配值）
    // 这些字段将在 Phase 4 BonusPointComponent 中填充
    // 此处预留接口

    // 4. 设置最终属性
    attributes->max_hp = std::max(1, final_max_hp);
    attributes->max_mp = std::max(0, final_max_mp);
    attributes->attack = std::max(0, final_attack);
    attributes->defense = std::max(0, final_defense);
    attributes->magic_attack = std::max(0, final_magic_attack);
    attributes->magic_defense = std::max(0, final_magic_defense);
    attributes->sc = std::max(0, final_sc);
    attributes->max_weight = std::max(0, final_max_weight);
    attributes->max_wear_weight = std::max(0, final_max_wear_weight);
    attributes->max_hand_weight = std::max(0, final_max_hand_weight);

    // 5. 钳制当前值
    attributes->hp = std::min(attributes->hp, attributes->max_hp);
    attributes->mp = std::min(attributes->mp, attributes->max_mp);

    // 6. 标记脏数据
    dirty_tracker::mark_attributes_dirty(registry, entity);
}

}  // namespace mir2::ecs
