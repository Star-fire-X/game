#include "logic/coroutine_executor.h"

#include "monitor/metrics.h"

namespace mir2::logic {
namespace {

constexpr const char* kMetricSuspendedCount = "logic.coroutine.suspended_count";
constexpr const char* kMetricRunningCount = "logic.coroutine.running_count";
constexpr const char* kMetricPendingResumeCallbacks =
    "logic.coroutine.pending_resume_callbacks";
constexpr const char* kMetricAccepting = "logic.coroutine.accepting";
constexpr const char* kMetricSpawnRejectedTotal =
    "logic.coroutine.spawn_rejected_total";
constexpr const char* kMetricDrainTimeoutTotal =
    "logic.coroutine.drain_timeout_total";

}  // namespace

CoroutineExecutor::CoroutineExecutor(asio::io_context& io_context, size_t blocking_threads)
    : io_context_(io_context),
      blocking_pool_(blocking_threads == 0 ? 1 : blocking_threads) {
  monitor::Metrics::Instance().SetGauge(kMetricSuspendedCount, 0);
  monitor::Metrics::Instance().SetGauge(kMetricRunningCount, 0);
  monitor::Metrics::Instance().SetGauge(kMetricPendingResumeCallbacks, 0);
  monitor::Metrics::Instance().SetGauge(kMetricAccepting, 1);
}

CoroutineExecutor::~CoroutineExecutor() {
  StopAccepting();
  DrainAndJoin(std::chrono::seconds(5));
}

void CoroutineExecutor::StopAccepting() noexcept {
  bool expected = true;
  if (accepting_tasks_.compare_exchange_strong(expected, false,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
    monitor::Metrics::Instance().SetGauge(kMetricAccepting, 0);
  }
}

bool CoroutineExecutor::DrainAndJoin(std::chrono::steady_clock::duration timeout) {
  StopAccepting();

  if (!blocking_pool_joined_.exchange(true, std::memory_order_acq_rel)) {
    blocking_pool_.join();
  }

  const bool drained = WaitForDrain(timeout);
  if (!drained) {
    monitor::Metrics::Instance().IncrementCounter(kMetricDrainTimeoutTotal);
  }
  return drained;
}

bool CoroutineExecutor::BeginSpawn() {
  if (!accepting_tasks_.load(std::memory_order_acquire)) {
    monitor::Metrics::Instance().IncrementCounter(kMetricSpawnRejectedTotal);
    return false;
  }
  IncrementRunning();
  return true;
}

bool CoroutineExecutor::IsDrained() const noexcept {
  return running_count_.load(std::memory_order_acquire) == 0 &&
         suspended_count_.load(std::memory_order_acquire) == 0 &&
         pending_resume_callbacks_.load(std::memory_order_acquire) == 0;
}

void CoroutineExecutor::NotifyDrainWaiters() noexcept {
  drain_cv_.notify_all();
}

bool CoroutineExecutor::WaitForDrain(std::chrono::steady_clock::duration timeout) {
  if (timeout <= std::chrono::steady_clock::duration::zero()) {
    return IsDrained();
  }
  std::unique_lock<std::mutex> lock(drain_mutex_);
  return drain_cv_.wait_for(lock, timeout, [this]() { return IsDrained(); });
}

void CoroutineExecutor::IncrementSuspended() {
  const auto value = suspended_count_.fetch_add(1) + 1;
  monitor::Metrics::Instance().SetGauge(kMetricSuspendedCount, value);
  NotifyDrainWaiters();
}

void CoroutineExecutor::DecrementSuspended() {
  auto value = suspended_count_.fetch_sub(1) - 1;
  if (value < 0) {
    value = 0;
    suspended_count_.store(0);
  }
  monitor::Metrics::Instance().SetGauge(kMetricSuspendedCount, value);
  NotifyDrainWaiters();
}

void CoroutineExecutor::IncrementRunning() {
  const auto value = running_count_.fetch_add(1) + 1;
  monitor::Metrics::Instance().SetGauge(kMetricRunningCount, value);
  NotifyDrainWaiters();
}

void CoroutineExecutor::DecrementRunning() {
  auto value = running_count_.fetch_sub(1) - 1;
  if (value < 0) {
    value = 0;
    running_count_.store(0);
  }
  monitor::Metrics::Instance().SetGauge(kMetricRunningCount, value);
  NotifyDrainWaiters();
}

void CoroutineExecutor::IncrementPendingResumeCallbacks() {
  const auto value = pending_resume_callbacks_.fetch_add(1) + 1;
  monitor::Metrics::Instance().SetGauge(kMetricPendingResumeCallbacks, value);
  NotifyDrainWaiters();
}

void CoroutineExecutor::DecrementPendingResumeCallbacks() {
  auto value = pending_resume_callbacks_.fetch_sub(1) - 1;
  if (value < 0) {
    value = 0;
    pending_resume_callbacks_.store(0);
  }
  monitor::Metrics::Instance().SetGauge(kMetricPendingResumeCallbacks, value);
  NotifyDrainWaiters();
}

}  // namespace mir2::logic
