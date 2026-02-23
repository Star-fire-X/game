#ifndef MIR2_COMMON_COMPRESSION_H
#define MIR2_COMMON_COMPRESSION_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mir2::common {

constexpr size_t kCompressionThreshold = 512;

std::vector<uint8_t> CompressLZ4(const uint8_t* data, size_t size);

bool DecompressLZ4(const uint8_t* data,
                   size_t compressed_size,
                   std::vector<uint8_t>* out,
                   size_t max_decompressed_size);

}  // namespace mir2::common

#endif  // MIR2_COMMON_COMPRESSION_H
