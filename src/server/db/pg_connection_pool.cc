#include "db/pg_connection_pool.h"

#include <iostream>

namespace mir2::db {

bool PgConnectionPool::Initialize(const config::DatabaseConfig& config) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (initialized_) {
    return true;
  }

  const std::string conn_str =
      "host=" + config.host + " port=" + std::to_string(config.port) + " user=" +
      config.user + " password=" + config.password + " dbname=" + config.database;

  for (int i = 0; i < config.pool_size; ++i) {
    try {
      auto conn = std::make_shared<pqxx::connection>(conn_str);
      if (!conn->is_open()) {
        std::cerr << "PostgreSQL connection failed" << std::endl;
        return false;
      }
      pool_.push(conn);
    } catch (const std::exception& ex) {
      std::cerr << "PostgreSQL init error: " << ex.what() << std::endl;
      return false;
    }
  }

  initialized_ = true;
  return true;
}

std::shared_ptr<pqxx::connection> PgConnectionPool::Acquire() {
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [this]() { return !pool_.empty(); });
  auto conn = pool_.front();
  pool_.pop();
  return conn;
}

void PgConnectionPool::Release(const std::shared_ptr<pqxx::connection>& connection) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(connection);
  }
  cv_.notify_one();
}

}  // namespace mir2::db
