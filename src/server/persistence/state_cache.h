/**
 * @file state_cache.h
 * @brief Redis hot data cache (Epic 4: Story 4.2)
 */

#ifndef MIR2_PERSISTENCE_STATE_CACHE_H
#define MIR2_PERSISTENCE_STATE_CACHE_H

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mir2::persistence {

/**
 * @brief Redis state cache (Story 4.2)
 *
 * Features:
 * - Write-Through consistency (write to cache and DB)
 * - Read fallback: Memory -> Redis -> PostgreSQL
 * - TTL management for inactive entities
 * - Graceful degradation on Redis failure
 * - Cache hit ratio monitoring
 *
 * Thread-safe for concurrent operations.
 */
class StateCache {
 public:
    /**
     * @brief Cache configuration
     */
    struct Config {
        std::string redis_host = "localhost";
        int redis_port = 6379;
        uint32_t ttl_seconds = 3600;  // 1 hour default
        bool enabled = true;
    };

    /**
     * @brief Create state cache
     * @param config Cache configuration
     */
    explicit StateCache(const Config& config = Config{});

    ~StateCache() = default;

    /**
     * @brief Write entity state (Write-Through)
     * @param key Cache key (e.g., "entity:123:data")
     * @param data Serialized entity
     * @return true if successful
     */
    bool Write(const std::string& key, const std::vector<uint8_t>& data);

    /**
     * @brief Read entity state (with fallback)
     * @param key Cache key
     * @return Data if found, nullopt if not found
     */
    std::optional<std::vector<uint8_t>> Read(const std::string& key);

    /**
     * @brief Batch read operations
     */
    std::map<std::string, std::vector<uint8_t>> ReadBatch(
        const std::vector<std::string>& keys);

    /**
     * @brief Set TTL for a key
     */
    bool SetTTL(const std::string& key, uint32_t seconds);

    /**
     * @brief Delete a key
     */
    bool Delete(const std::string& key);

    /**
     * @brief Check if key exists
     */
    bool Exists(const std::string& key);

    /**
     * @brief Flush all cache
     */
    bool Flush();

    /**
     * @brief Check if cache is ready
     */
    bool IsReady() const { return is_ready_; }

    /**
     * @brief Get cache statistics
     */
    struct CacheStats {
        uint64_t hits = 0;
        uint64_t misses = 0;
        uint64_t writes = 0;
        uint64_t evictions = 0;

        double GetHitRatio() const {
            uint64_t total = hits + misses;
            return total > 0 ? (double)hits / total : 0.0;
        }
    };

    CacheStats GetStats() const {
        return stats_;
    }

 private:
    Config config_;
    std::atomic<bool> is_ready_{false};
    CacheStats stats_;

    /**
     * @brief Initialize Redis connection
     */
    bool Initialize();

    /**
     * @brief Handle Redis connection failure
     */
    void OnRedisFailure();
};

}  // namespace mir2::persistence

#endif  // MIR2_PERSISTENCE_STATE_CACHE_H
