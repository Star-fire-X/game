#ifndef MIR2_STORAGE_ENGINE_TYPES_H_
#define MIR2_STORAGE_ENGINE_TYPES_H_

#include <cstdint>

namespace mir2::storage_engine {

// HLC (Hybrid Logical Clock) encoding helpers.
// Format: [48 bits physical time(ms)][16 bits logical counter]
constexpr uint64_t EncodeHLC(uint64_t physical_ms, uint16_t logical_counter) {
    return (physical_ms << 16) | logical_counter;
}

constexpr uint64_t DecodeHLCPhysical(uint64_t hlc) {
    return hlc >> 16;
}

constexpr uint16_t DecodeHLCLogical(uint64_t hlc) {
    return static_cast<uint16_t>(hlc & 0xFFFFUL);
}

}  // namespace mir2::storage_engine

#endif  // MIR2_STORAGE_ENGINE_TYPES_H_
