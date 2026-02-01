#include "ecs/systems/npc_ai_system.h"

#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/npc_events.h"
#include "ecs/systems/character_utils.h"
#include "ecs/systems/inventory_system.h"
#include "ecs/components/character_components.h"
#include "game/npc/npc_entity.h"

#include <utility>

namespace mir2::game::npc {

namespace {
constexpr float kDefaultPatrolInterval = 1.0f;
constexpr float kMinPatrolSpeed = 0.001f;

float compute_patrol_interval(float speed) {
    if (speed <= kMinPatrolSpeed) {
        return kDefaultPatrolInterval;
    }
    return 1.0f / speed;
}

std::vector<NpcPatrolPoint> convert_patrol_points(
    const std::vector<mir2::common::Position>& points) {
    std::vector<NpcPatrolPoint> converted;
    converted.reserve(points.size());
    for (const auto& point : points) {
        converted.push_back({point.x, point.y});
    }
    return converted;
}

void sync_state_component(ecs::NpcStateComponent& state, const NpcStateMachine& machine) {
    const auto current_state = machine.GetState();
    if (state.current_state != current_state) {
        state.previous_state = state.current_state;
        state.current_state = current_state;
    }

    state.state_timer = machine.GetStateTimer();
    state.interacting_player = machine.GetInteractingPlayer();
}

void sync_transform(entt::registry& registry, entt::entity entity,
                    ecs::TransformComponent& transform, const NpcEntity& npc) {
    bool changed = false;
    if (transform.map_id != npc.GetMapId()) {
        transform.map_id = npc.GetMapId();
        changed = true;
    }

    if (transform.position.x != npc.GetX() || transform.position.y != npc.GetY()) {
        transform.position.x = npc.GetX();
        transform.position.y = npc.GetY();
        changed = true;
    }

    if (changed) {
        ecs::dirty_tracker::mark_state_dirty(registry, entity);
    }
}

}  // namespace

struct NpcAISystem::NpcRuntime {
    explicit NpcRuntime(uint64_t id, const ecs::TransformComponent& transform)
        : npc(id),
          state_machine(npc) {
        npc.SetMap(transform.map_id, transform.position.x, transform.position.y);
    }

    NpcEntity npc;
    NpcStateMachine state_machine;
    bool config_initialized = false;
    bool cached_patrol_enabled = false;
    float cached_idle_duration = 0.0f;
    float cached_patrol_speed = 0.0f;
    std::vector<mir2::common::Position> cached_patrol_points;
};

NpcAISystem::NpcAISystem() = default;
NpcAISystem::NpcAISystem(entt::registry& registry, ecs::EventBus& event_bus) {
    RegisterQueryHandlers(registry, event_bus);
}
NpcAISystem::~NpcAISystem() = default;

void NpcAISystem::SyncRuntimeFromTransform(NpcRuntime& runtime,
                                           const ecs::TransformComponent& transform) {
    if (runtime.npc.GetMapId() != transform.map_id ||
        runtime.npc.GetX() != transform.position.x ||
        runtime.npc.GetY() != transform.position.y) {
        runtime.npc.SetMap(transform.map_id, transform.position.x, transform.position.y);
    }
}

bool NpcAISystem::ApplyAIConfig(NpcRuntime& runtime, const ecs::NpcAIComponent& ai) {
    if (!runtime.config_initialized || runtime.cached_idle_duration != ai.idle_duration) {
        runtime.state_machine.SetIdleDuration(ai.idle_duration);
        runtime.cached_idle_duration = ai.idle_duration;
    }

    if (!runtime.config_initialized || runtime.cached_patrol_speed != ai.patrol_speed) {
        runtime.state_machine.SetPatrolInterval(compute_patrol_interval(ai.patrol_speed));
        runtime.cached_patrol_speed = ai.patrol_speed;
    }

    const bool patrol_changed = !runtime.config_initialized ||
                                runtime.cached_patrol_enabled != ai.enable_patrol ||
                                runtime.cached_patrol_points != ai.patrol_points;
    if (patrol_changed) {
        runtime.cached_patrol_enabled = ai.enable_patrol;
        runtime.cached_patrol_points = ai.patrol_points;

        if (ai.enable_patrol && !ai.patrol_points.empty()) {
            runtime.state_machine.SetPatrolPoints(convert_patrol_points(ai.patrol_points));
        } else {
            runtime.state_machine.ClearPatrolPoints();
        }
    }

    runtime.config_initialized = true;
    return patrol_changed;
}

void NpcAISystem::Update(entt::registry& registry, float dt) {
    if (dt <= 0.0f) {
        return;
    }

    if (!pending_interactions_.empty()) {
        for (const auto& interaction : pending_interactions_) {
            ProcessInteraction(registry, interaction);
        }
        pending_interactions_.clear();
    }

    auto view = registry.view<ecs::NpcStateComponent, ecs::NpcAIComponent,
                              ecs::TransformComponent>();
    for (auto entity : view) {
        auto& state = view.get<ecs::NpcStateComponent>(entity);
        auto& ai = view.get<ecs::NpcAIComponent>(entity);
        auto& transform = view.get<ecs::TransformComponent>(entity);

        auto& runtime = GetOrCreateRuntime(entity, transform);
        SyncRuntimeFromTransform(runtime, transform);

        const bool patrol_config_reset = ApplyAIConfig(runtime, ai);
        if (patrol_config_reset) {
            ai.current_patrol_index = 0;
        }

        if (state.current_state != runtime.state_machine.GetState()) {
            runtime.state_machine.TransitionTo(state.current_state);
        }

        if (runtime.state_machine.GetState() != NpcState::Dead) {
            runtime.state_machine.Update(dt);
        }

        sync_state_component(state, runtime.state_machine);
        sync_transform(registry, entity, transform, runtime.npc);

        if (!ai.enable_patrol || ai.patrol_points.empty()) {
            ai.current_patrol_index = 0;
        } else if (ai.current_patrol_index >= ai.patrol_points.size()) {
            ai.current_patrol_index = 0;
        } else if (runtime.state_machine.GetState() == NpcState::Patrol) {
            const auto& current_target = ai.patrol_points[ai.current_patrol_index];
            if (transform.position == current_target) {
                ai.current_patrol_index =
                    (ai.current_patrol_index + 1) % ai.patrol_points.size();
            }
        }
    }

    CleanupRuntimes(registry);
}

void NpcAISystem::OnPlayerInteract(entt::entity npc, entt::entity player) {
    pending_interactions_.push_back({npc, player});
}

void NpcAISystem::RegisterQueryHandlers(entt::registry& registry,
                                        ecs::EventBus& event_bus) {
    if (query_handlers_registered_) {
        return;
    }

    event_bus.Subscribe<ecs::events::NpcHasItemEvent>(
        [this, &registry, &event_bus](const ecs::events::NpcHasItemEvent& event) {
            HandleHasItemQuery(registry, event_bus, event);
        });

    event_bus.Subscribe<ecs::events::NpcGetGoldEvent>(
        [this, &registry, &event_bus](const ecs::events::NpcGetGoldEvent& event) {
            HandleGetGoldQuery(registry, event_bus, event);
        });

    event_bus.Subscribe<ecs::events::NpcGetPlayerNameEvent>(
        [this, &registry, &event_bus](const ecs::events::NpcGetPlayerNameEvent& event) {
            HandleGetPlayerInfoQuery(registry, event_bus, event);
        });

    event_bus.Subscribe<ecs::events::NpcGetPlayerLevelEvent>(
        [this, &registry, &event_bus](const ecs::events::NpcGetPlayerLevelEvent& event) {
            HandleGetPlayerInfoQuery(registry, event_bus, event);
        });

    query_handlers_registered_ = true;
}

NpcAISystem::NpcRuntime& NpcAISystem::GetOrCreateRuntime(
    entt::entity entity, const ecs::TransformComponent& transform) {
    auto it = runtime_.find(entity);
    if (it != runtime_.end()) {
        return *it->second;
    }

    const auto id = static_cast<uint64_t>(entt::to_integral(entity));
    auto runtime = std::make_unique<NpcRuntime>(id, transform);
    auto [inserted, inserted_ok] = runtime_.emplace(entity, std::move(runtime));
    (void)inserted_ok;
    return *inserted->second;
}

void NpcAISystem::ProcessInteraction(entt::registry& registry,
                                     const PendingInteraction& interaction) {
    if (!registry.valid(interaction.npc)) {
        return;
    }

    if (!registry.all_of<ecs::NpcStateComponent, ecs::NpcAIComponent,
                         ecs::TransformComponent>(interaction.npc)) {
        return;
    }

    auto& state = registry.get<ecs::NpcStateComponent>(interaction.npc);
    auto& ai = registry.get<ecs::NpcAIComponent>(interaction.npc);
    auto& transform = registry.get<ecs::TransformComponent>(interaction.npc);

    auto& runtime = GetOrCreateRuntime(interaction.npc, transform);
    SyncRuntimeFromTransform(runtime, transform);
    ApplyAIConfig(runtime, ai);

    runtime.state_machine.OnPlayerInteract(interaction.player);
    sync_state_component(state, runtime.state_machine);
}

void NpcAISystem::CleanupRuntimes(entt::registry& registry) {
    for (auto it = runtime_.begin(); it != runtime_.end(); ) {
        if (!registry.valid(it->first) ||
            !registry.all_of<ecs::NpcStateComponent, ecs::NpcAIComponent,
                             ecs::TransformComponent>(it->first)) {
            it = runtime_.erase(it);
        } else {
            ++it;
        }
    }
}

void NpcAISystem::HandleHasItemQuery(entt::registry& registry,
                                     ecs::EventBus& event_bus,
                                     const ecs::events::NpcHasItemEvent& event) {
    ecs::events::NpcHasItemResultEvent result;
    result.player = event.player;
    result.npc_id = event.npc_id;
    result.request_id = event.request_id;
    result.item_id = event.item_id;
    result.count = event.count;

    if (!registry.valid(event.player)) {
        result.success = false;
        result.error_code = mir2::common::ErrorCode::CHARACTER_NOT_FOUND;
    } else if (event.item_id == 0 || event.count <= 0) {
        result.success = false;
        result.error_code = mir2::common::ErrorCode::INVALID_ACTION;
    } else {
        result.has_item =
            ecs::InventorySystem::HasItem(registry, event.player, event.item_id, event.count);
        result.success = true;
        result.error_code = mir2::common::ErrorCode::SUCCESS;
    }

    event_bus.Publish(result);

    if (event.result) {
        *event.result = result.has_item;
    }
    if (event.handled) {
        *event.handled = true;
    }
}

void NpcAISystem::HandleGetGoldQuery(entt::registry& registry,
                                     ecs::EventBus& event_bus,
                                     const ecs::events::NpcGetGoldEvent& event) {
    ecs::events::NpcGetGoldResultEvent result;
    result.player = event.player;
    result.npc_id = event.npc_id;
    result.request_id = event.request_id;

    if (!registry.valid(event.player)) {
        result.success = false;
        result.error_code = mir2::common::ErrorCode::CHARACTER_NOT_FOUND;
    } else if (registry.try_get<ecs::CharacterAttributesComponent>(event.player)) {
        result.amount = ecs::CharacterUtils::GetGold(registry, event.player);
        result.success = true;
        result.error_code = mir2::common::ErrorCode::SUCCESS;
    } else {
        result.success = false;
        result.error_code = mir2::common::ErrorCode::CHARACTER_NOT_FOUND;
    }

    event_bus.Publish(result);

    if (event.amount) {
        *event.amount = result.amount;
    }
    if (event.handled) {
        *event.handled = true;
    }
}

void NpcAISystem::HandleGetPlayerInfoQuery(
    entt::registry& registry,
    ecs::EventBus& event_bus,
    const ecs::events::NpcGetPlayerNameEvent& event) {
    ecs::events::NpcGetPlayerNameResultEvent result;
    result.player = event.player;
    result.npc_id = event.npc_id;
    result.request_id = event.request_id;

    if (!registry.valid(event.player)) {
        result.success = false;
        result.error_code = mir2::common::ErrorCode::CHARACTER_NOT_FOUND;
    } else if (auto* identity =
                   registry.try_get<ecs::CharacterIdentityComponent>(event.player)) {
        result.name = identity->name;
        result.success = true;
        result.error_code = mir2::common::ErrorCode::SUCCESS;
    } else {
        result.success = false;
        result.error_code = mir2::common::ErrorCode::CHARACTER_NOT_FOUND;
    }

    event_bus.Publish(result);

    if (event.name) {
        *event.name = result.name;
    }
    if (event.handled) {
        *event.handled = true;
    }
}

void NpcAISystem::HandleGetPlayerInfoQuery(
    entt::registry& registry,
    ecs::EventBus& event_bus,
    const ecs::events::NpcGetPlayerLevelEvent& event) {
    ecs::events::NpcGetPlayerLevelResultEvent result;
    result.player = event.player;
    result.npc_id = event.npc_id;
    result.request_id = event.request_id;

    if (!registry.valid(event.player)) {
        result.success = false;
        result.error_code = mir2::common::ErrorCode::CHARACTER_NOT_FOUND;
    } else if (auto* attributes =
                   registry.try_get<ecs::CharacterAttributesComponent>(event.player)) {
        result.level = attributes->level;
        result.success = true;
        result.error_code = mir2::common::ErrorCode::SUCCESS;
    } else {
        result.success = false;
        result.error_code = mir2::common::ErrorCode::CHARACTER_NOT_FOUND;
    }

    event_bus.Publish(result);

    if (event.level) {
        *event.level = result.level;
    }
    if (event.handled) {
        *event.handled = true;
    }
}

}  // namespace mir2::game::npc
