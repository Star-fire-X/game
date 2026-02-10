/**
 * @file postgres_connection_pool.h
 * @brief PostgreSQL connection pool with RAII semantics
 */

#ifndef MIR2_PERSISTENCE_POSTGRES_CONNECTION_POOL_H
#define MIR2_PERSISTENCE_POSTGRES_CONNECTION_POOL_H

#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <string>
#include <chrono>
#include <functional>
#include <thread>
#include <atomic>

#ifdef HAVE_LIBPQXX
#include <pqxx/pqxx>
#endif

namespace mir2::persistence {

#ifdef HAVE_LIBPQXX

/**
 * @brief RAII wrapper for pooled connections
 *
 * Automatically returns connection to pool on destruction.
 */
class PooledConnection {
 public:
    using ReturnCallback = std::function<void(std::unique_ptr<pqxx::connection>)>;

    PooledConnection() = default;
    PooledConnection(std::unique_ptr<pqxx::connection> conn, ReturnCallback return_cb);
    ~PooledConnection();

    PooledConnection(PooledConnection&& other) noexcept;
    PooledConnection& operator=(PooledConnection&& other) noexcept;
    PooledConnection(const PooledConnection&) = delete;
    PooledConnection& operator=(const PooledConnection&) = delete;

    pqxx::connection* get() const { return connection_.get(); }
    pqxx::connection& operator*() const { return *connection_; }
    pqxx::connection* operator->() const { return connection_.get(); }
    explicit operator bool() const { return connection_ != nullptr; }

 private:
    std::unique_ptr<pqxx::connection> connection_;
    ReturnCallback return_callback_;
};

/**
 * @brief Thread-safe PostgreSQL connection pool
 *
 * Features:
 * - Configurable pool size
 * - Automatic connection creation on demand
 * - Connection health checking
 * - Graceful shutdown
 */
class PostgresConnectionPool {
 public:
    struct Config {
        std::string connection_string;
        uint32_t pool_size = 10;
        uint32_t acquire_timeout_ms = 5000;
        uint32_t connection_timeout_ms = 5000;
        uint32_t health_check_interval_seconds = 30;  // 0 to disable
    };

    explicit PostgresConnectionPool(const Config& config);
    ~PostgresConnectionPool();

    PostgresConnectionPool(const PostgresConnectionPool&) = delete;
    PostgresConnectionPool& operator=(const PostgresConnectionPool&) = delete;

    /**
     * @brief Acquire a connection from the pool
     * @return PooledConnection that auto-returns on destruction
     */
    PooledConnection AcquireConnection();

    /**
     * @brief Check if connections are available
     */
    bool HasAvailable() const;

    struct Stats {
        uint32_t total_connections;
        uint32_t available_connections;
        uint32_t in_use_connections;
        uint32_t health_checks_performed;
        uint32_t stale_connections_removed;
    };
    Stats GetStats() const;

    /**
     * @brief Test connection health
     */
    bool TestConnection();

    /**
     * @brief Start background health checker thread
     */
    void StartHealthChecker();

    /**
     * @brief Stop background health checker thread
     */
    void StopHealthChecker();

    /**
     * @brief Shutdown pool and close all connections
     */
    void Shutdown();

 private:
    Config config_;
    mutable std::mutex mutex_;
    std::condition_variable available_cv_;
    std::queue<std::unique_ptr<pqxx::connection>> available_connections_;
    uint32_t total_created_ = 0;
    uint32_t in_use_ = 0;
    bool shutdown_ = false;

    // Health checker
    std::thread health_checker_thread_;
    std::atomic<bool> health_checker_running_{false};
    std::condition_variable health_checker_cv_;
    uint32_t health_checks_performed_ = 0;
    uint32_t stale_connections_removed_ = 0;

    std::unique_ptr<pqxx::connection> CreateConnection();
    void ReturnConnection(std::unique_ptr<pqxx::connection> conn);
    void HealthCheckerLoop();
    bool IsConnectionHealthy(pqxx::connection& conn);
};

#else  // !HAVE_LIBPQXX

// Stub implementation when libpqxx is not available
class PooledConnection {
 public:
    explicit operator bool() const { return false; }
};

class PostgresConnectionPool {
 public:
    struct Config {
        std::string connection_string;
        uint32_t pool_size = 10;
        uint32_t acquire_timeout_ms = 5000;
        uint32_t connection_timeout_ms = 5000;
        uint32_t health_check_interval_seconds = 30;
    };

    explicit PostgresConnectionPool(const Config&) {}
    PooledConnection AcquireConnection() { return PooledConnection{}; }
    bool HasAvailable() const { return false; }
    struct Stats {
        uint32_t total_connections = 0;
        uint32_t available_connections = 0;
        uint32_t in_use_connections = 0;
        uint32_t health_checks_performed = 0;
        uint32_t stale_connections_removed = 0;
    };
    Stats GetStats() const { return {}; }
    bool TestConnection() { return false; }
    void StartHealthChecker() {}
    void StopHealthChecker() {}
    void Shutdown() {}
};

#endif  // HAVE_LIBPQXX

}  // namespace mir2::persistence

#endif  // MIR2_PERSISTENCE_POSTGRES_CONNECTION_POOL_H
