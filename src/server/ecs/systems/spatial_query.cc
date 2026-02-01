/**
 * @file spatial_query.cc
 * @brief ECS spatial query implementation.
 */

#include "ecs/systems/spatial_query.h"

#include "ecs/components/character_components.h"

#include <cmath>
#include <cstdlib>

namespace mir2::ecs {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kRadToDeg = 180.0f / kPi;

struct DirectionVector {
    int dx = 0;
    int dy = 0;
};

DirectionVector direction_to_vector(mir2::common::Direction dir) {
    switch (dir) {
        case mir2::common::Direction::UP:
            return {0, -1};
        case mir2::common::Direction::UP_RIGHT:
            return {1, -1};
        case mir2::common::Direction::RIGHT:
            return {1, 0};
        case mir2::common::Direction::DOWN_RIGHT:
            return {1, 1};
        case mir2::common::Direction::DOWN:
            return {0, 1};
        case mir2::common::Direction::DOWN_LEFT:
            return {-1, 1};
        case mir2::common::Direction::LEFT:
            return {-1, 0};
        case mir2::common::Direction::UP_LEFT:
            return {-1, -1};
        default:
            return {0, 0};
    }
}

float normalize_degrees(float angle) {
    while (angle > 180.0f) {
        angle -= 360.0f;
    }
    while (angle < -180.0f) {
        angle += 360.0f;
    }
    return angle;
}

bool matches_filter(const entt::registry& registry,
                    entt::entity entity,
                    EntityFilter filter) {
    switch (filter) {
        case EntityFilter::ALL:
            return true;
        case EntityFilter::PLAYERS_ONLY:
            return registry.all_of<CharacterIdentityComponent>(entity);
        case EntityFilter::MONSTERS_ONLY:
        case EntityFilter::ENEMIES_ONLY:
            // Without explicit faction/monster tags, treat non-player entities as monsters/enemies.
            return !registry.all_of<CharacterIdentityComponent>(entity);
        default:
            return true;
    }
}

}  // namespace

SpatialQuery::SpatialQuery(entt::registry& registry)
    : registry_(registry) {}

std::vector<entt::entity> SpatialQuery::get_entities_in_radius(
    const mir2::common::Position& center,
    float radius,
    EntityFilter filter) const {
    std::vector<entt::entity> result;
    if (radius < 0.0f) {
        return result;
    }

    const float radius_sq = radius * radius;
    auto view = registry_.view<TransformComponent>();

    for (auto entity : view) {
        if (!matches_filter(registry_, entity, filter)) {
            continue;
        }
        const auto& transform = view.get<TransformComponent>(entity);
        if (distance_squared(center, transform.position) <= radius_sq) {
            result.push_back(entity);
        }
    }

    return result;
}

std::vector<entt::entity> SpatialQuery::get_entities_in_arc(
    const mir2::common::Position& center,
    float radius,
    mir2::common::Direction facing,
    float arc_degrees) const {
    std::vector<entt::entity> result;
    if (radius <= 0.0f || arc_degrees <= 0.0f) {
        return result;
    }

    const float radius_sq = radius * radius;
    const float facing_angle = direction_to_angle(facing);
    const float half_arc = arc_degrees * 0.5f;

    auto view = registry_.view<TransformComponent>();

    for (auto entity : view) {
        const auto& transform = view.get<TransformComponent>(entity);
        if (distance_squared(center, transform.position) > radius_sq) {
            continue;
        }

        const float target_angle = angle_between(center, transform.position);
        const float delta = std::fabs(normalize_degrees(target_angle - facing_angle));
        if (delta <= half_arc) {
            result.push_back(entity);
        }
    }

    return result;
}

std::vector<entt::entity> SpatialQuery::get_entities_in_line(
    const mir2::common::Position& start,
    mir2::common::Direction dir,
    int length) const {
    std::vector<entt::entity> result;
    if (length <= 0) {
        return result;
    }

    const DirectionVector dir_vec = direction_to_vector(dir);
    if (dir_vec.dx == 0 && dir_vec.dy == 0) {
        return result;
    }

    auto view = registry_.view<TransformComponent>();

    for (auto entity : view) {
        const auto& transform = view.get<TransformComponent>(entity);
        const int dx = transform.position.x - start.x;
        const int dy = transform.position.y - start.y;
        int steps = 0;

        if (dir_vec.dx == 0) {
            if (dx != 0) {
                continue;
            }
            if ((dy > 0) != (dir_vec.dy > 0)) {
                continue;
            }
            steps = std::abs(dy);
        } else if (dir_vec.dy == 0) {
            if (dy != 0) {
                continue;
            }
            if ((dx > 0) != (dir_vec.dx > 0)) {
                continue;
            }
            steps = std::abs(dx);
        } else {
            if (dx == 0 || dy == 0) {
                continue;
            }
            if ((dx > 0) != (dir_vec.dx > 0) || (dy > 0) != (dir_vec.dy > 0)) {
                continue;
            }
            if (std::abs(dx) != std::abs(dy)) {
                continue;
            }
            steps = std::abs(dx);
        }

        if (steps >= 1 && steps <= length) {
            result.push_back(entity);
        }
    }

    return result;
}

float SpatialQuery::distance_squared(const mir2::common::Position& a,
                                     const mir2::common::Position& b) const {
    const float dx = static_cast<float>(a.x - b.x);
    const float dy = static_cast<float>(a.y - b.y);
    return dx * dx + dy * dy;
}

float SpatialQuery::angle_between(const mir2::common::Position& from,
                                  const mir2::common::Position& to) const {
    const float dx = static_cast<float>(to.x - from.x);
    const float dy = static_cast<float>(to.y - from.y);
    return std::atan2(dy, dx) * kRadToDeg;
}

float SpatialQuery::direction_to_angle(mir2::common::Direction dir) const {
    switch (dir) {
        case mir2::common::Direction::UP:
            return -90.0f;
        case mir2::common::Direction::UP_RIGHT:
            return -45.0f;
        case mir2::common::Direction::RIGHT:
            return 0.0f;
        case mir2::common::Direction::DOWN_RIGHT:
            return 45.0f;
        case mir2::common::Direction::DOWN:
            return 90.0f;
        case mir2::common::Direction::DOWN_LEFT:
            return 135.0f;
        case mir2::common::Direction::LEFT:
            return 180.0f;
        case mir2::common::Direction::UP_LEFT:
            return -135.0f;
        default:
            return 0.0f;
    }
}

}  // namespace mir2::ecs
