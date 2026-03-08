#ifndef MIR2_SERVER_CACHE_GLOBAL_HYBRID_CLOCK_H_
#define MIR2_SERVER_CACHE_GLOBAL_HYBRID_CLOCK_H_

#include <atomic>
#include <cstdint>
#include <memory>

namespace mir2::cache {

class RocksDBCache;  // 前向声明

// 全局无锁混合逻辑时钟 (Hybrid Logical Clock)
// 使用无锁 CAS 操作生成单调递增版本号
// HLC 编码: [48 bits 物理时间(ms)][16 bits 逻辑计数器]
class GlobalHybridClock {
public:
    // 构造函数，可传入 L2 缓存用于崩溃恢复
    explicit GlobalHybridClock(RocksDBCache* l2_cache = nullptr);

    // 析构函数
    ~GlobalHybridClock();

    // 生成下一个版本号（无锁 CAS 操作）
    // 性能目标：>1M ops/s
    uint64_t Next();

    // 从崩溃恢复：扫描 RocksDB 获取最大版本号
    void RecoverFromPersistence();

    // 获取当前版本号（仅读，无原子操作）
    uint64_t GetCurrent() const {
        return clock_.load(std::memory_order_acquire);
    }

    // 提取版本号的物理时间部分（毫秒）
    static uint64_t GetPhysicalTime(uint64_t version) {
        return version >> 16;
    }

    // 提取版本号的逻辑计数器部分
    static uint16_t GetLogicalCounter(uint64_t version) {
        return static_cast<uint16_t>(version & 0xFFFFUL);
    }

private:
    // 扫描 RocksDB 获取最大版本号（用于恢复）
    uint64_t ScanMaxVersionInDB();

    // 获取当前系统时间（毫秒）
    uint64_t GetCurrentTimeMs() const;

    // 原子版本号存储
    std::atomic<uint64_t> clock_{0};

    // L2 缓存引用（用于恢复）
    RocksDBCache* l2_cache_;
};

}  // namespace mir2::cache

#endif  // MIR2_SERVER_CACHE_GLOBAL_HYBRID_CLOCK_H_
