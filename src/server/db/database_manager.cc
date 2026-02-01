#include "db/database_manager.h"

namespace mir2::db {

bool DatabaseManager::Initialize(const config::DatabaseConfig& db_config,
                                 const config::RedisConfig& redis_config) {
  if (!pool_.Initialize(db_config)) {
    return false;
  }
  if (!redis_manager_.Initialize(redis_config)) {
    return false;
  }
  return true;
}

void DatabaseManager::Shutdown() {
  redis_manager_.Shutdown();
}

PgConnectionGuard DatabaseManager::AcquireConnection() {
  return PgConnectionGuard(pool_, pool_.Acquire());
}

}  // namespace mir2::db
