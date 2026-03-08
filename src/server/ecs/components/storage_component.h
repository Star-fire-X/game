/**
 * @file storage_component.h
 * @brief ECS storage component.
 */

#ifndef MIR2_SERVER_ECS_STORAGE_COMPONENT_H_
#define MIR2_SERVER_ECS_STORAGE_COMPONENT_H_

#include <array>
#include <cstddef>

#include <entt/entt.hpp>

namespace mir2::ecs {

constexpr int kMaxStorageSlots = 40;

/**
 * @brief Storage slots attached to a character (POD).
 */
struct StorageComponent {
    // EnTT's null sentinel is not guaranteed to be zero; initialize explicitly.
    std::array<entt::entity, kMaxStorageSlots> slots = [] {
        std::array<entt::entity, kMaxStorageSlots> value{};
        value.fill(entt::null);
        return value;
    }();

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

#endif  // MIR2_SERVER_ECS_STORAGE_COMPONENT_H_
