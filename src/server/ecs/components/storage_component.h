/**
 * @file storage_component.h
 * @brief ECS storage component.
 */

#ifndef LEGEND2_SERVER_ECS_STORAGE_COMPONENT_H
#define LEGEND2_SERVER_ECS_STORAGE_COMPONENT_H

#include <array>
#include <cstddef>

#include <entt/entt.hpp>

namespace mir2::ecs {

constexpr int kMaxStorageSlots = 40;

/**
 * @brief Storage slots attached to a character (POD).
 */
struct StorageComponent {
    std::array<entt::entity, kMaxStorageSlots> slots{};

    int FindFreeSlot() const {
        for (int i = 0; i < kMaxStorageSlots; ++i) {
            if (slots[static_cast<std::size_t>(i)] == entt::null) {
                return i;
            }
        }
        return -1;
    }
};

}  // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_STORAGE_COMPONENT_H
