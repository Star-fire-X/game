#include "storage_engine/backends/storage_engine_backend.h"

#include <chrono>
#include <cstddef>

#include <pqxx/pqxx>
#include <spdlog/spdlog.h>

namespace mir2::db {

namespace {
int64_t ElapsedMs(std::chrono::steady_clock::time_point start) {
  auto elapsed = std::chrono::steady_clock::now() - start;
  return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}

constexpr const char* kUpsertSQL =
    "INSERT INTO kv_store (key, version, data, updated_at) "
    "VALUES ($1, $2, $3, NOW()) "
    "ON CONFLICT (key) DO UPDATE "
    "SET version = EXCLUDED.version, data = EXCLUDED.data, updated_at = NOW() "
    "WHERE kv_store.version < EXCLUDED.version";
}  // namespace

StorageEngineBackend::StorageEngineBackend(const config::DatabaseConfig& db_config)
    : StorageEngineBackend(db_config, std::make_shared<PgConnectionPool>()) {}

StorageEngineBackend::StorageEngineBackend(
    const config::DatabaseConfig& db_config,
    std::shared_ptr<PgConnectionPool> pool)
    : db_config_(db_config),
      pool_(std::move(pool)) {}

bool StorageEngineBackend::Initialize() {
  if (initialized_) {
    return true;
  }

  if (!pool_) {
    pool_ = std::make_shared<PgConnectionPool>();
  }
  if (!pool_->Initialize(db_config_)) {
    return false;
  }

  initialized_ = true;
  return true;
}

mir2::storage_engine::IStorageBackend::StorageResult StorageEngineBackend::Save(
    const std::string& key,
    uint64_t version,
    const std::vector<uint8_t>& data) {
  if (!IsHealthy()) {
    return StorageResult{false, "backend not healthy", 0};
  }

  const auto start = std::chrono::steady_clock::now();
  try {
    auto conn = pool_->Acquire();
    if (!conn) {
      return StorageResult{false, "failed to acquire connection", ElapsedMs(start)};
    }

    PgConnectionGuard guard(*pool_, conn);
    pqxx::work txn(*conn);

    txn.exec(kUpsertSQL,
             pqxx::params{
                 key, static_cast<int64_t>(version), pqxx::binary_cast(data)});
    txn.commit();

    return StorageResult{true, "", ElapsedMs(start)};
  } catch (const std::exception& ex) {
    return StorageResult{false, ex.what(), ElapsedMs(start)};
  }
}

mir2::storage_engine::IStorageBackend::StorageResult StorageEngineBackend::SaveBatch(
    const std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>>& items) {
  if (!IsHealthy()) {
    return StorageResult{false, "backend not healthy", 0};
  }

  if (items.empty()) {
    return StorageResult{true, "", 0};
  }

  const auto start = std::chrono::steady_clock::now();
  try {
    auto conn = pool_->Acquire();
    if (!conn) {
      return StorageResult{false, "failed to acquire connection", ElapsedMs(start)};
    }

    PgConnectionGuard guard(*pool_, conn);
    pqxx::work txn(*conn);

    for (const auto& [key, version, data] : items) {
      txn.exec(kUpsertSQL,
               pqxx::params{
                   key, static_cast<int64_t>(version), pqxx::binary_cast(data)});
    }
    txn.commit();

    return StorageResult{true, "", ElapsedMs(start)};
  } catch (const std::exception& ex) {
    return StorageResult{false, ex.what(), ElapsedMs(start)};
  }
}

std::optional<std::pair<uint64_t, std::vector<uint8_t>>> StorageEngineBackend::Load(
    const std::string& key) {
  auto logger = spdlog::get("mir2");

  if (!IsHealthy()) {
    if (logger) {
      logger->warn("StorageEngineBackend::Load: backend not healthy for key '{}'", key);
    }
    return std::nullopt;
  }

  try {
    auto conn = pool_->Acquire();
    if (!conn) {
      if (logger) {
        logger->error("StorageEngineBackend::Load: failed to acquire connection for key '{}'", key);
      }
      return std::nullopt;
    }

    PgConnectionGuard guard(*pool_, conn);
    pqxx::work txn(*conn);
    pqxx::result result = txn.exec(
        "SELECT version, data FROM kv_store WHERE key = $1",
        pqxx::params{key});

    if (result.empty()) {
      return std::nullopt;
    }

    const auto& row = result[0];
    uint64_t version = static_cast<uint64_t>(row[0].as<int64_t>());
    const pqxx::bytes blob = row[1].as<pqxx::bytes>();
    std::vector<uint8_t> data;
    data.reserve(blob.size());
    for (std::byte value : blob) {
      data.push_back(std::to_integer<uint8_t>(value));
    }

    return std::make_pair(version, std::move(data));
  } catch (const pqxx::broken_connection& ex) {
    if (logger) {
      logger->error("StorageEngineBackend::Load: connection lost for key '{}': {}", key, ex.what());
    }
    return std::nullopt;
  } catch (const pqxx::sql_error& ex) {
    if (logger) {
      logger->error("StorageEngineBackend::Load: SQL error for key '{}': {}", key, ex.what());
    }
    return std::nullopt;
  } catch (const std::exception& ex) {
    if (logger) {
      logger->error("StorageEngineBackend::Load: unexpected error for key '{}': {}", key, ex.what());
    }
    return std::nullopt;
  }
}

std::optional<std::map<std::string, std::pair<uint64_t, std::vector<uint8_t>>>>
StorageEngineBackend::LoadAll() {
  auto logger = spdlog::get("mir2");

  if (!IsHealthy()) {
    if (logger) {
      logger->warn("StorageEngineBackend::LoadAll: backend not healthy");
    }
    return std::nullopt;
  }

  try {
    auto conn = pool_->Acquire();
    if (!conn) {
      if (logger) {
        logger->error("StorageEngineBackend::LoadAll: failed to acquire connection");
      }
      return std::nullopt;
    }

    PgConnectionGuard guard(*pool_, conn);
    pqxx::work txn(*conn);
    pqxx::result result = txn.exec("SELECT key, version, data FROM kv_store");

    std::map<std::string, std::pair<uint64_t, std::vector<uint8_t>>> all;
    for (const auto& row : result) {
      std::string key = row[0].as<std::string>();
      uint64_t version = static_cast<uint64_t>(row[1].as<int64_t>());
      const pqxx::bytes blob = row[2].as<pqxx::bytes>();
      std::vector<uint8_t> data;
      data.reserve(blob.size());
      for (std::byte value : blob) {
        data.push_back(std::to_integer<uint8_t>(value));
      }
      all.emplace(std::move(key), std::make_pair(version, std::move(data)));
    }

    return all;
  } catch (const pqxx::broken_connection& ex) {
    if (logger) {
      logger->error("StorageEngineBackend::LoadAll: connection lost: {}", ex.what());
    }
    return std::nullopt;
  } catch (const pqxx::sql_error& ex) {
    if (logger) {
      logger->error("StorageEngineBackend::LoadAll: SQL error: {}", ex.what());
    }
    return std::nullopt;
  } catch (const std::exception& ex) {
    if (logger) {
      logger->error("StorageEngineBackend::LoadAll: unexpected error: {}", ex.what());
    }
    return std::nullopt;
  }
}

mir2::storage_engine::IStorageBackend::StorageResult StorageEngineBackend::Validate() {
  if (!IsHealthy()) {
    return StorageResult{false, "StorageEngineBackend not initialized", 0};
  }
  return StorageResult{true, "", 0};
}

bool StorageEngineBackend::IsHealthy() const {
  return initialized_ && pool_ && pool_->IsReady();
}

}  // namespace mir2::db
