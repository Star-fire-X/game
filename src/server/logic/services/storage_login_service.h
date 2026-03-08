/**
 * @file storage_login_service.h
 * @brief Storage-backed login service with negative cache.
 */

#ifndef MIR2_LOGIC_SERVICES_STORAGE_LOGIN_SERVICE_H_
#define MIR2_LOGIC_SERVICES_STORAGE_LOGIN_SERVICE_H_

#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

#include "logic/handlers/login/login_handler.h"

namespace mir2::storage_engine {
class StorageEngine;
}

namespace mir2::logic {

class CoroutineExecutor;
template <typename T>
class Task;

class StorageLoginService final : public LoginService {
 public:
  StorageLoginService(CoroutineExecutor& executor,
                      mir2::storage_engine::StorageEngine* storage_engine);
  void SetStorageEngine(mir2::storage_engine::StorageEngine* storage_engine);

  void Login(const std::string& username,
             const std::string& password,
             LoginCallback callback) override;

 private:
  Task<void> LoginAsync(std::string username,
                        std::string password,
                        LoginCallback callback);

  struct NegativeCacheEntry {
    std::chrono::steady_clock::time_point expire_at;
  };

  bool IsMissingAccountCached(const std::string& username);
  void CacheMissingAccount(const std::string& username);
  void RemoveMissingAccount(const std::string& username);
  void MaybePruneExpiredLocked(std::chrono::steady_clock::time_point now);
  void PruneExpiredLocked(std::chrono::steady_clock::time_point now);

  static constexpr std::chrono::seconds kNegativeCacheTtl{30};
  static constexpr std::chrono::seconds kNegativeCachePruneInterval{1};
  static constexpr size_t kMaxNegativeCacheEntries = 50000;

  CoroutineExecutor& executor_;
  mir2::storage_engine::StorageEngine* storage_engine_ = nullptr;
  std::mutex negative_cache_mutex_;
  std::unordered_map<std::string, NegativeCacheEntry> negative_cache_;
  std::chrono::steady_clock::time_point next_prune_at_{};
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_SERVICES_STORAGE_LOGIN_SERVICE_H_
