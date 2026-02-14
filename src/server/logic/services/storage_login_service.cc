#include "logic/services/storage_login_service.h"

#include <utility>

#include "common/crypto_utils.h"
#include "storage_engine/backends/common/account_storage_codec.h"
#include "log/logger.h"
#include "logic/coroutine_executor.h"
#include "monitor/metrics.h"
#include "storage_engine/storage_engine.h"

namespace mir2::logic {
namespace {

constexpr const char* kMetricLoginSpawnRejectedNotAcceptingTotal =
    "logic.login.spawn_rejected_not_accepting_total";
constexpr const char* kMetricLoginSpawnRejectedOverLimitTotal =
    "logic.login.spawn_rejected_over_limit_total";
constexpr const char* kMetricLoginSpawnRejectedInvalidTaskTotal =
    "logic.login.spawn_rejected_invalid_task_total";

}  // namespace

StorageLoginService::StorageLoginService(
    CoroutineExecutor& executor,
    mir2::storage_engine::StorageEngine* storage_engine)
    : executor_(executor),
      storage_engine_(storage_engine) {}

void StorageLoginService::SetStorageEngine(
    mir2::storage_engine::StorageEngine* storage_engine) {
  storage_engine_ = storage_engine;
}

void StorageLoginService::Login(const std::string& username,
                                const std::string& password,
                                LoginCallback callback) {
  if (!callback) {
    return;
  }

  if (username.empty()) {
    LoginResult result;
    result.code = mir2::common::ErrorCode::kAccountNotFound;
    callback(result);
    return;
  }

  if (IsMissingAccountCached(username)) {
    LoginResult result;
    result.code = mir2::common::ErrorCode::kAccountNotFound;
    callback(result);
    return;
  }

  LoginCallback callback_for_task = callback;
  executor_.SpawnOrDrop(
      LoginAsync(username, password, std::move(callback_for_task)),
      [callback = std::move(callback)](SpawnResult reason) mutable {
        if (!callback) {
          return;
        }

        monitor::Metrics::Instance().IncrementCounter(
            monitor::Metrics::kLoginSpawnRejectedTotal);
        LoginResult result;
        switch (reason) {
          case SpawnResult::kOverLimit:
            monitor::Metrics::Instance().IncrementCounter(
                kMetricLoginSpawnRejectedOverLimitTotal);
            result.code = mir2::common::ErrorCode::SERVER_OVERLOADED;
            break;
          case SpawnResult::kNotAccepting:
            monitor::Metrics::Instance().IncrementCounter(
                kMetricLoginSpawnRejectedNotAcceptingTotal);
            result.code = mir2::common::ErrorCode::SERVER_MAINTENANCE;
            break;
          case SpawnResult::kInvalidTask:
            monitor::Metrics::Instance().IncrementCounter(
                kMetricLoginSpawnRejectedInvalidTaskTotal);
            result.code = mir2::common::ErrorCode::kUnknown;
            break;
          case SpawnResult::kSpawned:
            result.code = mir2::common::ErrorCode::kUnknown;
            break;
        }

        SYSLOG_WARN("StorageLoginService login dropped before spawn reason={}",
                    ToString(reason));
        callback(result);
      });
}

Task<void> StorageLoginService::LoginAsync(std::string username,
                                           std::string password,
                                           LoginCallback callback) {
  if (!callback) {
    co_return;
  }

  LoginResult result;
  try {
    mir2::storage_engine::StorageEngine* engine = storage_engine_;
    if (!engine) {
      if (!mir2::storage_engine::StorageEngine::IsInitialized()) {
        result.code = mir2::common::ErrorCode::kUnknown;
        callback(result);
        co_return;
      }
      engine = &mir2::storage_engine::StorageEngine::Instance();
    }

    const std::string key = mir2::db::BuildAccountStorageKey(username);
    auto stored = co_await engine->GetAsync(key, executor_);
    if (!stored) {
      stored = co_await engine->LoadFromDBAsync(key, executor_);
    }

    if (!stored) {
      CacheMissingAccount(username);
      result.code = mir2::common::ErrorCode::kAccountNotFound;
      callback(result);
      co_return;
    }

    auto account = mir2::db::DecodeAccountData(stored->data);
    if (!account) {
      result.code = mir2::common::ErrorCode::kUnknown;
      callback(result);
      co_return;
    }

    if (account->banned) {
      result.code = mir2::common::ErrorCode::ACCOUNT_BANNED;
      callback(result);
      co_return;
    }

    if (!mir2::common::VerifyPassword(password, account->password_hash)) {
      result.code = mir2::common::ErrorCode::kPasswordWrong;
      callback(result);
      co_return;
    }

    RemoveMissingAccount(username);
    result.code = mir2::common::ErrorCode::kOk;
    result.account_id = account->id;
    result.token = mir2::common::GenerateSecureToken();
    callback(result);
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("StorageLoginService login failed: {}", ex.what());
    result.code = mir2::common::ErrorCode::kUnknown;
    callback(result);
  } catch (...) {
    SYSLOG_ERROR("StorageLoginService login failed: unknown error");
    result.code = mir2::common::ErrorCode::kUnknown;
    callback(result);
  }
  co_return;
}

bool StorageLoginService::IsMissingAccountCached(const std::string& username) {
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(negative_cache_mutex_);
  MaybePruneExpiredLocked(now);
  auto it = negative_cache_.find(username);
  if (it == negative_cache_.end()) {
    return false;
  }
  if (it->second.expire_at <= now) {
    negative_cache_.erase(it);
    return false;
  }
  return true;
}

void StorageLoginService::CacheMissingAccount(const std::string& username) {
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(negative_cache_mutex_);
  MaybePruneExpiredLocked(now);
  if (negative_cache_.size() >= kMaxNegativeCacheEntries) {
    negative_cache_.erase(negative_cache_.begin());
  }
  negative_cache_[username] = NegativeCacheEntry{now + kNegativeCacheTtl};
}

void StorageLoginService::RemoveMissingAccount(const std::string& username) {
  std::lock_guard<std::mutex> lock(negative_cache_mutex_);
  negative_cache_.erase(username);
}

void StorageLoginService::MaybePruneExpiredLocked(
    std::chrono::steady_clock::time_point now) {
  if (now < next_prune_at_) {
    return;
  }
  PruneExpiredLocked(now);
  next_prune_at_ = now + kNegativeCachePruneInterval;
}

void StorageLoginService::PruneExpiredLocked(
    std::chrono::steady_clock::time_point now) {
  for (auto it = negative_cache_.begin(); it != negative_cache_.end();) {
    if (it->second.expire_at <= now) {
      it = negative_cache_.erase(it);
      continue;
    }
    ++it;
  }
}

}  // namespace mir2::logic
