/**
 * @file prewarm_manager.cc
 * @brief Pre-warm manager implementation.
 */

#include "logic/prewarm_manager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <utility>
#include <vector>

#include "log/logger.h"
#include "monitor/metrics.h"

namespace mir2::logic {
namespace {

constexpr const char* kMetricPrewarmPendingEntries = "logic.prewarm.pending_entries";
constexpr const char* kMetricPrewarmSpawnRejectedNotAcceptingTotal =
    "logic.prewarm.spawn_rejected_not_accepting_total";
constexpr const char* kMetricPrewarmSpawnRejectedOverLimitTotal =
    "logic.prewarm.spawn_rejected_over_limit_total";
constexpr const char* kMetricPrewarmSpawnRejectedInvalidTaskTotal =
    "logic.prewarm.spawn_rejected_invalid_task_total";

Task<void> RunPrewarmEntryTask(CoroutineExecutor* executor,
                               const PrewarmManager::Loader* loader,
                               PrewarmEntry entry,
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
    monitor::Metrics::Instance().SetGauge(kMetricPrewarmPendingEntries, 0);
    co_return result;
  }

  const auto started_at = std::chrono::steady_clock::now();
  const size_t batch_size = batch_size_ == 0 ? kDefaultBatchSize : batch_size_;
  monitor::Metrics::Instance().SetGauge(
      kMetricPrewarmPendingEntries, static_cast<double>(entries.size()));

  size_t index = 0;
  while (index < entries.size()) {
    const size_t remaining = entries.size() - index;
    const size_t count = std::min(batch_size, remaining);
    std::atomic<uint32_t> success{0};
    std::atomic<uint32_t> failed{0};
    std::vector<Task<void>> batch_tasks;
    batch_tasks.reserve(count);
    std::vector<std::pair<uint64_t, uint32_t>> batch_entries;
    batch_entries.reserve(count);

    for (size_t i = 0; i < count; ++i) {
      PrewarmEntry entry = entries[index + i];
      batch_entries.emplace_back(entry.client_id, entry.player_id);
      batch_tasks.push_back(
          RunPrewarmEntryTask(
              &executor_, &loader_, std::move(entry), &success, &failed));
    }

    co_await executor_.WhenAll(
        std::move(batch_tasks),
        [&failed, batch_entries = std::move(batch_entries)](size_t task_index,
                                                            SpawnResult reason) {
          failed.fetch_add(1, std::memory_order_relaxed);
          monitor::Metrics::Instance().IncrementCounter(
              monitor::Metrics::kPrewarmSpawnRejectedTotal);
          switch (reason) {
            case SpawnResult::kNotAccepting:
              monitor::Metrics::Instance().IncrementCounter(
                  kMetricPrewarmSpawnRejectedNotAcceptingTotal);
              break;
            case SpawnResult::kOverLimit:
              monitor::Metrics::Instance().IncrementCounter(
                  kMetricPrewarmSpawnRejectedOverLimitTotal);
              break;
            case SpawnResult::kInvalidTask:
              monitor::Metrics::Instance().IncrementCounter(
                  kMetricPrewarmSpawnRejectedInvalidTaskTotal);
              break;
            case SpawnResult::kSpawned:
              break;
          }

          uint64_t client_id = 0;
          uint32_t player_id = 0;
          if (task_index < batch_entries.size()) {
            client_id = batch_entries[task_index].first;
            player_id = batch_entries[task_index].second;
          }
          SYSLOG_WARN("Prewarm task dropped (client_id={}, player_id={}, reason={})",
                      client_id,
                      player_id,
                      ToString(reason));
        });

    result.success += success.load(std::memory_order_relaxed);
    result.failed += failed.load(std::memory_order_relaxed);
    index += count;
    monitor::Metrics::Instance().SetGauge(
        kMetricPrewarmPendingEntries,
        static_cast<double>(entries.size() - index));
  }

  const auto finished_at = std::chrono::steady_clock::now();
  const auto duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(finished_at - started_at).count();
  result.duration_ms = duration_ms < 0 ? 0 : static_cast<uint64_t>(duration_ms);
  monitor::Metrics::Instance().SetGauge(kMetricPrewarmPendingEntries, 0);
  co_return result;
}

}  // namespace mir2::logic
