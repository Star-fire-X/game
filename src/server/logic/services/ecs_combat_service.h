/**
 * @file ecs_combat_service.h
 * @brief ECS combat service adapter.
 */

#ifndef MIR2_LOGIC_SERVICES_ECS_COMBAT_SERVICE_H_
#define MIR2_LOGIC_SERVICES_ECS_COMBAT_SERVICE_H_

#include "logic/services/combat_service.h"

namespace mir2::ecs {
class RegistryManager;
class World;
}  // namespace mir2::ecs

namespace mir2::logic {

class EcsCombatService : public CombatService {
 public:
  explicit EcsCombatService(mir2::ecs::RegistryManager& registry_manager);

  CombatResult Attack(uint64_t attacker_id, uint64_t target_id) override;
  CombatResult UseSkill(uint64_t caster_id, uint64_t target_id, uint32_t skill_id) override;

 private:
  mir2::ecs::RegistryManager& registry_manager_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_SERVICES_ECS_COMBAT_SERVICE_H_
