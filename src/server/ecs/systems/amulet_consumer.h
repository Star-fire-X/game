#ifndef MIR2_SERVER_ECS_SYSTEMS_AMULET_CONSUMER_H_
#define MIR2_SERVER_ECS_SYSTEMS_AMULET_CONSUMER_H_

#include <entt/entt.hpp>

namespace mir2::ecs {

// Checks whether a talisman (StdMode=25, Shape=5) is equipped in AMULET or
// BRACELET_LEFT and has enough durability to cast.
bool CheckAmulet(const entt::registry& registry, entt::entity character);

// Consumes talisman durability on cast, removing it if durability drops below 100.
bool ConsumeAmulet(entt::registry& registry, entt::entity character);

}  // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_SYSTEMS_AMULET_CONSUMER_H_
