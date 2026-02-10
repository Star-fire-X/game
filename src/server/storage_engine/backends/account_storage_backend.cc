#include "storage_engine/backends/account_storage_backend.h"

#include <pqxx/pqxx>
#include <spdlog/spdlog.h>

#include "common/types/database_types.h"
#include "storage_engine/backends/common/account_storage_codec.h"

namespace mir2::db {

namespace {
constexpr uint64_t kDefaultVersion = 1;
}

AccountStorageBackend::AccountStorageBackend(
    std::unique_ptr<mir2::storage_engine::IStorageBackend> kv_backend,
    const config::DatabaseConfig& db_config)
    : AccountStorageBackend(
          std::move(kv_backend), db_config, std::make_shared<PgConnectionPool>()) {}

AccountStorageBackend::AccountStorageBackend(
    std::unique_ptr<mir2::storage_engine::IStorageBackend> kv_backend,
    const config::DatabaseConfig& db_config,
    std::shared_ptr<PgConnectionPool> pool)
    : kv_backend_(std::move(kv_backend)),
      db_config_(db_config),
      pool_(std::move(pool)) {}

bool AccountStorageBackend::Initialize() {
  if (initialized_) {
    return true;
  }
  if (!kv_backend_) {
    return false;
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

mir2::storage_engine::IStorageBackend::StorageResult AccountStorageBackend::Save(
    const std::string& key,
    uint64_t version,
    const std::vector<uint8_t>& data) {
  if (!kv_backend_) {
    return StorageResult{false, "kv backend unavailable", 0};
  }
  return kv_backend_->Save(key, version, data);
}

mir2::storage_engine::IStorageBackend::StorageResult AccountStorageBackend::SaveBatch(
    const std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>>& items) {
  if (!kv_backend_) {
    return StorageResult{false, "kv backend unavailable", 0};
  }
  return kv_backend_->SaveBatch(items);
}

std::optional<std::pair<uint64_t, std::vector<uint8_t>>> AccountStorageBackend::Load(
    const std::string& key) {
  if (!kv_backend_) {
    return std::nullopt;
  }

  auto username = ParseAccountStorageKey(key);
  if (username) {
    return LoadAccountByUsername(*username);
  }
  return kv_backend_->Load(key);
}

std::optional<std::map<std::string, std::pair<uint64_t, std::vector<uint8_t>>>>
AccountStorageBackend::LoadAll() {
  if (!kv_backend_) {
    return std::nullopt;
  }
  return kv_backend_->LoadAll();
}

mir2::storage_engine::IStorageBackend::StorageResult AccountStorageBackend::Validate() {
  if (!initialized_) {
    return StorageResult{false, "AccountStorageBackend not initialized", 0};
  }
  if (!kv_backend_) {
    return StorageResult{false, "kv backend unavailable", 0};
  }
  auto kv_result = kv_backend_->Validate();
  if (!kv_result.success) {
    return kv_result;
  }
  if (!pool_ || !pool_->IsReady()) {
    return StorageResult{false, "account pool not ready", 0};
  }
  return StorageResult{true, "", 0};
}

bool AccountStorageBackend::IsHealthy() const {
  return initialized_ && kv_backend_ && kv_backend_->IsHealthy() &&
         pool_ && pool_->IsReady();
}

std::optional<std::pair<uint64_t, std::vector<uint8_t>>>
AccountStorageBackend::LoadAccountByUsername(const std::string& username) {
  auto logger = spdlog::get("mir2");

  if (username.empty() || !pool_ || !pool_->IsReady()) {
    return std::nullopt;
  }

  try {
    auto conn = pool_->Acquire();
    if (!conn) {
      if (logger) {
        logger->error("AccountStorageBackend::LoadAccountByUsername: failed to acquire connection for user '{}'",
                      username);
      }
      return std::nullopt;
    }

    PgConnectionGuard guard(*pool_, conn);
    pqxx::work txn(*conn);
    pqxx::result result = txn.exec(
        "SELECT id, username, password_hash, email, "
        "EXTRACT(EPOCH FROM created_at)::BIGINT * 1000, "
        "EXTRACT(EPOCH FROM last_login)::BIGINT * 1000, banned "
        "FROM accounts WHERE username = $1",
        pqxx::params{username});

    if (result.empty()) {
      return std::nullopt;
    }

    AccountData account;
    const auto& row = result[0];
    account.id = row[0].as<uint64_t>();
    account.username = row[1].as<std::string>();
    account.password_hash = row[2].as<std::string>();
    account.email = row[3].as<std::string>("");
    account.created_at = row[4].as<int64_t>(0);
    account.last_login = row[5].as<int64_t>(0);
    account.banned = row[6].as<bool>(false);

    uint64_t version = kDefaultVersion;
    if (account.last_login > 0) {
      version = static_cast<uint64_t>(account.last_login);
    } else if (account.created_at > 0) {
      version = static_cast<uint64_t>(account.created_at);
    } else if (account.id > 0) {
      version = account.id;
    }

    return std::make_pair(version, EncodeAccountData(account));
  } catch (const pqxx::broken_connection& ex) {
    if (logger) {
      logger->error("AccountStorageBackend::LoadAccountByUsername: connection lost for user '{}': {}",
                    username, ex.what());
    }
    return std::nullopt;
  } catch (const pqxx::sql_error& ex) {
    if (logger) {
      logger->error("AccountStorageBackend::LoadAccountByUsername: SQL error for user '{}': {}",
                    username, ex.what());
    }
    return std::nullopt;
  } catch (const std::exception& ex) {
    if (logger) {
      logger->error("AccountStorageBackend::LoadAccountByUsername: unexpected error for user '{}': {}",
                    username, ex.what());
    }
    return std::nullopt;
  }
}

}  // namespace mir2::db
