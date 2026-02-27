#ifndef MIR2_STORAGE_ENGINE_BACKENDS_ACCOUNT_STORAGE_BACKEND_H_
#define MIR2_STORAGE_ENGINE_BACKENDS_ACCOUNT_STORAGE_BACKEND_H_

#include <chrono>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "common/types/database_types.h"
#include "config/config_manager.h"
#include "storage_engine/backends/postgres/pg_connection_pool.h"
#include "storage_engine/interfaces/storage_backend.h"

namespace mir2::db {

class AccountStorageBackend final : public mir2::storage_engine::IStorageBackend,
                                    public mir2::storage_engine::IAtomicBatchStorageBackend {
 public:
  struct AccountCacheOptions {
    uint32_t ttl_seconds = 300;
    size_t max_entries = 10000;
  };

  AccountStorageBackend(
      std::unique_ptr<mir2::storage_engine::IStorageBackend> kv_backend,
      const config::DatabaseConfig& db_config);
  AccountStorageBackend(
      std::unique_ptr<mir2::storage_engine::IStorageBackend> kv_backend,
      const config::DatabaseConfig& db_config,
      const AccountCacheOptions& cache_options);
  AccountStorageBackend(
      std::unique_ptr<mir2::storage_engine::IStorageBackend> kv_backend,
      const config::DatabaseConfig& db_config,
      std::shared_ptr<PgConnectionPool> pool);
  AccountStorageBackend(
      std::unique_ptr<mir2::storage_engine::IStorageBackend> kv_backend,
      const config::DatabaseConfig& db_config,
      std::shared_ptr<PgConnectionPool> pool,
      const AccountCacheOptions& cache_options);

  bool Initialize();

  StorageResult Save(const std::string& key,
                     uint64_t version,
                     const std::vector<uint8_t>& data) override;

  StorageResult SaveBatch(const BatchItems& items) override;

  // Atomicity is only guaranteed for batches fully routed to one store.
  // Mixed account key + non-account key batches are rejected as unsupported.
  StorageResult SaveBatchAtomic(const BatchItems& items) override;

  std::optional<std::pair<uint64_t, std::vector<uint8_t>>> Load(
      const std::string& key) override;

  std::optional<std::map<std::string, std::pair<uint64_t, std::vector<uint8_t>>>> LoadAll()
      override;

  StorageResult Validate() override;

  bool IsHealthy() const override;

 private:
  struct AccountWriteItem {
    std::string username;
    AccountData account;
    uint64_t fallback_version = 0;
  };

  struct AccountCacheEntry {
    std::pair<uint64_t, std::vector<uint8_t>> payload;
    std::chrono::steady_clock::time_point expires_at;
    std::list<std::string>::iterator lru_it;
  };

  StorageResult BuildWriteBatches(const BatchItems& items,
                                  std::vector<AccountWriteItem>* account_items,
                                  BatchItems* non_account_items) const;
  StorageResult UpsertAccounts(const std::vector<AccountWriteItem>& items);
  uint64_t ResolveAccountVersion(const AccountData& account,
                                 uint64_t fallback_version) const;
  std::optional<std::pair<uint64_t, std::vector<uint8_t>>> GetCachedAccount(
      const std::string& username);
  void PutCachedAccount(const std::string& username,
                        uint64_t version,
                        const std::vector<uint8_t>& payload);
  void InvalidateCachedAccount(const std::string& username);
  void ReportAccountCacheSize(size_t size) const;

  std::optional<std::pair<uint64_t, std::vector<uint8_t>>> LoadAccountByUsername(
      const std::string& username);

  std::unique_ptr<mir2::storage_engine::IStorageBackend> kv_backend_;
  config::DatabaseConfig db_config_;
  std::shared_ptr<PgConnectionPool> pool_;
  AccountCacheOptions cache_options_;
  mutable std::mutex account_cache_mutex_;
  std::list<std::string> account_cache_lru_;
  std::unordered_map<std::string, AccountCacheEntry> account_cache_;
  bool initialized_ = false;
};

}  // namespace mir2::db

#endif  // MIR2_STORAGE_ENGINE_BACKENDS_ACCOUNT_STORAGE_BACKEND_H_
