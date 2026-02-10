#ifndef MIR2_STORAGE_ENGINE_BACKENDS_ACCOUNT_STORAGE_BACKEND_H_
#define MIR2_STORAGE_ENGINE_BACKENDS_ACCOUNT_STORAGE_BACKEND_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "config/config_manager.h"
#include "storage_engine/backends/postgres/pg_connection_pool.h"
#include "storage_engine/interfaces/storage_backend.h"

namespace mir2::db {

class AccountStorageBackend final : public mir2::storage_engine::IStorageBackend {
 public:
  AccountStorageBackend(
      std::unique_ptr<mir2::storage_engine::IStorageBackend> kv_backend,
      const config::DatabaseConfig& db_config);
  AccountStorageBackend(
      std::unique_ptr<mir2::storage_engine::IStorageBackend> kv_backend,
      const config::DatabaseConfig& db_config,
      std::shared_ptr<PgConnectionPool> pool);

  bool Initialize();

  StorageResult Save(const std::string& key,
                     uint64_t version,
                     const std::vector<uint8_t>& data) override;

  StorageResult SaveBatch(
      const std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>>& items) override;

  std::optional<std::pair<uint64_t, std::vector<uint8_t>>> Load(
      const std::string& key) override;

  std::optional<std::map<std::string, std::pair<uint64_t, std::vector<uint8_t>>>> LoadAll()
      override;

  StorageResult Validate() override;

  bool IsHealthy() const override;

 private:
  std::optional<std::pair<uint64_t, std::vector<uint8_t>>> LoadAccountByUsername(
      const std::string& username);

  std::unique_ptr<mir2::storage_engine::IStorageBackend> kv_backend_;
  config::DatabaseConfig db_config_;
  std::shared_ptr<PgConnectionPool> pool_;
  bool initialized_ = false;
};

}  // namespace mir2::db

#endif  // MIR2_STORAGE_ENGINE_BACKENDS_ACCOUNT_STORAGE_BACKEND_H_
