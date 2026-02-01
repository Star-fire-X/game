#ifndef LEGEND2_SERVER_ECS_SPATIAL_QUERY_H
#define LEGEND2_SERVER_ECS_SPATIAL_QUERY_H

#include "ecs/components/transform_component.h"
#include "common/types.h"
#include <entt/entt.hpp>
#include <vector>

namespace mir2::ecs {

enum class EntityFilter : uint8_t {
    ALL = 0,
    PLAYERS_ONLY = 1,
    MONSTERS_ONLY = 2,
    ENEMIES_ONLY = 3
};

class SpatialQuery {
public:
    explicit SpatialQuery(entt::registry& registry);

    // Circle area
    std::vector<entt::entity> get_entities_in_radius(
        const mir2::common::Position& center,
        float radius,
        EntityFilter filter = EntityFilter::ALL) const;
    
    // Arc area (for 半月弯刀)
    std::vector<entt::entity> get_entities_in_arc(
        const mir2::common::Position& center,
        float radius,
        mir2::common::Direction facing,
        float arc_degrees) const;
    
    // Line area (for 刺杀剑术)
    std::vector<entt::entity> get_entities_in_line(
        const mir2::common::Position& start,
        mir2::common::Direction dir,
        int length) const;

private:
    entt::registry& registry_;
    
    float distance_squared(const mir2::common::Position& a, const mir2::common::Position& b) const;
    float angle_between(const mir2::common::Position& from, const mir2::common::Position& to) const;
    float direction_to_angle(mir2::common::Direction dir) const;
};

} // namespace mir2::ecs
#endif
