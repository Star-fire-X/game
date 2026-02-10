/**
 * @file ground_item_system.cc
 * @brief ECS 地面物品清理系统实现
 */

#include "ecs/systems/ground_item_system.h"

#include <vector>

#include "core/utils.h"
#include "ecs/components/ground_item_component.h"

namespace mir2::ecs {

GroundItemSystem::GroundItemSystem()
    : System(SystemPriority::kInventory) {}

void GroundItemSystem::Update(entt::registry& registry, float delta_time) {
    if (delta_time < 0.0f) {
        return;
    }

    check_timer_ += delta_time;
    if (check_timer_ < kCheckIntervalSeconds) {
        return;
    }
    check_timer_ = 0.0f;

    const int64_t now_ms = mir2::core::GetCurrentTimestampMs();
    CleanupExpired(registry, now_ms);
}

void GroundItemSystem::CleanupExpired(entt::registry& registry, int64_t now_ms) {
    auto view = registry.view<GroundItemComponent>();
    if (view.empty()) {
        return;
    }

    std::vector<entt::entity> expired_entities;
    // expired_entities.reserve(view.size_hint());  // EnTT API不兼容

    for (auto entity : view) {
        const auto& item = view.get<GroundItemComponent>(entity);
        if (item.IsExpired(now_ms)) {
            expired_entities.push_back(entity);
        }
    }

    for (auto entity : expired_entities) {
        if (registry.valid(entity)) {
            registry.destroy(entity);
        }
    }
}

}  // namespace mir2::ecs
