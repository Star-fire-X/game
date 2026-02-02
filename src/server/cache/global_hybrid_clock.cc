#include "global_hybrid_clock.h"

#include <chrono>
#include <spdlog/spdlog.h>

namespace mir2::cache {

GlobalHybridClock::GlobalHybridClock(RocksDBCache* l2_cache)
    : l2_cache_(l2_cache) {
    // 初始化时从持久化恢复（如果 L2 存在）
    if (l2_cache_) {
        RecoverFromPersistence();
    }
}

GlobalHybridClock::~GlobalHybridClock() {
    // 无特殊清理逻辑
}

uint64_t GlobalHybridClock::GetCurrentTimeMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

uint64_t GlobalHybridClock::Next() {
    uint64_t current = clock_.load(std::memory_order_acquire);
    uint64_t current_time_ms = GetCurrentTimeMs();

    while (true) {
        uint64_t physical = DecodeHLCPhysical(current);
        uint16_t logical = DecodeHLCLogical(current);

        // 情况 1: 系统时间比当前版本号的物理时间更新
        if (current_time_ms > physical) {
            // 新时间，逻辑计数器重置为 0
            uint64_t next = EncodeHLC(current_time_ms, 0);
            if (clock_.compare_exchange_strong(current, next,
                                               std::memory_order_release,
                                               std::memory_order_acquire)) {
                return next;
            }
            // CAS 失败，重试
            continue;
        }

        // 情况 2: 系统时间 <= 当前版本号的物理时间
        // 保持物理时间不变，递增逻辑计数器（防止时钟回退）
        if (logical < 0xFFFFU) {
            // 逻辑计数器未溢出
            uint64_t next = EncodeHLC(physical, logical + 1);
            if (clock_.compare_exchange_strong(current, next,
                                               std::memory_order_release,
                                               std::memory_order_acquire)) {
                return next;
            }
            // CAS 失败，重试
            continue;
        } else {
            // 逻辑计数器溢出（极端情况）
            // 等待系统时间推进或使用更大的时间跳跃
            // 为简单起见，这里强制推进物理时间
            auto logger = spdlog::get("mir2");
            if (logger) {
                logger->warn("HLC logical counter overflow at time {}", physical);
            }

            // 设置物理时间为 current + 1 毫秒
            uint64_t next = EncodeHLC(physical + 1, 0);
            if (clock_.compare_exchange_strong(current, next,
                                               std::memory_order_release,
                                               std::memory_order_acquire)) {
                return next;
            }
            // CAS 失败，重试
            continue;
        }
    }
}

void GlobalHybridClock::RecoverFromPersistence() {
    if (!l2_cache_) {
        return;
    }

    // 扫描 RocksDB 获取最大版本号
    uint64_t max_version_from_db = ScanMaxVersionInDB();

    // 读取当前系统时间
    uint64_t current_time_ms = GetCurrentTimeMs();

    // 选择最大值初始化 HLC
    uint64_t physical_from_db = DecodeHLCPhysical(max_version_from_db);
    uint64_t init_physical = std::max(current_time_ms, physical_from_db);

    // 初始化为恢复出的最大版本号
    uint64_t initial_hlc = EncodeHLC(init_physical, 0);
    clock_.store(initial_hlc, std::memory_order_release);

    auto logger = spdlog::get("mir2");
    if (logger) {
        logger->info(
            "HLC recovered: max_version_db={:#x}, current_time_ms={}, "
            "init_hlc={:#x}",
            max_version_from_db, current_time_ms, initial_hlc);
    }
}

uint64_t GlobalHybridClock::ScanMaxVersionInDB() {
    // 注意：这里实现需要依赖 RocksDBCache
    // 暂时返回 0，将由 RocksDBCache 初始化时调用实际实现
    return 0;
}

}  // namespace mir2::cache
