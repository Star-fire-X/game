/**
 * @file role_record.h
 * @brief Session-scoped role snapshot metadata.
 */

#ifndef MIR2_LOGIC_SERVICES_ROLE_RECORD_H_
#define MIR2_LOGIC_SERVICES_ROLE_RECORD_H_

#include <cstdint>
#include <string>

namespace mir2::logic {

struct RoleRecord {
  uint64_t player_id = 0;
  std::string name;
  uint8_t profession = 0;
  uint8_t gender = 0;
  uint16_t level = 1;
  uint32_t map_id = 1;
  int x = 100;
  int y = 100;
  uint64_t gold = 0;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_SERVICES_ROLE_RECORD_H_
