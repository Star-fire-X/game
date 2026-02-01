#include "ecs/world.h"

#include "ecs/components/character_components.h"
#include "ecs/components/combat_component.h"
#include "ecs/event_bus.h"
#include "ecs/systems/npc_ai_system.h"
#include "ecs/systems/storage_system.h"
#include "log/logger.h"

#include <algorithm>

namespace mir2::ecs {

World::World(std::size_t reserve_capacity)
    : event_bus_(std::make_unique<EventBus>(registry_)) {
    npc_ai_system_ = std::make_unique<game::npc::NpcAISystem>(registry_, *event_bus_);
    SYSLOG_INFO("World: NpcAISystem registered");
    CreateSystem<StorageSystem>(registry_, *event_bus_);
    SYSLOG_INFO("World: StorageSystem registered");
    if (reserve_capacity == 0) {
        return;
    }

    // EnTT registry reserve API is not available in all versions; skip preallocation.
}

World::~World() = default;

EventBus& World::GetEventBus() {
    return *event_bus_;
}

game::npc::NpcAISystem* World::GetNpcAISystem() {
    return npc_ai_system_.get();
}

const game::npc::NpcAISystem* World::GetNpcAISystem() const {
    return npc_ai_system_.get();
}

void World::ClearSystems() {
    systems_.clear();
    systems_dirty_ = false;
}

void World::Update(float delta_time) {
    if (systems_dirty_) {
        std::sort(systems_.begin(), systems_.end(),
                  [](const std::unique_ptr<System>& lhs, const std::unique_ptr<System>& rhs) {
                      return static_cast<int>(lhs->Priority()) <
                             static_cast<int>(rhs->Priority());
                  });
        systems_dirty_ = false;
    }

    if (npc_ai_system_) {
        // Run NPC AI before movement-related systems to keep NPC state/transform consistent.
        npc_ai_system_->Update(registry_, delta_time);
    }

    for (const auto& system : systems_) {
        system->Update(registry_, delta_time);
    }

    event_bus_->FlushEvents();
}

}  // namespace mir2::ecs
