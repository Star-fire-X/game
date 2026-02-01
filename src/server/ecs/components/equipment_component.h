/**
 * @file equipment_component.h
 * @brief ECS equipment slot component.
 *
 * Holds references to equipped item entities on a character.
 */

#ifndef LEGEND2_SERVER_ECS_EQUIPMENT_COMPONENT_H
#define LEGEND2_SERVER_ECS_EQUIPMENT_COMPONENT_H

#include "common/enums.h"

#include <array>
#include <cstddef>

#include <entt/entt.hpp>

namespace mir2::ecs {

/**
 * @brief Equipment slots attached to a character (POD).
 *
 * Slot order matches mir2::common::EquipSlot:
 * WEAPON, ARMOR, HELMET, BOOTS, RING_LEFT, RING_RIGHT, NECKLACE,
 * BRACELET_LEFT, BRACELET_RIGHT, BELT, AMULET, TALISMAN, CHARM (total 13 slots).
 * Each entry stores an item entity; entt::null means empty.
 */
struct EquipmentSlotComponent {
    static constexpr std::size_t kSlotCount =
        static_cast<std::size_t>(mir2::common::EquipSlot::MAX_SLOTS);

    std::array<entt::entity, kSlotCount> slots{};  ///< Equipped item entities.
};

}  // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_EQUIPMENT_COMPONENT_H
