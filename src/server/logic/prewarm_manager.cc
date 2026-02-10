/**
 * @file prewarm_manager.cc
 * @brief Pre-warm manager implementation.
 */

#include "logic/prewarm_manager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <latch>
#include <utility>

#include "log/logger.h"

namespace mir2::logic {
namespace {

Task<void> RunPrewarmEntryTask(CoroutineExecutor* executor,
                               const PrewarmManager::Loader* loader,
                               PrewarmEntry entry,
                               std::latch* latch,
                               std::atomic<uint32_t>* success,
                               std::atomic<uint32_t>* failed) {
  bool ok = true;
  try {
    if (loader != nullptr && *loader) {
      ok = co_await executor->Async([loader, entry]() { return (*loader)(entry); });
    } else {
      ok = true;
    }
  } catch (const std::exception& ex) {
    ok = false;
    SYSLOG_WARN("Prewarm failed (client_id={}, player_id={}, error={})",
                entry.client_id, entry.player_id, ex.what());
  } catch (...) {
    ok = false;
    SYSLOG_WARN("Prewarm failed (client_id={}, player_id={}, error=unknown)",
                entry.client_id, entry.player_id);
  }

  if (ok) {
    success->fetch_add(1, std::memory_order_relaxed);
  } else {
    failed->fetch_add(1, std::memory_order_relaxed);
  }
  latch->count_down();
  co_return;
}

}  // namespace

PrewarmManager::PrewarmManager(CoroutineExecutor& executor, size_t batch_size)
    : executor_(executor),
      batch_size_(batch_size == 0 ? kDefaultBatchSize : batch_size) {}

void PrewarmManager::SetLoader(Loader loader) {
  loader_ = std::move(loader);
}

Task<PrewarmResult> PrewarmManager::Prewarm(const std::vector<PrewarmEntry>& entries) {
  PrewarmResult result;
  result.total = static_cast<uint32_t>(entries.size());
  if (entries.empty()) {
    co_return result;
  }

  const auto started_at = std::chrono::steady_clock::now();
  const size_t batch_size = batch_size_ == 0 ? kDefaultBatchSize : batch_size_;

  size_t index = 0;
  while (index < entries.size()) {
    const size_t remaining = entries.size() - index;
    const size_t count = std::min(batch_size, remaining);
    std::latch latch(static_cast<std::ptrdiff_t>(count));
    std::atomic<uint32_t> success{0};
    std::atomic<uint32_t> failed{0};

    for (size_t i = 0; i < count; ++i) {
      PrewarmEntry entry = entries[index + i];
      if (!executor_.Spawn(RunPrewarmEntryTask(
              &executor_, &loader_, std::move(entry), &latch, &success, &failed))) {
        failed.fetch_add(1, std::memory_order_relaxed);
        latch.count_down();
      }
    }

    co_await executor_.Async([&latch]() { latch.wait(); });
    result.success += success.load(std::memory_order_relaxed);
    result.failed += failed.load(std::memory_order_relaxed);
    index += count;
  }

  const auto finished_at = std::chrono::steady_clock::now();
  const auto duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(finished_at - started_at).count();
  result.duration_ms = duration_ms < 0 ? 0 : static_cast<uint64_t>(duration_ms);
  co_return result;
}

}  // namespace mir2::logic
