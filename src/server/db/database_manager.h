/**
 * @file database_manager.h
 * @brief 数据库管理器
 */

#ifndef MIR2_DB_DATABASE_MANAGER_H
#define MIR2_DB_DATABASE_MANAGER_H

#include <string>

#include "db/pg_connection_pool.h"
#include "db/redis_manager.h"

namespace mir2::db {

/**
 * @brief 数据库管理器
 */
class DatabaseManager {
 public:
  /**
   * @brief 初始化数据库与缓存
   */
  bool Initialize(const config::DatabaseConfig& db_config, const config::RedisConfig& redis_config);

  /**
   * @brief 关闭所有资源
   */
  void Shutdown();

  /**
   * @brief 获取PostgreSQL连接守卫
   */
  PgConnectionGuard AcquireConnection();

  /**
   * @brief 获取Redis管理器
   */
  RedisManager& GetRedisManager() { return redis_manager_; }

 private:
  PgConnectionPool pool_;
  RedisManager redis_manager_;
};

}  // namespace mir2::db

#endif  // MIR2_DB_DATABASE_MANAGER_H
