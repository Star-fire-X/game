#include "storage_engine/backends/account_storage_backend.h"

#include <chrono>
#include <utility>

#include <pqxx/pqxx>
#include <spdlog/spdlog.h>

#include "common/types/database_types.h"
#include "monitor/metrics.h"
#include "storage_engine/backends/common/account_storage_codec.h"

namespace mir2::db {

namespace {
constexpr uint64_t kDefaultVersion = 1;
constexpr const char* kAccountUpsertSql =
    "INSERT INTO accounts (username, password_hash, email, created_at, last_login, banned) "
    "VALUES ($1, $2, $3, "
    "CASE WHEN $4::BIGINT > 0 THEN TO_TIMESTAMP($4::DOUBLE PRECISION / 1000.0) ELSE NOW() END, "
    "CASE WHEN $5::BIGINT > 0 THEN TO_TIMESTAMP($5::DOUBLE PRECISION / 1000.0) ELSE NULL END, "
    "$6) "
    "ON CONFLICT (username) DO UPDATE "
    "SET password_hash = EXCLUDED.password_hash, "
    "email = EXCLUDED.email, "
    "last_login = EXCLUDED.last_login, "
    "banned = EXCLUDED.banned "
    "RETURNING id, username, password_hash, email, "
    "EXTRACT(EPOCH FROM created_at)::BIGINT * 1000, "
    "EXTRACT(EPOCH FROM last_login)::BIGINT * 1000, banned";
constexpr const char* kAccountCacheHitMetric = "storage.account_cache.hit_total";
constexpr const char* kAccountCacheMissMetric = "storage.account_cache.miss_total";
constexpr const char* kAccountCacheSizeMetric = "storage.account_cache.size";

int64_t ElapsedMs(std::chrono::steady_clock::time_point start) {
  const auto elapsed = std::chrono::steady_clock::now() - start;
  return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}
}

AccountStorageBackend::AccountStorageBackend(
    std::unique_ptr<mir2::storage_engine::IStorageBackend> kv_backend,
    const config::DatabaseConfig& db_config)
    : AccountStorageBackend(
          std::move(kv_backend),
          db_config,
          std::make_shared<PgConnectionPool>(),
          AccountCacheOptions{}) {}

AccountStorageBackend::AccountStorageBackend(
    std::unique_ptr<mir2::storage_engine::IStorageBackend> kv_backend,
    const config::DatabaseConfig& db_config,
    const AccountCacheOptions& cache_options)
    : AccountStorageBackend(
          std::move(kv_backend),
          db_config,
          std::make_shared<PgConnectionPool>(),
          cache_options) {}

AccountStorageBackend::AccountStorageBackend(
    std::unique_ptr<mir2::storage_engine::IStorageBackend> kv_backend,
    const config::DatabaseConfig& db_config,
    std::shared_ptr<PgConnectionPool> pool)
    : AccountStorageBackend(
          std::move(kv_backend),
          db_config,
          std::move(pool),
          AccountCacheOptions{}) {}

AccountStorageBackend::AccountStorageBackend(
    std::unique_ptr<mir2::storage_engine::IStorageBackend> kv_backend,
    const config::DatabaseConfig& db_config,
    std::shared_ptr<PgConnectionPool> pool,
    const AccountCacheOptions& cache_options)
    : kv_backend_(std::move(kv_backend)),
      db_config_(db_config),
      pool_(std::move(pool)),
      cache_options_(cache_options) {}

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
  ReportAccountCacheSize(0);
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

  const auto username = ParseAccountStorageKey(key);
  if (!username) {
    return kv_backend_->Save(key, version, data);
  }

  std::vector<AccountWriteItem> account_items;
  BatchItems non_account_items;
  const BatchItems single_item = {{key, version, data}};
  auto split_result = BuildWriteBatches(single_item, &account_items, &non_account_items);
  if (!split_result.success) {
    return split_result;
  }

  return UpsertAccounts(account_items);
}

mir2::storage_engine::IStorageBackend::StorageResult AccountStorageBackend::SaveBatch(
    const BatchItems& items) {
  if (!kv_backend_) {
    return StorageResult{false, "kv backend unavailable", 0};
  }

  if (items.empty()) {
    return StorageResult{true, "", 0};
  }

  std::vector<AccountWriteItem> account_items;
  BatchItems non_account_items;
  auto split_result = BuildWriteBatches(items, &account_items, &non_account_items);
  if (!split_result.success) {
    return split_result;
  }

  int64_t op_time_ms = 0;
  if (!account_items.empty()) {
    auto account_result = UpsertAccounts(account_items);
    op_time_ms += account_result.operation_time_ms;
    if (!account_result.success) {
      account_result.operation_time_ms = op_time_ms;
      return account_result;
    }
  }

  if (!non_account_items.empty()) {
    auto kv_result = kv_backend_->SaveBatch(non_account_items);
    kv_result.operation_time_ms += op_time_ms;
    return kv_result;
  }

  return StorageResult{true, "", op_time_ms};
}

mir2::storage_engine::IStorageBackend::StorageResult AccountStorageBackend::SaveBatchAtomic(
    const BatchItems& items) {
  if (!kv_backend_) {
    return StorageResult{false, "kv backend unavailable", 0};
  }

  if (items.empty()) {
    return StorageResult{true, "", 0};
  }

  std::vector<AccountWriteItem> account_items;
  BatchItems non_account_items;
  auto split_result = BuildWriteBatches(items, &account_items, &non_account_items);
  if (!split_result.success) {
    return split_result;
  }

  if (!account_items.empty() && !non_account_items.empty()) {
    return StorageResult{false, "cross-store atomic batch unsupported", 0};
  }

  if (!account_items.empty()) {
    return UpsertAccounts(account_items);
  }

  auto* atomic_kv_backend =
      dynamic_cast<mir2::storage_engine::IAtomicBatchStorageBackend*>(kv_backend_.get());
  if (atomic_kv_backend != nullptr) {
    return atomic_kv_backend->SaveBatchAtomic(non_account_items);
  }
  return kv_backend_->SaveBatch(non_account_items);
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

mir2::storage_engine::IStorageBackend::StorageResult
AccountStorageBackend::BuildWriteBatches(const BatchItems& items,
                                         std::vector<AccountWriteItem>* account_items,
                                         BatchItems* non_account_items) const {
  if (account_items == nullptr || non_account_items == nullptr) {
    return StorageResult{false, "invalid output pointer", 0};
  }

  account_items->clear();
  non_account_items->clear();
  account_items->reserve(items.size());
  non_account_items->reserve(items.size());

  for (const auto& [key, version, data] : items) {
    const auto username = ParseAccountStorageKey(key);
    if (!username) {
      non_account_items->push_back({key, version, data});
      continue;
    }

    auto account_opt = DecodeAccountData(data);
    if (!account_opt) {
      return StorageResult{
          false, "failed to decode account payload for key '" + key + "'", 0};
    }

    AccountData account = std::move(*account_opt);
    if (account.username.empty()) {
      account.username = *username;
    } else if (account.username != *username) {
      return StorageResult{
          false,
          "account payload username mismatch for key '" + key + "'",
          0};
    }

    account_items->push_back(AccountWriteItem{
        .username = *username,
        .account = std::move(account),
        .fallback_version = version,
    });
  }

  return StorageResult{true, "", 0};
}

mir2::storage_engine::IStorageBackend::StorageResult
AccountStorageBackend::UpsertAccounts(const std::vector<AccountWriteItem>& items) {
  if (items.empty()) {
    return StorageResult{true, "", 0};
  }
  if (!pool_ || !pool_->IsReady()) {
    return StorageResult{false, "account pool not ready", 0};
  }

  const auto start = std::chrono::steady_clock::now();
  std::vector<AccountWriteItem> canonical_items;
  canonical_items.reserve(items.size());
  try {
    auto conn = pool_->Acquire();
    if (!conn) {
      return StorageResult{false, "failed to acquire connection", ElapsedMs(start)};
    }

    PgConnectionGuard guard(*pool_, conn);
    pqxx::work txn(*conn);
    for (const auto& item : items) {
      const pqxx::result result = txn.exec(
          kAccountUpsertSql,
          pqxx::params{
              item.username,
              item.account.password_hash,
              item.account.email,
              item.account.created_at,
              item.account.last_login,
              item.account.banned});

      if (result.empty()) {
        return StorageResult{
            false,
            "account upsert returned no rows for username '" + item.username + "'",
            ElapsedMs(start)};
      }

      const auto& row = result[0];
      AccountData canonical_account;
      canonical_account.id = row[0].as<uint64_t>();
      canonical_account.username = row[1].as<std::string>();
      canonical_account.password_hash = row[2].as<std::string>();
      canonical_account.email = row[3].as<std::string>("");
      canonical_account.created_at = row[4].as<int64_t>(0);
      canonical_account.last_login = row[5].as<int64_t>(0);
      canonical_account.banned = row[6].as<bool>(false);

      canonical_items.push_back(AccountWriteItem{
          .username = canonical_account.username,
          .account = std::move(canonical_account),
          .fallback_version = item.fallback_version,
      });
    }
    txn.commit();
  } catch (const std::exception& ex) {
    return StorageResult{false, ex.what(), ElapsedMs(start)};
  }

  for (const auto& item : canonical_items) {
    const auto payload = EncodeAccountData(item.account);
    PutCachedAccount(
        item.username,
        ResolveAccountVersion(item.account, item.fallback_version),
        payload);
  }
  return StorageResult{true, "", ElapsedMs(start)};
}

uint64_t AccountStorageBackend::ResolveAccountVersion(const AccountData& account,
                                                      uint64_t fallback_version) const {
  if (fallback_version > 0) {
    return fallback_version;
  }
  if (account.last_login > 0) {
    return static_cast<uint64_t>(account.last_login);
  }
  if (account.created_at > 0) {
    return static_cast<uint64_t>(account.created_at);
  }
  if (account.id > 0) {
    return account.id;
  }
  return kDefaultVersion;
}

std::optional<std::pair<uint64_t, std::vector<uint8_t>>>
AccountStorageBackend::GetCachedAccount(const std::string& username) {
  if (cache_options_.max_entries == 0 || cache_options_.ttl_seconds == 0) {
    monitor::Metrics::Instance().IncrementCounter(kAccountCacheMissMetric);
    return std::nullopt;
  }

  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(account_cache_mutex_);
  auto it = account_cache_.find(username);
  if (it == account_cache_.end()) {
    monitor::Metrics::Instance().IncrementCounter(kAccountCacheMissMetric);
    return std::nullopt;
  }
  if (it->second.expires_at <= now) {
    account_cache_lru_.erase(it->second.lru_it);
    account_cache_.erase(it);
    ReportAccountCacheSize(account_cache_.size());
    monitor::Metrics::Instance().IncrementCounter(kAccountCacheMissMetric);
    return std::nullopt;
  }

  account_cache_lru_.splice(account_cache_lru_.begin(),
                            account_cache_lru_,
                            it->second.lru_it);
  it->second.lru_it = account_cache_lru_.begin();
  monitor::Metrics::Instance().IncrementCounter(kAccountCacheHitMetric);
  return it->second.payload;
}

void AccountStorageBackend::PutCachedAccount(const std::string& username,
                                             uint64_t version,
                                             const std::vector<uint8_t>& payload) {
  if (cache_options_.max_entries == 0 || cache_options_.ttl_seconds == 0) {
    return;
  }

  const auto expire_at = std::chrono::steady_clock::now() +
                         std::chrono::seconds(cache_options_.ttl_seconds);
  std::lock_guard<std::mutex> lock(account_cache_mutex_);
  auto it = account_cache_.find(username);
  if (it != account_cache_.end()) {
    it->second.payload = std::make_pair(version, payload);
    it->second.expires_at = expire_at;
    account_cache_lru_.splice(account_cache_lru_.begin(),
                              account_cache_lru_,
                              it->second.lru_it);
    it->second.lru_it = account_cache_lru_.begin();
    ReportAccountCacheSize(account_cache_.size());
    return;
  }

  if (account_cache_.size() >= cache_options_.max_entries &&
      !account_cache_lru_.empty()) {
    const std::string& to_evict = account_cache_lru_.back();
    account_cache_.erase(to_evict);
    account_cache_lru_.pop_back();
  }

  account_cache_lru_.push_front(username);
  account_cache_[username] = AccountCacheEntry{
      .payload = std::make_pair(version, payload),
      .expires_at = expire_at,
      .lru_it = account_cache_lru_.begin(),
  };
  ReportAccountCacheSize(account_cache_.size());
}

void AccountStorageBackend::InvalidateCachedAccount(const std::string& username) {
  if (cache_options_.max_entries == 0) {
    return;
  }
  std::lock_guard<std::mutex> lock(account_cache_mutex_);
  auto it = account_cache_.find(username);
  if (it == account_cache_.end()) {
    return;
  }
  account_cache_lru_.erase(it->second.lru_it);
  account_cache_.erase(it);
  ReportAccountCacheSize(account_cache_.size());
}

void AccountStorageBackend::ReportAccountCacheSize(size_t size) const {
  monitor::Metrics::Instance().SetGauge(
      kAccountCacheSizeMetric, static_cast<double>(size));
}

std::optional<std::pair<uint64_t, std::vector<uint8_t>>>
AccountStorageBackend::LoadAccountByUsername(const std::string& username) {
  auto logger = spdlog::get("mir2");

  if (username.empty()) {
    return std::nullopt;
  }

  if (auto cached = GetCachedAccount(username); cached) {
    return cached;
  }

  if (!pool_ || !pool_->IsReady()) {
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
      InvalidateCachedAccount(username);
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

    const uint64_t version = ResolveAccountVersion(account, 0);
    const auto payload = EncodeAccountData(account);
    PutCachedAccount(username, version, payload);
    return std::make_pair(version, payload);
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
