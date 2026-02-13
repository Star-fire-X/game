#include "ecs/systems/bonus_point_system.h"

#include "ecs/components/character_components.h"
#include "ecs/components/bonus_point_component.h"
#include "ecs/dirty_tracker.h"
#include "ecs/systems/growth_formulas.h"
#include "ecs/systems/attribute_recalc_system.h"

namespace mir2::ecs {

BonusPointSystem::BonusPointSystem(entt::registry& registry)
    : registry_(registry) {}

void BonusPointSystem::RecalcAvailablePoints(entt::entity entity) {
    auto* identity = registry_.try_get<CharacterIdentityComponent>(entity);
    auto* attributes = registry_.try_get<CharacterAttributesComponent>(entity);
    if (!identity || !attributes) {
        return;
    }

    int total = GrowthFormulas::GetBonusPoint(identity->char_class, attributes->level);

    auto& bonus = registry_.get_or_emplace<BonusPointComponent>(entity);
    bonus.total_available = total;

    // 同步到 attributes 供持久化
    attributes->bonus_remaining = bonus.remaining();
    dirty_tracker::mark_attributes_dirty(registry_, entity);
}

bool BonusPointSystem::AllocatePoint(entt::entity entity, const std::string& attribute_name) {
    auto& bonus = registry_.get_or_emplace<BonusPointComponent>(entity);

    if (bonus.remaining() <= 0) {
        return false;
    }

    // 分配到指定属性
    int* target = nullptr;
    if (attribute_name == "dc") target = &bonus.allocated.dc;
    else if (attribute_name == "mc") target = &bonus.allocated.mc;
    else if (attribute_name == "sc") target = &bonus.allocated.sc;
    else if (attribute_name == "ac") target = &bonus.allocated.ac;
    else if (attribute_name == "mac") target = &bonus.allocated.mac;
    else if (attribute_name == "hp") target = &bonus.allocated.hp;
    else if (attribute_name == "mp") target = &bonus.allocated.mp;
    else if (attribute_name == "hit") target = &bonus.allocated.hit;
    else if (attribute_name == "speed") target = &bonus.allocated.speed;

    if (!target) {
        return false;
    }

    *target += 1;

    // 更新 attributes 中的 bonus_remaining
    auto* attributes = registry_.try_get<CharacterAttributesComponent>(entity);
    if (attributes) {
        attributes->bonus_remaining = bonus.remaining();
    }

    // 触发属性重算
    AttributeRecalcSystem::RecalcAll(registry_, entity);
    return true;
}

void BonusPointSystem::ResetPoints(entt::entity entity) {
    auto* bonus = registry_.try_get<BonusPointComponent>(entity);
    if (!bonus) {
        return;
    }

    bonus->allocated = BonusAbility{};

    auto* attributes = registry_.try_get<CharacterAttributesComponent>(entity);
    if (attributes) {
        attributes->bonus_remaining = bonus->remaining();
    }

    // 触发属性重算
    AttributeRecalcSystem::RecalcAll(registry_, entity);
}

}  // namespace mir2::ecs
