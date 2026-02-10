/**
 * @file pk_system.cc
 * @brief PK系统实现
 */

#include "ecs/systems/pk_system.h"

#include "ecs/components/character_components.h"
#include "ecs/components/equipment_component.h"
#include "ecs/components/item_component.h"
#include "ecs/dirty_tracker.h"

#include <algorithm>
#include <random>
#include <vector>

namespace mir2::ecs {

namespace {

bool is_valid_entity(const entt::registry& registry, entt::entity entity) {
    return entity != entt::null && registry.valid(entity);
}

bool is_player(const entt::registry& registry, entt::entity entity) {
    // 检查是否为玩家（有CharacterIdentityComponent）
    return registry.try_get<CharacterIdentityComponent>(entity) != nullptr;
}

} // namespace

PKSystem::PKSystem(entt::registry& registry)
    : registry_(registry) {}

void PKSystem::record_pk_attack(entt::entity attacker, entt::entity victim) {
    if (!is_valid_entity(registry_, attacker) || !is_valid_entity(registry_, victim)) {
        return;
    }

    // 只记录玩家对玩家的攻击
    if (!is_player(registry_, attacker) || !is_player(registry_, victim)) {
        return;
    }

    // 在受害者身上记录攻击者
    auto& victim_pk = registry_.get_or_emplace<PKComponent>(victim);
    victim_pk.add_hiter(attacker, current_time_ms_);
}

void PKSystem::handle_pk_kill(entt::entity killer, entt::entity victim) {
    if (!is_valid_entity(registry_, killer) || !is_valid_entity(registry_, victim)) {
        return;
    }

    // 只处理玩家对玩家的击杀
    if (!is_player(registry_, killer) || !is_player(registry_, victim)) {
        return;
    }

    // 增加击杀者的PK值
    auto& killer_pk = registry_.get_or_emplace<PKComponent>(killer);
    killer_pk.pk_points += 10;  // 每次击杀增加10点PK值
    killer_pk.last_pk_time_ms = current_time_ms_;
    killer_pk.update_red_name_status();

    dirty_tracker::mark_attributes_dirty(registry_, killer);
}

void PKSystem::update(int64_t current_time_ms) {
    current_time_ms_ = current_time_ms;

    auto view = registry_.view<PKComponent>();
    for (auto entity : view) {
        auto& pk_comp = view.get<PKComponent>(entity);

        // 清理过期的攻击记录
        pk_comp.cleanup_expired_hiters(current_time_ms_);

        // PK值自然衰减（每小时减少1点）
        if (pk_comp.pk_points > 0 && pk_comp.last_pk_time_ms > 0) {
            const int64_t time_since_last_pk = current_time_ms_ - pk_comp.last_pk_time_ms;
            const int64_t hours_passed = time_since_last_pk / 3600000; // 1小时 = 3600000ms
            if (hours_passed > 0) {
                pk_comp.pk_points = std::max(0, pk_comp.pk_points - static_cast<int>(hours_passed));
                pk_comp.last_pk_time_ms = current_time_ms_;
                pk_comp.update_red_name_status();
                dirty_tracker::mark_attributes_dirty(registry_, entity);
            }
        }
    }
}

bool PKSystem::is_red_name(entt::entity entity) const {
    const auto* pk_comp = registry_.try_get<PKComponent>(entity);
    return pk_comp && pk_comp->is_red_name;
}

void PKSystem::handle_red_name_death(entt::entity victim,
                                     const mir2::common::Position& death_pos) {
    if (!is_valid_entity(registry_, victim)) {
        return;
    }

    if (!is_player(registry_, victim)) {
        return;
    }

    const auto* pk_comp = registry_.try_get<PKComponent>(victim);
    if (!pk_comp || !pk_comp->is_red_name) {
        return;
    }

    int drop_count = 0;
    if (pk_comp->pk_points >= 100) {
        drop_count = 2;
    } else if (pk_comp->pk_points >= 50) {
        drop_count = 1;
    }

    if (drop_count <= 0) {
        return;
    }

    auto* equipment = registry_.try_get<EquipmentSlotComponent>(victim);
    if (!equipment) {
        return;
    }

    std::vector<std::size_t> filled_slots;
    filled_slots.reserve(EquipmentSlotComponent::kSlotCount);
    for (std::size_t i = 0; i < EquipmentSlotComponent::kSlotCount; ++i) {
        const entt::entity item = equipment->slots[i];
        if (item == entt::null) {
            continue;
        }
        if (!registry_.valid(item)) {
            equipment->slots[i] = entt::null;
            continue;
        }
        filled_slots.push_back(i);
    }

    if (filled_slots.empty()) {
        return;
    }

    drop_count = std::min(drop_count, static_cast<int>(filled_slots.size()));

    static thread_local std::mt19937 rng(std::random_device{}());
    std::shuffle(filled_slots.begin(), filled_slots.end(), rng);

    uint32_t map_id = 0;
    if (const auto* victim_state = registry_.try_get<CharacterStateComponent>(victim)) {
        map_id = victim_state->map_id;
    }

    for (int i = 0; i < drop_count; ++i) {
        const std::size_t slot_index = filled_slots[static_cast<std::size_t>(i)];
        entt::entity item = equipment->slots[slot_index];
        if (item == entt::null) {
            continue;
        }

        equipment->slots[slot_index] = entt::null;

        if (auto* owner = registry_.try_get<InventoryOwnerComponent>(item)) {
            owner->owner = entt::null;
            owner->slot_index = -1;
        }

        auto& item_state = registry_.get_or_emplace<CharacterStateComponent>(item);
        item_state.map_id = map_id;
        item_state.position = death_pos;
    }

    dirty_tracker::mark_items_dirty(registry_, victim);
    dirty_tracker::mark_equipment_dirty(registry_, victim);
}

} // namespace mir2::ecs
