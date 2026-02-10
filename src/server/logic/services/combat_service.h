/**
 * @file combat_service.h
 * @brief Combat service contract for logic handlers.
 */

#ifndef MIR2_LOGIC_SERVICES_COMBAT_SERVICE_H_
#define MIR2_LOGIC_SERVICES_COMBAT_SERVICE_H_

#include <cstdint>

#include "server/common/error_codes.h"

namespace mir2::logic {

/**
 * @brief Combat execution result.
 */
struct CombatResult {
  mir2::common::ErrorCode code = mir2::common::ErrorCode::kOk;
  int damage = 0;
  int healing = 0;
  int target_hp = 0;
  bool target_dead = false;
};

/**
 * @brief Combat service interface consumed by attack/skill handlers.
 */
class CombatService {
 public:
  virtual ~CombatService() = default;

  virtual CombatResult Attack(uint64_t attacker_id, uint64_t target_id) = 0;
  virtual CombatResult UseSkill(uint64_t caster_id, uint64_t target_id, uint32_t skill_id) = 0;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_SERVICES_COMBAT_SERVICE_H_
