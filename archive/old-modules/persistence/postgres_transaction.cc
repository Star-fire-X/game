/**
 * @file postgres_transaction.cc
 * @brief PostgreSQL transaction implementation
 */

#include "persistence/postgres_transaction.h"
#include "log/logger.h"

namespace mir2::persistence {

#ifdef HAVE_LIBPQXX

PostgresTransaction::PostgresTransaction(PooledConnection& conn) {
    if (!conn) {
        SYSLOG_ERROR("Cannot create transaction: invalid connection");
        throw std::runtime_error("Invalid connection for transaction");
    }

    try {
        work_ = std::make_unique<pqxx::work>(*conn);
        SYSLOG_DEBUG("Transaction started");
    } catch (const std::exception& e) {
        SYSLOG_ERROR("Failed to start transaction: {}", e.what());
        throw;
    }
}

PostgresTransaction::~PostgresTransaction() {
    if (work_ && !committed_ && !rolled_back_) {
        try {
            work_->abort();
            SYSLOG_WARN("Transaction auto-rolled back (destructor)");
        } catch (const std::exception& e) {
            SYSLOG_ERROR("Failed to rollback transaction: {}", e.what());
        }
    }
}

void PostgresTransaction::Commit() {
    if (!work_ || committed_ || rolled_back_) {
        SYSLOG_WARN("Commit called on inactive transaction");
        return;
    }

    try {
        work_->commit();
        committed_ = true;
        SYSLOG_DEBUG("Transaction committed");
    } catch (const pqxx::sql_error& e) {
        SYSLOG_ERROR("Transaction commit failed: {} (query: {})", e.what(), e.query());
        throw;
    }
}

void PostgresTransaction::Rollback() {
    if (!work_ || committed_ || rolled_back_) {
        return;
    }

    try {
        work_->abort();
        rolled_back_ = true;
        SYSLOG_DEBUG("Transaction rolled back");
    } catch (const std::exception& e) {
        SYSLOG_ERROR("Transaction rollback failed: {}", e.what());
    }
}

pqxx::result PostgresTransaction::Exec(const std::string& query) {
    if (!IsActive()) {
        throw std::runtime_error("Cannot execute on inactive transaction");
    }
    return work_->exec(query);
}

#endif  // HAVE_LIBPQXX

}  // namespace mir2::persistence
