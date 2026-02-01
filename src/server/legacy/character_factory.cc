/**
 * @file character_factory.cc
 * @brief 角色 ECS 工厂函数实现
 */

#include "legacy/character_factory.h"

#include "ecs/components/character_components.h"
#include "ecs/components/equipment_component.h"
#include "ecs/inventory_migration.h"
#include <chrono>
#include <utility>

namespace legend2 {

entt::entity CreateCharacterEntity(entt::registry& registry,
                                   uint32_t id,
                                   const mir2::common::CharacterCreateRequest& request) {
    entt::entity entity = registry.create();

    auto& identity = registry.emplace<mir2::ecs::CharacterIdentityComponent>(entity);
    identity.id = id;
    identity.account_id = request.account_id;
    identity.name = request.name;
    identity.char_class = request.char_class;
    identity.gender = request.gender;

    auto& state = registry.emplace<mir2::ecs::CharacterStateComponent>(entity);
    state.map_id = 1;
    state.position = {100, 100};
    state.direction = mir2::common::Direction::DOWN;

    const mir2::common::CharacterStats base_stats = mir2::common::get_class_base_stats(request.char_class);

    auto& attributes = registry.emplace<mir2::ecs::CharacterAttributesComponent>(entity);
    attributes.level = base_stats.level;
    attributes.experience = base_stats.experience;
    attributes.hp = base_stats.hp;
    attributes.max_hp = base_stats.max_hp;
    attributes.mp = base_stats.mp;
    attributes.max_mp = base_stats.max_mp;
    attributes.attack = base_stats.attack;
    attributes.defense = base_stats.defense;
    attributes.magic_attack = base_stats.magic_attack;
    attributes.magic_defense = base_stats.magic_defense;
    attributes.speed = base_stats.speed;
    attributes.gold = base_stats.gold;

    auto& equipment = registry.emplace<mir2::ecs::EquipmentSlotComponent>(entity);
    equipment.slots.fill(entt::null);

    auto now = std::chrono::system_clock::now();
    state.created_at = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    state.last_login = state.created_at;
    state.last_active = state.created_at;

    return entity;
}

entt::entity LoadCharacterEntity(entt::registry& registry,
                                 const mir2::common::CharacterData& data) {
    entt::entity entity = registry.create();

    auto& identity = registry.emplace<mir2::ecs::CharacterIdentityComponent>(entity);
    identity.id = data.id;
    identity.account_id = data.account_id;
    identity.name = data.name;
    identity.char_class = data.char_class;
    identity.gender = data.gender;

    auto& attributes = registry.emplace<mir2::ecs::CharacterAttributesComponent>(entity);
    attributes.level = data.stats.level;
    attributes.experience = data.stats.experience;
    attributes.hp = data.stats.hp;
    attributes.max_hp = data.stats.max_hp;
    attributes.mp = data.stats.mp;
    attributes.max_mp = data.stats.max_mp;
    attributes.attack = data.stats.attack;
    attributes.defense = data.stats.defense;
    attributes.magic_attack = data.stats.magic_attack;
    attributes.magic_defense = data.stats.magic_defense;
    attributes.speed = data.stats.speed;
    attributes.gold = data.stats.gold;

    auto& state = registry.emplace<mir2::ecs::CharacterStateComponent>(entity);
    state.map_id = data.map_id;
    state.position = data.position;
    state.created_at = data.created_at;
    state.last_login = data.last_login;
    state.last_active = data.last_login;

    mir2::ecs::inventory::LoadInventoryFromJson(registry,
                                                entity,
                                                data.inventory_json,
                                                data.equipment_json,
                                                data.skills_json);

    registry.emplace<mir2::ecs::DirtyComponent>(entity, false, false, false, false);

    return entity;
}

mir2::common::CharacterData SaveCharacterData(entt::registry& registry, entt::entity entity) {
    mir2::common::CharacterData data;

    if (const auto* identity = registry.try_get<mir2::ecs::CharacterIdentityComponent>(entity)) {
        data.id = identity->id;
        data.account_id = identity->account_id;
        data.name = identity->name;
        data.char_class = identity->char_class;
        data.gender = identity->gender;
    }

    if (const auto* state = registry.try_get<mir2::ecs::CharacterStateComponent>(entity)) {
        data.map_id = state->map_id;
        data.position = state->position;
        data.created_at = state->created_at;
        data.last_login = state->last_login;
    }

    if (const auto* attributes = registry.try_get<mir2::ecs::CharacterAttributesComponent>(entity)) {
        data.stats.level = attributes->level;
        data.stats.experience = attributes->experience;
        data.stats.hp = attributes->hp;
        data.stats.max_hp = attributes->max_hp;
        data.stats.mp = attributes->mp;
        data.stats.max_mp = attributes->max_mp;
        data.stats.attack = attributes->attack;
        data.stats.defense = attributes->defense;
        data.stats.magic_attack = attributes->magic_attack;
        data.stats.magic_defense = attributes->magic_defense;
        data.stats.speed = attributes->speed;
        data.stats.gold = attributes->gold;
    }

    auto [inventory_json, equipment_json, skills_json] =
        mir2::ecs::inventory::SaveInventoryToJson(registry, entity);
    data.inventory_json = std::move(inventory_json);
    data.equipment_json = std::move(equipment_json);
    data.skills_json = std::move(skills_json);

    return data;
}

}  // namespace legend2
