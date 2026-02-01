/**
 * @file constants.h
 * @brief 公共常量
 */

#ifndef MIR2_COMMON_CONSTANTS_H
#define MIR2_COMMON_CONSTANTS_H

#include <cstdint>

namespace mir2::common {

constexpr uint16_t kDefaultServerPort = 7000;
constexpr int kDefaultTickIntervalMs = 50;
constexpr int kDefaultIoThreads = 4;
constexpr int kMaxConnections = 5000;

}  // namespace mir2::common

#endif  // MIR2_COMMON_CONSTANTS_H
