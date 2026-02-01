/**
 * @file pg_connection_pool.h
 * @brief PostgreSQL连接池
 */

#ifndef MIR2_DB_PG_CONNECTION_POOL_H
#define MIR2_DB_PG_CONNECTION_POOL_H

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

#include <pqxx/pqxx>

#include "config/config_manager.h"

namespace mir2::db {

/**
 * @brief PostgreSQL连接池
 */
class PgConnectionPool {
 public:
  /**
   * @brief 初始化连接池
   */
  bool Initialize(const config::DatabaseConfig& config);

  /**
   * @brief 获取连接
   */
  std::shared_ptr<pqxx::connection> Acquire();

  /**
   * @brief 归还连接
   */
  void Release(const std::shared_ptr<pqxx::connection>& connection);

  bool IsReady() const { return initialized_; }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<std::shared_ptr<pqxx::connection>> pool_;
  bool initialized_ = false;
};

/**
 * @brief 连接守卫，自动归还连接
 */
class PgConnectionGuard {
 public:
  PgConnectionGuard(PgConnectionPool& pool, std::shared_ptr<pqxx::connection> conn)
      : pool_(pool), conn_(std::move(conn)) {}
  ~PgConnectionGuard() {
    if (conn_) {
      pool_.Release(conn_);
    }
  }

  pqxx::connection* operator->() { return conn_.get(); }
  explicit operator bool() const { return conn_ != nullptr; }

 private:
  PgConnectionPool& pool_;
  std::shared_ptr<pqxx::connection> conn_;
};

}  // namespace mir2::db

#endif  // MIR2_DB_PG_CONNECTION_POOL_H
