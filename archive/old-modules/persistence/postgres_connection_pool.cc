/**
 * @file postgres_connection_pool.cc
 * @brief PostgreSQL connection pool implementation
 */

#include "persistence/postgres_connection_pool.h"
#include "log/logger.h"

namespace mir2::persistence {

#ifdef HAVE_LIBPQXX

PooledConnection::PooledConnection(std::unique_ptr<pqxx::connection> conn, ReturnCallback return_cb)
    : connection_(std::move(conn)), return_callback_(std::move(return_cb)) {}

PooledConnection::~PooledConnection() {
    if (connection_ && return_callback_) {
        return_callback_(std::move(connection_));
    }
}

PooledConnection::PooledConnection(PooledConnection&& other) noexcept
    : connection_(std::move(other.connection_)),
      return_callback_(std::move(other.return_callback_)) {
    other.return_callback_ = nullptr;
}

PooledConnection& PooledConnection::operator=(PooledConnection&& other) noexcept {
    if (this != &other) {
        if (connection_ && return_callback_) {
            return_callback_(std::move(connection_));
        }
        connection_ = std::move(other.connection_);
        return_callback_ = std::move(other.return_callback_);
        other.return_callback_ = nullptr;
    }
    return *this;
}

PostgresConnectionPool::PostgresConnectionPool(const Config& config)
    : config_(config) {
    SYSLOG_INFO("PostgreSQL connection pool initialized (size: {})", config_.pool_size);

    // Start health checker if interval is configured
    if (config_.health_check_interval_seconds > 0) {
        StartHealthChecker();
    }
}

PostgresConnectionPool::~PostgresConnectionPool() {
    StopHealthChecker();
    Shutdown();
}

std::unique_ptr<pqxx::connection> PostgresConnectionPool::CreateConnection() {
    try {
        auto conn = std::make_unique<pqxx::connection>(config_.connection_string);
        if (conn->is_open()) {
            SYSLOG_DEBUG("Created new PostgreSQL connection");
            return conn;
        }
        SYSLOG_ERROR("Failed to open PostgreSQL connection");
        return nullptr;
    } catch (const pqxx::broken_connection& e) {
        SYSLOG_ERROR("PostgreSQL connection error: {}", e.what());
        return nullptr;
    } catch (const std::exception& e) {
        SYSLOG_ERROR("Unexpected error creating connection: {}", e.what());
        return nullptr;
    }
}

void PostgresConnectionPool::ReturnConnection(std::unique_ptr<pqxx::connection> conn) {
    if (!conn) return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) {
        conn.reset();
        --in_use_;
        return;
    }

    try {
        if (conn->is_open()) {
            available_connections_.push(std::move(conn));
            --in_use_;
            available_cv_.notify_one();
            return;
        }
    } catch (...) {}

    --in_use_;
    --total_created_;
    SYSLOG_WARN("Discarded broken PostgreSQL connection");
}

PooledConnection PostgresConnectionPool::AcquireConnection() {
    std::unique_lock<std::mutex> lock(mutex_);

    auto timeout = std::chrono::milliseconds(config_.acquire_timeout_ms);
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (!shutdown_) {
        // Try to get an available connection
        if (!available_connections_.empty()) {
            auto conn = std::move(available_connections_.front());
            available_connections_.pop();

            try {
                if (conn->is_open()) {
                    ++in_use_;
                    return PooledConnection(
                        std::move(conn),
                        [this](std::unique_ptr<pqxx::connection> c) {
                            ReturnConnection(std::move(c));
                        });
                }
            } catch (...) {
                --total_created_;
            }
            continue;
        }

        // Create new connection if pool not full
        if (total_created_ < config_.pool_size) {
            ++total_created_;
            lock.unlock();

            auto conn = CreateConnection();
            if (conn) {
                lock.lock();
                ++in_use_;
                return PooledConnection(
                    std::move(conn),
                    [this](std::unique_ptr<pqxx::connection> c) {
                        ReturnConnection(std::move(c));
                    });
            }

            lock.lock();
            --total_created_;
        }

        // Wait for available connection
        if (available_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
            SYSLOG_WARN("Connection pool acquire timeout after {}ms", config_.acquire_timeout_ms);
            return PooledConnection{};
        }
    }

    return PooledConnection{};
}

bool PostgresConnectionPool::HasAvailable() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !available_connections_.empty() || total_created_ < config_.pool_size;
}

PostgresConnectionPool::Stats PostgresConnectionPool::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return Stats{
        .total_connections = total_created_,
        .available_connections = static_cast<uint32_t>(available_connections_.size()),
        .in_use_connections = in_use_,
        .health_checks_performed = health_checks_performed_,
        .stale_connections_removed = stale_connections_removed_
    };
}

bool PostgresConnectionPool::TestConnection() {
    auto conn = AcquireConnection();
    if (!conn) return false;

    try {
        pqxx::work txn(*conn);
        auto result = txn.exec("SELECT 1");
        txn.commit();
        return !result.empty() && result[0][0].as<int>() == 1;
    } catch (const std::exception& e) {
        SYSLOG_ERROR("Connection test failed: {}", e.what());
        return false;
    }
}

void PostgresConnectionPool::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_) return;

        shutdown_ = true;
        SYSLOG_INFO("Shutting down PostgreSQL connection pool");

        while (!available_connections_.empty()) {
            available_connections_.pop();
        }
    }
    available_cv_.notify_all();
    health_checker_cv_.notify_all();
}

void PostgresConnectionPool::StartHealthChecker() {
    if (health_checker_running_.exchange(true)) {
        return;  // Already running
    }

    health_checker_thread_ = std::thread([this]() {
        HealthCheckerLoop();
    });

    SYSLOG_INFO("Connection pool health checker started (interval: {}s)",
                config_.health_check_interval_seconds);
}

void PostgresConnectionPool::StopHealthChecker() {
    if (!health_checker_running_.exchange(false)) {
        return;  // Not running
    }

    health_checker_cv_.notify_all();

    if (health_checker_thread_.joinable()) {
        health_checker_thread_.join();
    }

    SYSLOG_INFO("Connection pool health checker stopped");
}

void PostgresConnectionPool::HealthCheckerLoop() {
    while (health_checker_running_) {
        // Wait for interval or shutdown signal
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto wait_time = std::chrono::seconds(config_.health_check_interval_seconds);

            if (health_checker_cv_.wait_for(lock, wait_time, [this]() {
                return !health_checker_running_ || shutdown_;
            })) {
                break;  // Signaled to stop
            }
        }

        if (!health_checker_running_ || shutdown_) {
            break;
        }

        // Check available connections
        std::vector<std::unique_ptr<pqxx::connection>> healthy_connections;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++health_checks_performed_;

            size_t checked = 0;
            size_t removed = 0;

            while (!available_connections_.empty()) {
                auto conn = std::move(available_connections_.front());
                available_connections_.pop();
                ++checked;

                if (IsConnectionHealthy(*conn)) {
                    healthy_connections.push_back(std::move(conn));
                } else {
                    ++removed;
                    ++stale_connections_removed_;
                    --total_created_;
                }
            }

            // Return healthy connections to pool
            for (auto& conn : healthy_connections) {
                available_connections_.push(std::move(conn));
            }

            if (removed > 0) {
                SYSLOG_WARN("Health check removed {} stale connections (checked: {})",
                           removed, checked);
            } else {
                SYSLOG_DEBUG("Health check passed for {} connections", checked);
            }
        }
    }
}

bool PostgresConnectionPool::IsConnectionHealthy(pqxx::connection& conn) {
    try {
        if (!conn.is_open()) {
            return false;
        }

        // Execute simple query to verify connection
        pqxx::nontransaction ntx(conn);
        auto result = ntx.exec("SELECT 1");
        return !result.empty() && result[0][0].as<int>() == 1;
    } catch (const std::exception& e) {
        SYSLOG_DEBUG("Connection health check failed: {}", e.what());
        return false;
    }
}

#endif  // HAVE_LIBPQXX

}  // namespace mir2::persistence
