#include "common/compression.h"

#include <algorithm>
#include <limits>

#include <lz4.h>

namespace mir2::common {

std::vector<uint8_t> CompressLZ4(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return {};
    }
    if (size > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return {};
    }

    const int src_size = static_cast<int>(size);
    const int max_dst = LZ4_compressBound(src_size);
    if (max_dst <= 0) {
        return {};
    }

    std::vector<uint8_t> output(static_cast<size_t>(max_dst));
    const int compressed_size = LZ4_compress_default(
        reinterpret_cast<const char*>(data),
        reinterpret_cast<char*>(output.data()),
        src_size,
        max_dst);
    if (compressed_size <= 0) {
        return {};
    }

    output.resize(static_cast<size_t>(compressed_size));
    return output;
}

bool DecompressLZ4(const uint8_t* data,
                   size_t compressed_size,
                   std::vector<uint8_t>* out,
                   size_t max_decompressed_size) {
    if (!data || !out) {
        return false;
    }

    out->clear();
    if (compressed_size == 0) {
        return true;
    }
    if (compressed_size > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        max_decompressed_size > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        max_decompressed_size == 0) {
        return false;
    }

    constexpr size_t kMinDecodeBufferSize = 4 * 1024;
    size_t output_capacity = std::max(compressed_size, kMinDecodeBufferSize);
    if (output_capacity > max_decompressed_size) {
        output_capacity = max_decompressed_size;
    }

    while (true) {
        out->resize(output_capacity);
        const int decompressed_size = LZ4_decompress_safe(
            reinterpret_cast<const char*>(data),
            reinterpret_cast<char*>(out->data()),
            static_cast<int>(compressed_size),
            static_cast<int>(output_capacity));
        if (decompressed_size >= 0) {
            out->resize(static_cast<size_t>(decompressed_size));
            return true;
        }

        if (output_capacity >= max_decompressed_size) {
            out->clear();
            return false;
        }

        const size_t remaining = max_decompressed_size - output_capacity;
        const size_t growth = std::min(output_capacity, remaining);
        output_capacity += growth;
    }
}

}  // namespace mir2::common
