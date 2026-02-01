/**
 * @file summon_system.h
 * @brief ECS 召唤系统
 */

#ifndef LEGEND2_SERVER_ECS_SYSTEMS_SUMMON_SYSTEM_H
#define LEGEND2_SERVER_ECS_SYSTEMS_SUMMON_SYSTEM_H

#include "common/types.h"
#include "ecs/components/summon_component.h"

#include <entt/entt.hpp>

namespace mir2::ecs {

class SummonSystem {
public:
    explicit SummonSystem(entt::registry& registry);

    mir2::common::ErrorCode summon_creature(entt::entity summoner, uint32_t skill_id,
                                       const mir2::common::Position& pos);
    void dismiss_summon(entt::entity summoner, entt::entity summon);
    void dismiss_all_summons(entt::entity summoner);
    void update(int64_t current_time_ms);

private:
    entt::registry& registry_;
    int64_t current_time_ms_ = 0;

    entt::entity create_skeleton(entt::entity owner, int skill_level,
                                 const mir2::common::Position& pos);
    entt::entity create_elf_monster(entt::entity owner, int skill_level,
                                    const mir2::common::Position& pos);
    void check_loyalty_expiration();
};

} // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_SYSTEMS_SUMMON_SYSTEM_H
