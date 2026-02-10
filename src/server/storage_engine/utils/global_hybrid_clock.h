#ifndef MIR2_STORAGE_ENGINE_UTILS_GLOBAL_HYBRID_CLOCK_H_
#define MIR2_STORAGE_ENGINE_UTILS_GLOBAL_HYBRID_CLOCK_H_

#include <atomic>
#include <cstdint>

namespace mir2::storage_engine::l2 {
class RocksDBCache;
}  // namespace mir2::storage_engine::l2

namespace mir2::storage_engine::utils {

// Global Hybrid Logical Clock (HLC).
class GlobalHybridClock {
public:
    explicit GlobalHybridClock(l2::RocksDBCache* l2_cache = nullptr);
    ~GlobalHybridClock();

    uint64_t Next();

    void RecoverFromPersistence();

    uint64_t GetCurrent() const {
        return clock_.load(std::memory_order_acquire);
    }

    static uint64_t GetPhysicalTime(uint64_t version) {
        return version >> 16;
    }

    static uint16_t GetLogicalCounter(uint64_t version) {
        return static_cast<uint16_t>(version & 0xFFFFUL);
    }

private:
    uint64_t ScanMaxVersionInDB();
    uint64_t GetCurrentTimeMs() const;

    std::atomic<uint64_t> clock_{0};
    l2::RocksDBCache* l2_cache_;
};

}  // namespace mir2::storage_engine::utils

#endif  // MIR2_STORAGE_ENGINE_UTILS_GLOBAL_HYBRID_CLOCK_H_
