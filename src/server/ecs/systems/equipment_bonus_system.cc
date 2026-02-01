#include "ecs/systems/equipment_bonus_system.h"

#include "ecs/components/equipment_component.h"
#include "ecs/components/item_component.h"

namespace mir2::ecs {

EquipmentBonusSystem::EquipmentBonusSystem(entt::registry& registry)
    : registry_(registry) {}

EquipmentBonus EquipmentBonusSystem::calculate_total_bonus(entt::entity entity) const {
    EquipmentBonus total{};

    if (!registry_.valid(entity)) {
        return total;
    }

    const auto* equipment = registry_.try_get<EquipmentSlotComponent>(entity);
    if (!equipment) {
        return total;
    }

    for (const auto& item_entity : equipment->slots) {
        if (item_entity == entt::null || !registry_.valid(item_entity)) {
            continue;
        }

        const auto* item = registry_.try_get<ItemComponent>(item_entity);
        if (!item) {
            continue;
        }

        // 累加基础属性
        total.attack_bonus += item->attack_bonus;
        total.defense_bonus += item->defense_bonus;
        total.magic_attack_bonus += item->magic_attack_bonus;
        total.magic_defense_bonus += item->magic_defense_bonus;
        total.hp_bonus += item->hp_bonus;
        total.mp_bonus += item->mp_bonus;
        total.hit_rate_bonus += item->hit_rate_bonus;
        total.dodge_bonus += item->dodge_bonus;
        total.speed_bonus += item->speed_bonus;
        total.luck_bonus += item->luck;

        // 累加特殊效果
        total.lifesteal_percent += item->lifesteal_percent;
        total.reflect_percent += item->reflect_percent;

        // 元素伤害按类型累加
        switch (item->elemental_type) {
        case 1: total.fire_damage += item->elemental_damage; break;
        case 2: total.ice_damage += item->elemental_damage; break;
        case 3: total.lightning_damage += item->elemental_damage; break;
        case 4: total.poison_damage += item->elemental_damage; break;
        default: break;
        }
    }

    return total;
}

void EquipmentBonusSystem::recalculate_bonus(entt::entity entity) {
    if (!registry_.valid(entity)) {
        return;
    }

    EquipmentBonus bonus = calculate_total_bonus(entity);
    registry_.emplace_or_replace<EquipmentBonus>(entity, bonus);
}

const EquipmentBonus* EquipmentBonusSystem::get_cached_bonus(entt::entity entity) const {
    if (!registry_.valid(entity)) {
        return nullptr;
    }
    return registry_.try_get<EquipmentBonus>(entity);
}

} // namespace mir2::ecs
