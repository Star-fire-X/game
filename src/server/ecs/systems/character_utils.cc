#include "ecs/systems/character_utils.h"

#include <algorithm>

namespace mir2::ecs {
namespace CharacterUtils {

void AddGold(entt::registry& registry, entt::entity entity, int amount) {
    if (amount <= 0) {
        return;
    }

    auto* attributes = registry.try_get<CharacterAttributesComponent>(entity);
    if (!attributes) {
        return;
    }

    attributes->gold += amount;
}

bool SpendGold(entt::registry& registry, entt::entity entity, int amount) {
    auto* attributes = registry.try_get<CharacterAttributesComponent>(entity);
    if (!attributes) {
        return false;
    }

    // 花费量为0或负数时直接成功
    if (amount <= 0) {
        return true;
    }

    if (attributes->gold < amount) {
        return false;
    }

    attributes->gold -= amount;
    return true;
}

int GetGold(entt::registry& registry, entt::entity entity) {
    auto* attributes = registry.try_get<CharacterAttributesComponent>(entity);
    if (!attributes) {
        return 0;
    }

    return attributes->gold;
}

void AddStats(entt::registry& registry, entt::entity entity,
              const mir2::common::CharacterStats& bonus) {
    auto* attributes = registry.try_get<CharacterAttributesComponent>(entity);
    if (!attributes) {
        return;
    }

    attributes->max_hp += bonus.max_hp;
    attributes->max_mp += bonus.max_mp;
    attributes->attack += bonus.attack;
    attributes->defense += bonus.defense;
    attributes->magic_attack += bonus.magic_attack;
    attributes->magic_defense += bonus.magic_defense;
    attributes->speed += bonus.speed;

    // 确保当前HP/MP不超过新的最大值
    attributes->hp = std::min(attributes->hp, attributes->max_hp);
    attributes->mp = std::min(attributes->mp, attributes->max_mp);
}

void RemoveStats(entt::registry& registry, entt::entity entity,
                 const mir2::common::CharacterStats& bonus) {
    auto* attributes = registry.try_get<CharacterAttributesComponent>(entity);
    if (!attributes) {
        return;
    }

    // 确保属性不会低于最小值
    attributes->max_hp = std::max(1, attributes->max_hp - bonus.max_hp);
    attributes->max_mp = std::max(0, attributes->max_mp - bonus.max_mp);
    attributes->attack = std::max(1, attributes->attack - bonus.attack);
    attributes->defense = std::max(0, attributes->defense - bonus.defense);
    attributes->magic_attack =
        std::max(0, attributes->magic_attack - bonus.magic_attack);
    attributes->magic_defense =
        std::max(0, attributes->magic_defense - bonus.magic_defense);
    attributes->speed = std::max(0, attributes->speed - bonus.speed);

    // 确保当前HP/MP不超过新的最大值
    attributes->hp = std::min(attributes->hp, attributes->max_hp);
    attributes->mp = std::min(attributes->mp, attributes->max_mp);
}

}  // namespace CharacterUtils
}  // namespace mir2::ecs
