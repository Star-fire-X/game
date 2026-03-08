/**
 * @file npc_ai_system.h
 * @brief ECS NPC AI logic system
 */

#ifndef MIR2_SERVER_ECS_SYSTEMS_NPC_AI_SYSTEM_H_
#define MIR2_SERVER_ECS_SYSTEMS_NPC_AI_SYSTEM_H_

#include "ecs/components/npc_component.h"
#include "ecs/components/transform_component.h"
#include "ecs/event_bus.h"

#include <entt/entt.hpp>

#include <memory>
#include <unordered_map>
#include <vector>

namespace mir2::ecs::events {
struct NpcHasItemEvent;
struct NpcGetGoldEvent;
struct NpcGetPlayerNameEvent;
struct NpcGetPlayerLevelEvent;
}  // namespace mir2::ecs::events

namespace mir2::game::npc {

/**
 * @brief NPC AI system
 */
class NpcAISystem {
 public:
    NpcAISystem();
    NpcAISystem(entt::registry& registry, ecs::EventBus& event_bus);
    ~NpcAISystem();

    /**
     * @brief System update
     */
    void Update(entt::registry& registry, float dt);

    /**
     * @brief Handle player interaction
     */
    void OnPlayerInteract(entt::entity npc, entt::entity player);

    /**
     * @brief Register query event handlers (hasItem/getGold/player info).
     */
    void RegisterQueryHandlers(entt::registry& registry, ecs::EventBus& event_bus);

 private:
    struct PendingInteraction {
        entt::entity npc = entt::null;
        entt::entity player = entt::null;
    };

    struct NpcRuntime;

    static void SyncRuntimeFromTransform(NpcRuntime& runtime,
                                         const ecs::TransformComponent& transform);
    static bool ApplyAIConfig(NpcRuntime& runtime, const ecs::NpcAIComponent& ai);
    NpcRuntime& GetOrCreateRuntime(entt::entity entity,
                                   const ecs::TransformComponent& transform);
    void ProcessInteraction(entt::registry& registry, const PendingInteraction& interaction);
    void CleanupRuntimes(entt::registry& registry);

    void HandleHasItemQuery(entt::registry& registry,
                            ecs::EventBus& event_bus,
                            const ecs::events::NpcHasItemEvent& event);
    void HandleGetGoldQuery(entt::registry& registry,
                            ecs::EventBus& event_bus,
                            const ecs::events::NpcGetGoldEvent& event);
    void HandleGetPlayerInfoQuery(entt::registry& registry,
                                  ecs::EventBus& event_bus,
                                  const ecs::events::NpcGetPlayerNameEvent& event);
    void HandleGetPlayerInfoQuery(entt::registry& registry,
                                  ecs::EventBus& event_bus,
                                  const ecs::events::NpcGetPlayerLevelEvent& event);

    std::unordered_map<entt::entity, std::unique_ptr<NpcRuntime>> runtime_;
    std::vector<PendingInteraction> pending_interactions_;
    bool query_handlers_registered_ = false;
    std::vector<ecs::EventBus::Subscription> query_subscriptions_;
};

}  // namespace mir2::game::npc

#endif  // MIR2_SERVER_ECS_SYSTEMS_NPC_AI_SYSTEM_H_
