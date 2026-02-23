#include "storage_engine/backends/postgres/pg_connection_pool.h"

#include <algorithm>
#include <iostream>

#include "monitor/metrics.h"

namespace mir2::db {

namespace {
constexpr const char* kDbPoolInUseMetric = "storage.db.pool.in_use";
constexpr const char* kDbPoolAvailableMetric = "storage.db.pool.available";
constexpr const char* kDbPoolUsageRatioMetric = "storage.db.pool.usage_ratio";
}  // namespace

bool PgConnectionPool::Initialize(const config::DatabaseConfig& config) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (initialized_) {
    return true;
  }

  configured_pool_size_ = static_cast<size_t>(std::max(0, config.pool_size));
  in_use_count_ = 0;

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
  UpdateMetricsLocked();
  return true;
}

std::shared_ptr<pqxx::connection> PgConnectionPool::Acquire() {
  std::unique_lock<std::mutex> lock(mutex_);
  // A zero-sized pool is used by some test/runtime configs to disable DB I/O.
  // Do not block forever in that case.
  if (!initialized_ || configured_pool_size_ == 0) {
    return nullptr;
  }
  cv_.wait(lock, [this]() { return !pool_.empty(); });
  auto conn = pool_.front();
  pool_.pop();
  ++in_use_count_;
  UpdateMetricsLocked();
  return conn;
}

void PgConnectionPool::Release(const std::shared_ptr<pqxx::connection>& connection) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(connection);
    if (in_use_count_ > 0) {
      --in_use_count_;
    }
    UpdateMetricsLocked();
  }
  cv_.notify_one();
}

size_t PgConnectionPool::PoolSize() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return configured_pool_size_;
}

size_t PgConnectionPool::InUseCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return in_use_count_;
}

void PgConnectionPool::UpdateMetricsLocked() {
  const size_t available_count = pool_.size();
  monitor::Metrics::Instance().SetGauge(
      kDbPoolInUseMetric, static_cast<double>(in_use_count_));
  monitor::Metrics::Instance().SetGauge(
      kDbPoolAvailableMetric, static_cast<double>(available_count));

  const double ratio =
      configured_pool_size_ == 0
          ? 0.0
          : static_cast<double>(in_use_count_) /
                static_cast<double>(configured_pool_size_);
  monitor::Metrics::Instance().SetGauge(kDbPoolUsageRatioMetric, ratio);
}

}  // namespace mir2::db
