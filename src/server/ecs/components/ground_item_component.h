/**
 * @file ground_item_component.h
 * @brief ECS ground item component.
 */

#ifndef MIR2_SERVER_ECS_GROUND_ITEM_COMPONENT_H_
#define MIR2_SERVER_ECS_GROUND_ITEM_COMPONENT_H_

#include <cstdint>

#include <entt/entt.hpp>

#include "common/item_constants.h"

namespace mir2::ecs {

struct GroundItemComponent {
    entt::entity owner = entt::null;      // 拾取权归属者
    entt::entity dropper = entt::null;    // 掉落者
    int64_t drop_time_ms = 0;             // 掉落时间戳
    int64_t ownership_expire_ms = 0;      // 拾取权过期时间
    uint32_t map_id = 0;                  // 所在地图
    int32_t x = 0;                        // X坐标
    int32_t y = 0;                        // Y坐标

    bool IsOwnershipExpired(int64_t current_time_ms) const {
        if (ownership_expire_ms > 0) {
            return current_time_ms >= ownership_expire_ms;
        }
        if (drop_time_ms <= 0) {
            return false;
        }
        return current_time_ms >= drop_time_ms + mir2::common::pickup::kOwnershipDurationMs;
    }

    bool IsExpired(int64_t current_time_ms) const {
        if (drop_time_ms <= 0) {
            return false;
        }
        return current_time_ms >= drop_time_ms + mir2::common::pickup::kItemExpireDurationMs;
    }

    bool CanPickup(entt::entity picker, int64_t current_time_ms) const {
        if (picker == entt::null) {
            return false;
        }
        if (owner == entt::null || picker == owner) {
            return true;
        }
        return IsOwnershipExpired(current_time_ms);
    }
};

}  // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_GROUND_ITEM_COMPONENT_H_
