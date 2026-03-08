/**
 * @file var_ref.h
 * @brief Variable-length payload reference for side channel.
 */

#ifndef MIR2_LOGIC_EVENTS_VAR_REF_H_
#define MIR2_LOGIC_EVENTS_VAR_REF_H_

#include <cstdint>
#include <limits>

namespace mir2::logic::events {

// P0 placeholder for variable-length bypass payload references.
struct VarRef {
  uint16_t producer_id = std::numeric_limits<uint16_t>::max();
  uint16_t block_index = std::numeric_limits<uint16_t>::max();
  uint32_t offset = 0;
  uint32_t length = 0;
  uint32_t generation = 0;
};

static_assert(sizeof(VarRef) == 16, "VarRef must stay compact.");

}  // namespace mir2::logic::events

#endif  // MIR2_LOGIC_EVENTS_VAR_REF_H_
