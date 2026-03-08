#include <storage_engine/utils/global_hybrid_clock.h>

#include <algorithm>
#include <chrono>

#include <spdlog/spdlog.h>

#include <storage_engine/l2/rocksdb_cache.h>
#include <storage_engine/types.h>

namespace mir2::storage_engine::utils {

GlobalHybridClock::GlobalHybridClock(l2::RocksDBCache* l2_cache)
    : l2_cache_(l2_cache) {
    if (l2_cache_) {
        RecoverFromPersistence();
    }
}

GlobalHybridClock::~GlobalHybridClock() = default;

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

        if (current_time_ms > physical) {
            uint64_t next = EncodeHLC(current_time_ms, 0);
            if (clock_.compare_exchange_strong(current, next,
                                               std::memory_order_release,
                                               std::memory_order_acquire)) {
                return next;
            }
            continue;
        }

        if (logical < 0xFFFFU) {
            uint64_t next = EncodeHLC(physical, logical + 1);
            if (clock_.compare_exchange_strong(current, next,
                                               std::memory_order_release,
                                               std::memory_order_acquire)) {
                return next;
            }
            continue;
        }

        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->warn("HLC logical counter overflow at time {}", physical);
        }

        uint64_t next = EncodeHLC(physical + 1, 0);
        if (clock_.compare_exchange_strong(current, next,
                                           std::memory_order_release,
                                           std::memory_order_acquire)) {
            return next;
        }
    }
}

void GlobalHybridClock::RecoverFromPersistence() {
    if (!l2_cache_) {
        return;
    }

    uint64_t max_version_from_db = ScanMaxVersionInDB();
    uint64_t current_time_ms = GetCurrentTimeMs();

    uint64_t physical_from_db = DecodeHLCPhysical(max_version_from_db);
    uint64_t init_physical = std::max(current_time_ms, physical_from_db);

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
    if (!l2_cache_) {
        return 0;
    }

    return l2_cache_->GetMaxVersion();
}

}  // namespace mir2::storage_engine::utils
