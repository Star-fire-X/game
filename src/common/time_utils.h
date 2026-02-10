/**
 * @file time_utils.h
 * @brief Common time utility helpers.
 */

#ifndef MIR2_COMMON_TIME_UTILS_H
#define MIR2_COMMON_TIME_UTILS_H

#include <chrono>
#include <cstdint>

namespace mir2::common {

// Returns the current timestamp in milliseconds.
inline int64_t now_ms() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             clock::now().time_since_epoch())
      .count();
}

}  // namespace mir2::common

#endif  // MIR2_COMMON_TIME_UTILS_H
