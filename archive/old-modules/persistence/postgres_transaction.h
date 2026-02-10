/**
 * @file postgres_transaction.h
 * @brief RAII transaction wrapper for PostgreSQL
 */

#ifndef MIR2_PERSISTENCE_POSTGRES_TRANSACTION_H
#define MIR2_PERSISTENCE_POSTGRES_TRANSACTION_H

#include "persistence/postgres_connection_pool.h"
#include <string>

namespace mir2::persistence {

#ifdef HAVE_LIBPQXX

/**
 * @brief RAII transaction wrapper
 *
 * Automatically rolls back on destruction if not committed.
 * Uses pqxx::work for transaction semantics.
 *
 * Thread safety: Not thread-safe. Each thread should use its own transaction.
 */
class PostgresTransaction {
 public:
    /**
     * @brief Start a new transaction
     * @param conn Pooled connection to use
     * @throws std::runtime_error if connection is invalid
     */
    explicit PostgresTransaction(PooledConnection& conn);

    /**
     * @brief Destructor - auto-rollback if not committed
     */
    ~PostgresTransaction();

    PostgresTransaction(const PostgresTransaction&) = delete;
    PostgresTransaction& operator=(const PostgresTransaction&) = delete;
    PostgresTransaction(PostgresTransaction&&) = delete;
    PostgresTransaction& operator=(PostgresTransaction&&) = delete;

    /**
     * @brief Commit the transaction
     * @throws pqxx::sql_error on database error
     */
    void Commit();

    /**
     * @brief Rollback the transaction
     */
    void Rollback();

    /**
     * @brief Get underlying pqxx::work object
     */
    pqxx::work& Work() { return *work_; }

    /**
     * @brief Check if transaction is still active
     */
    bool IsActive() const { return work_ != nullptr && !committed_ && !rolled_back_; }

    /**
     * @brief Execute a query
     * @param query SQL query string
     * @return Query result
     */
    pqxx::result Exec(const std::string& query);

    /**
     * @brief Execute parameterized query (prevents SQL injection)
     * @tparam Args Parameter types
     * @param query SQL query with $1, $2, etc. placeholders
     * @param args Query parameters
     * @return Query result
     */
    template<typename... Args>
    pqxx::result ExecParams(const std::string& query, Args&&... args) {
        return work_->exec_params(query, std::forward<Args>(args)...);
    }

 private:
    std::unique_ptr<pqxx::work> work_;
    bool committed_ = false;
    bool rolled_back_ = false;
};

#else  // !HAVE_LIBPQXX

// Stub implementation when libpqxx is not available
class PostgresTransaction {
 public:
    explicit PostgresTransaction(PooledConnection&) {}
    void Commit() {}
    void Rollback() {}
    bool IsActive() const { return false; }
};

#endif  // HAVE_LIBPQXX

}  // namespace mir2::persistence

#endif  // MIR2_PERSISTENCE_POSTGRES_TRANSACTION_H
