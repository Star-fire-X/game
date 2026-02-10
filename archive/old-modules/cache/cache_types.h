#ifndef MIR2_SERVER_CACHE_CACHE_TYPES_H_
#define MIR2_SERVER_CACHE_CACHE_TYPES_H_

#include <cstdint>
#include <memory>
#include <vector>

namespace mir2::cache {

// HLC (Hybrid Logical Clock) 编码版本号
// 格式: [48 bits 物理时间(ms)][16 bits 逻辑计数器]
// 范围: 0x0000_0000_0000_0000 ~ 0xFFFF_FFFF_FFFF_FFFF

// 版本号编码：时间戳 + 逻辑序列号
constexpr uint64_t EncodeHLC(uint64_t physical_ms, uint16_t logical_counter) {
    return (physical_ms << 16) | logical_counter;
}

// 版本号解码：提取物理时间部分
constexpr uint64_t DecodeHLCPhysical(uint64_t hlc) {
    return hlc >> 16;
}

// 版本号解码：提取逻辑计数器部分
constexpr uint16_t DecodeHLCLogical(uint64_t hlc) {
    return static_cast<uint16_t>(hlc & 0xFFFFUL);
}

// 优先级枚举
enum class Priority : uint8_t {
    HIGH = 0,     // 立即提交 (<100ms)
    NORMAL = 1    // 批量提交 (30s)
};

// 带版本的数据容器
struct VersionedData {
    // 使用 shared_ptr 实现零拷贝共享
    using DataPtr = std::shared_ptr<const std::vector<uint8_t>>;

    uint64_t version;           // HLC 编码的版本号
    DataPtr data;               // 数据指针（零拷贝）
    uint64_t timestamp_ms;      // 写入时间戳（毫秒）

    // 默认构造
    VersionedData() : version(0), data(nullptr), timestamp_ms(0) {}

    // 带参数构造
    VersionedData(uint64_t v, DataPtr d, uint64_t ts)
        : version(v), data(std::move(d)), timestamp_ms(ts) {}

    // 检查数据有效性
    bool IsValid() const { return data != nullptr; }

    // 获取数据大小
    size_t GetSize() const { return data ? data->size() : 0; }
};

// 缓存统计信息
struct CacheStats {
    uint64_t hits = 0;      // 缓存命中次数
    uint64_t misses = 0;    // 缓存未命中次数
    uint64_t writes = 0;    // 写入次数

    // 获取命中率
    double GetHitRatio() const {
        uint64_t total = hits + misses;
        return total > 0 ? static_cast<double>(hits) / total : 0.0;
    }

    // 重置统计
    void Reset() {
        hits = 0;
        misses = 0;
        writes = 0;
    }
};

}  // namespace mir2::cache

#endif  // MIR2_SERVER_CACHE_CACHE_TYPES_H_
