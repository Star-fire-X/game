/**
 * @file prewarm_manager.h
 * @brief Pre-warm manager for restoring player contexts.
 */

#ifndef MIR2_LOGIC_PREWARM_MANAGER_H_
#define MIR2_LOGIC_PREWARM_MANAGER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "logic/coroutine_executor.h"
#include "logic/task.h"

namespace mir2::logic {

struct PrewarmEntry {
  uint64_t client_id = 0;
  uint32_t player_id = 0;
  uint32_t account_id = 0;
  std::string ip_address;
  uint64_t connected_at_ms = 0;
  uint64_t last_active_ms = 0;
};

struct PrewarmResult {
  uint32_t total = 0;
  uint32_t success = 0;
  uint32_t failed = 0;
  uint64_t duration_ms = 0;
};

class PrewarmManager {
 public:
  using Loader = std::function<bool(const PrewarmEntry&)>;

  static constexpr size_t kDefaultBatchSize = 50;

  explicit PrewarmManager(CoroutineExecutor& executor,
                          size_t batch_size = kDefaultBatchSize);

  void SetLoader(Loader loader);

  Task<PrewarmResult> Prewarm(const std::vector<PrewarmEntry>& entries);

  size_t BatchSize() const { return batch_size_; }

 private:
  CoroutineExecutor& executor_;
  Loader loader_;
  size_t batch_size_ = kDefaultBatchSize;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_PREWARM_MANAGER_H_
