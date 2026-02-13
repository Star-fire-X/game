#include "logic/coroutine_executor.h"

#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include "log/logger.h"
#include "monitor/metrics_sink.h"

namespace mir2::logic {
namespace {

constexpr int64_t kDefaultMaxRunningTasks = 20000;
constexpr const char* kEnvMaxRunningTasks = "LEGEND2_COROUTINE_MAX_RUNNING";
constexpr const char* kEnvTracePath = "LEGEND2_COROUTINE_TRACE_JSON_PATH";
constexpr const char* kEnvStarvationWatchdogThresholdMs =
    "LEGEND2_COROUTINE_STARVATION_THRESHOLD_MS";
constexpr const char* kEnvStarvationWatchdogPollMs =
    "LEGEND2_COROUTINE_STARVATION_POLL_INTERVAL_MS";

constexpr const char* kMetricSuspendedCount = "logic.coroutine.suspended_count";
constexpr const char* kMetricRunningCount = "logic.coroutine.running_count";
constexpr const char* kMetricPendingResumeCallbacks =
    "logic.coroutine.pending_resume_callbacks";
constexpr const char* kMetricTimeoutBackgroundInflight =
    "logic.coroutine.timeout_background_inflight";
constexpr const char* kMetricTimeoutBackgroundTotal =
    "logic.coroutine.timeout_background_total";
constexpr const char* kMetricAccepting = "logic.coroutine.accepting";
constexpr const char* kMetricMaxRunningLimit = "logic.coroutine.max_running_limit";
constexpr const char* kMetricSpawnRejectedTotal =
    "logic.coroutine.spawn_rejected_total";
constexpr const char* kMetricSpawnRejectedInvalidTaskTotal =
    "logic.coroutine.spawn_rejected_invalid_task_total";
constexpr const char* kMetricSpawnRejectedNotAcceptingTotal =
    "logic.coroutine.spawn_rejected_not_accepting_total";
constexpr const char* kMetricSpawnRejectedOverLimitTotal =
    "logic.coroutine.spawn_rejected_over_limit_total";
constexpr const char* kMetricDrainTimeoutTotal =
    "logic.coroutine.drain_timeout_total";
constexpr const char* kMetricRunningUnderflowTotal =
    "logic.coroutine.running_underflow_total";
constexpr const char* kMetricSuspendedUnderflowTotal =
    "logic.coroutine.suspended_underflow_total";
constexpr const char* kMetricPendingResumeUnderflowTotal =
    "logic.coroutine.pending_resume_underflow_total";
constexpr const char* kMetricTimeoutBackgroundInflightUnderflowTotal =
    "logic.coroutine.timeout_background_inflight_underflow_total";
constexpr const char* kMetricTaskUnhandledExceptionTotal =
    "logic.coroutine.task_unhandled_exception_total";
constexpr const char* kMetricTaskUnhandledExceptionTerminalTotal =
    "logic.coroutine.task_unhandled_exception_terminal_total";
constexpr const char* kMetricTaskDetachedTerminalExceptionTotal =
    "logic.coroutine.task_detached_terminal_exception_total";
constexpr const char* kMetricActiveTrackedCount =
    "logic.coroutine.active_tracked_count";
constexpr const char* kMetricHungDetectedTotal =
    "logic.coroutine.hung_detected_total";
constexpr const char* kMetricStarvationDetectedTotal =
    "logic.coroutine.starvation_detected_total";
constexpr const char* kMetricSnapshotTotal =
    "logic.coroutine.snapshot_total";
constexpr const char* kMetricTraceExportEnabled =
    "logic.coroutine.trace_export_enabled";

struct DecrementResult {
  int64_t value = 0;
  bool underflow = false;
};

DecrementResult DecrementClampNonNegative(std::atomic<int64_t>* counter) {
  int64_t current = counter->load(std::memory_order_acquire);
  while (true) {
    if (current <= 0) {
      return DecrementResult{0, true};
    }
    const int64_t next = current - 1;
    if (counter->compare_exchange_weak(current,
                                       next,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
      return DecrementResult{next, false};
    }
  }
}

int64_t ParsePositiveInt64FromEnv(const char* env_name) noexcept {
  const char* env = std::getenv(env_name);
  if (env == nullptr || *env == '\0') {
    return 0;
  }

  errno = 0;
  char* end = nullptr;
  const long long parsed = std::strtoll(env, &end, 10);
  if (errno != 0 || end == env || (end && *end != '\0')) {
    return 0;
  }
  if (parsed <= 0) {
    return 0;
  }
  if (parsed > std::numeric_limits<int64_t>::max()) {
    return std::numeric_limits<int64_t>::max();
  }
  return static_cast<int64_t>(parsed);
}

int64_t ParseMaxRunningTasksFromEnv() noexcept {
  return ParsePositiveInt64FromEnv(kEnvMaxRunningTasks);
}

std::chrono::steady_clock::duration ParseStarvationWatchdogThresholdFromEnv() noexcept {
  const int64_t ms = ParsePositiveInt64FromEnv(kEnvStarvationWatchdogThresholdMs);
  if (ms <= 0) {
    return std::chrono::steady_clock::duration::zero();
  }
  return std::chrono::milliseconds(ms);
}

std::chrono::steady_clock::duration ParseStarvationWatchdogPollFromEnv() noexcept {
  const int64_t ms = ParsePositiveInt64FromEnv(kEnvStarvationWatchdogPollMs);
  if (ms <= 0) {
    return std::chrono::steady_clock::duration::zero();
  }
  return std::chrono::milliseconds(ms);
}

std::chrono::steady_clock::duration ResolveStarvationWatchdogPollInterval(
    std::chrono::steady_clock::duration threshold,
    std::chrono::steady_clock::duration configured) noexcept {
  if (threshold <= std::chrono::steady_clock::duration::zero()) {
    return std::chrono::steady_clock::duration::zero();
  }

  if (configured > std::chrono::steady_clock::duration::zero()) {
    return std::min(configured, threshold);
  }

  const auto default_poll = std::max(
      std::chrono::milliseconds(10),
      std::chrono::duration_cast<std::chrono::milliseconds>(threshold / 4));
  return std::min(default_poll, std::chrono::milliseconds(500));
}

bool SleepWithStop(std::stop_token stop_token,
                   std::chrono::steady_clock::duration duration) noexcept {
  constexpr auto kSleepSlice = std::chrono::milliseconds(20);
  auto remaining = duration;
  while (remaining > std::chrono::steady_clock::duration::zero()) {
    if (stop_token.stop_requested()) {
      return false;
    }
    const auto slice = std::min(remaining, std::chrono::steady_clock::duration(kSleepSlice));
    std::this_thread::sleep_for(slice);
    remaining -= slice;
  }
  return !stop_token.stop_requested();
}

int64_t ResolveMaxRunningTasks(int64_t configured) noexcept {
  if (configured > 0) {
    return configured;
  }
  const int64_t env_limit = ParseMaxRunningTasksFromEnv();
  if (env_limit > 0) {
    return env_limit;
  }
  return kDefaultMaxRunningTasks;
}

std::string FormatExceptionMessage(const std::exception_ptr& exception) {
  if (!exception) {
    return "unknown";
  }
  try {
    std::rethrow_exception(exception);
  } catch (const std::exception& ex) {
    return ex.what();
  } catch (...) {
    return "non-std exception";
  }
}

uint64_t DurationToMilliseconds(std::chrono::steady_clock::duration duration) {
  if (duration <= std::chrono::steady_clock::duration::zero()) {
    return 0;
  }
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
}

bool IsSuspendedStatus(CoroutineStatus status) {
  return status == CoroutineStatus::kSuspendedAsync ||
         status == CoroutineStatus::kSuspendedSleep;
}

std::string EscapeJson(const std::string& text) {
  std::string escaped;
  escaped.reserve(text.size() + 16);
  for (const char ch : text) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

std::string NormalizeHandlerKey(std::string key) {
  if (key.empty()) {
    return "unknown";
  }

  for (char& ch : key) {
    const unsigned char value = static_cast<unsigned char>(ch);
    if (std::isalnum(value)) {
      ch = static_cast<char>(std::tolower(value));
      continue;
    }
    ch = '_';
  }
  return key;
}

CoroutineStatus ResolveSuspendedStatus(const std::string& reason) {
  if (reason.rfind("sleep", 0) == 0) {
    return CoroutineStatus::kSuspendedSleep;
  }
  return CoroutineStatus::kSuspendedAsync;
}

std::mutex& TaskMetricsSinkMutex() {
  static std::mutex mutex;
  return mutex;
}

std::weak_ptr<monitor::IMetricsSink>& TaskMetricsSinkSlot() {
  static std::weak_ptr<monitor::IMetricsSink> sink = monitor::CreateDefaultMetricsSink();
  return sink;
}

std::shared_ptr<monitor::IMetricsSink> LoadTaskMetricsSink() {
  std::lock_guard<std::mutex> lock(TaskMetricsSinkMutex());
  auto sink = TaskMetricsSinkSlot().lock();
  if (!sink) {
    sink = monitor::CreateDefaultMetricsSink();
    TaskMetricsSinkSlot() = sink;
  }
  return sink;
}

void StoreTaskMetricsSink(const std::shared_ptr<monitor::IMetricsSink>& sink) {
  std::lock_guard<std::mutex> lock(TaskMetricsSinkMutex());
  TaskMetricsSinkSlot() = sink ? sink : monitor::CreateDefaultMetricsSink();
}

void ReportTaskUnhandledExceptionDefault(bool terminal_exception,
                                         const std::exception_ptr& exception) noexcept {
  try {
    auto sink = LoadTaskMetricsSink();
    sink->IncrementCounter(kMetricTaskUnhandledExceptionTotal);
    if (terminal_exception) {
      sink->IncrementCounter(kMetricTaskUnhandledExceptionTerminalTotal);
      SYSLOG_ERROR("Coroutine task terminal unhandled exception: {}",
                   FormatExceptionMessage(exception));
    }
  } catch (...) {
  }
}

void ReportTaskDetachedExceptionDefault(const std::exception_ptr& exception) noexcept {
  try {
    LoadTaskMetricsSink()->IncrementCounter(kMetricTaskDetachedTerminalExceptionTotal);
    SYSLOG_ERROR("Detached coroutine terminal exception: {}",
                 FormatExceptionMessage(exception));
  } catch (...) {
  }
}

}  // namespace

struct CoroutineExecutor::TraceWriter {
  bool Open(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex);
    out.open(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
      return false;
    }
    start_time = std::chrono::steady_clock::now();
    first_event = true;
    out << "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[\n";
    return true;
  }

  void Close() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!out.is_open()) {
      return;
    }
    out << "\n]}\n";
    out.flush();
    out.close();
  }

  bool IsEnabled() const noexcept {
    std::lock_guard<std::mutex> lock(mutex);
    return out.is_open();
  }

  void WriteEvent(const std::string& phase,
                  const CoroutineRuntimeState& runtime,
                  std::chrono::steady_clock::time_point ts,
                  const char* reason) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!out.is_open()) {
      return;
    }

    const auto ts_us = std::chrono::duration_cast<std::chrono::microseconds>(
        ts - start_time);
    const auto tid = static_cast<uint64_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));

    if (!first_event) {
      out << ",\n";
    }
    first_event = false;

    out << "{\"name\":\"" << EscapeJson(runtime.name)
        << "\",\"cat\":\"coroutine\",\"ph\":\"" << phase
        << "\",\"ts\":" << ts_us.count()
        << ",\"pid\":1,\"tid\":" << tid;

    if (phase != "E") {
      out << ",\"args\":{"
          << "\"coroutine_id\":" << runtime.coroutine_id
          << ",\"trace_id\":" << runtime.trace_id
          << ",\"msg_id\":" << runtime.msg_id
          << ",\"client_id\":" << runtime.client_id
          << ",\"status\":\"" << EscapeJson(ToString(runtime.status)) << "\""
          << ",\"reason\":\"" << EscapeJson(reason ? reason : "") << "\""
          << "}";
    }

    out << "}";
  }

  mutable std::mutex mutex;
  std::ofstream out;
  std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
  bool first_event = true;
};

const char* ToString(SpawnResult result) noexcept {
  switch (result) {
    case SpawnResult::kSpawned:
      return "spawned";
    case SpawnResult::kInvalidTask:
      return "invalid_task";
    case SpawnResult::kNotAccepting:
      return "not_accepting";
    case SpawnResult::kOverLimit:
      return "over_limit";
  }
  return "unknown";
}

const char* ToString(CoroutineStatus status) noexcept {
  switch (status) {
    case CoroutineStatus::kQueued:
      return "queued";
    case CoroutineStatus::kRunning:
      return "running";
    case CoroutineStatus::kSuspendedAsync:
      return "suspended_async";
    case CoroutineStatus::kSuspendedSleep:
      return "suspended_sleep";
  }
  return "unknown";
}

CoroutineExecutor::CoroutineExecutor(asio::io_context& io_context,
                                     size_t blocking_threads,
                                     int64_t max_running_tasks,
                                     std::shared_ptr<monitor::IMetricsSink> metrics_sink)
    : io_context_(io_context),
      blocking_pool_(blocking_threads == 0 ? 1 : blocking_threads),
      lifetime_state_(std::make_shared<ExecutorLifetimeState>(io_context_)),
      metrics_sink_(std::move(metrics_sink)),
      timer_pool_(std::make_shared<ReusableTimerPool>(io_context_)),
      max_running_tasks_(ResolveMaxRunningTasks(max_running_tasks)) {
  if (!metrics_sink_) {
    metrics_sink_ = monitor::CreateDefaultMetricsSink();
  }
  StoreTaskMetricsSink(metrics_sink_);

  lifetime_state_->increment_suspended = [this]() { IncrementSuspended(); };
  lifetime_state_->decrement_suspended = [this]() { DecrementSuspended(); };
  lifetime_state_->decrement_running = [this]() { DecrementRunning(); };
  lifetime_state_->increment_pending_resume_callbacks = [this]() {
    IncrementPendingResumeCallbacks();
  };
  lifetime_state_->decrement_pending_resume_callbacks = [this]() {
    DecrementPendingResumeCallbacks();
  };
  lifetime_state_->increment_timeout_background_inflight = [this]() {
    IncrementTimeoutBackgroundInflight();
  };
  lifetime_state_->decrement_timeout_background_inflight = [this]() {
    DecrementTimeoutBackgroundInflight();
  };
  lifetime_state_->increment_timeout_background_total = [this]() {
    IncrementTimeoutBackgroundTotal();
  };
  SetTaskUnhandledExceptionReporter(&ReportTaskUnhandledExceptionDefault);
  SetTaskDetachedExceptionReporter(&ReportTaskDetachedExceptionDefault);

  SetMetricGauge(kMetricSuspendedCount, 0);
  SetMetricGauge(kMetricRunningCount, 0);
  SetMetricGauge(kMetricPendingResumeCallbacks, 0);
  SetMetricGauge(kMetricTimeoutBackgroundInflight, 0);
  SetMetricGauge(kMetricAccepting, 1);
  SetMetricGauge(kMetricMaxRunningLimit, static_cast<double>(max_running_tasks_));
  SetMetricGauge(kMetricActiveTrackedCount, 0);
  SetMetricGauge(kMetricTraceExportEnabled, 0);

  const char* trace_path = std::getenv(kEnvTracePath);
  if (trace_path != nullptr && *trace_path != '\0' &&
      !ConfigureTraceExport(trace_path)) {
    SYSLOG_WARN("Coroutine trace export init failed path={}", trace_path);
  }

  ConfigureStarvationWatchdog(
      ParseStarvationWatchdogThresholdFromEnv(),
      ParseStarvationWatchdogPollFromEnv());
}

CoroutineExecutor::~CoroutineExecutor() {
  StopAccepting();
  DrainAndJoin(std::chrono::seconds(5));
  StopStarvationWatchdog();
  if (timer_pool_) {
    auto pool = std::move(timer_pool_);
    pool->recycle_enabled.store(false, std::memory_order_release);
    {
      std::lock_guard<std::mutex> lock(pool->mutex);
      pool->free_timers.clear();
    }
  }

  std::shared_ptr<TraceWriter> writer;
  {
    std::lock_guard<std::mutex> lock(coroutine_mutex_);
    writer = std::move(trace_writer_);
  }
  if (writer) {
    writer->Close();
  }

  if (lifetime_state_) {
    lifetime_state_->RequestStop();
  }
}

void CoroutineExecutor::StopAccepting() noexcept {
  bool expected = true;
  if (accepting_tasks_.compare_exchange_strong(expected, false,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
    SetMetricGauge(kMetricAccepting, 0);
  }
}

bool CoroutineExecutor::DrainAndJoin(std::chrono::steady_clock::duration timeout) {
  StopAccepting();

  if (!blocking_pool_joined_.exchange(true, std::memory_order_acq_rel)) {
    blocking_pool_.join();
  }

  const bool drained = WaitForDrain(timeout);
  if (!drained) {
    IncrementMetricCounter(kMetricDrainTimeoutTotal);
  }
  return drained;
}

uint64_t CoroutineExecutor::AllocateTraceId() noexcept {
  uint64_t id = next_trace_id_.fetch_add(1, std::memory_order_relaxed);
  if (id == 0) {
    id = next_trace_id_.fetch_add(1, std::memory_order_relaxed);
  }
  return id;
}

size_t CoroutineExecutor::ActiveCoroutineCount() const noexcept {
  std::lock_guard<std::mutex> lock(coroutine_mutex_);
  return active_coroutines_.size();
}

std::shared_ptr<CoroutineExecutor::CoroutineRuntimeState>
CoroutineExecutor::CurrentRuntime() const {
  return CurrentRuntimeSlot();
}

std::string CoroutineExecutor::BuildHandlerGaugeMetricName(
    const std::string& handler_key) const {
  return "logic.handler." + NormalizeHandlerKey(handler_key) +
         ".coroutine_inflight";
}

std::shared_ptr<CoroutineExecutor::CoroutineRuntimeState>
CoroutineExecutor::RegisterCoroutine(CoroutineMetadata metadata) {
  auto runtime = std::make_shared<CoroutineRuntimeState>();
  const auto now = std::chrono::steady_clock::now();

  uint64_t coroutine_id = next_coroutine_id_.fetch_add(1, std::memory_order_relaxed);
  if (coroutine_id == 0) {
    coroutine_id = next_coroutine_id_.fetch_add(1, std::memory_order_relaxed);
  }

  runtime->created_at = now;
  runtime->last_state_time = now;
  runtime->last_suspend_time = now;
  runtime->coroutine_id = coroutine_id;
  runtime->trace_id = metadata.trace_id == 0 ? AllocateTraceId() : metadata.trace_id;
  runtime->msg_id = metadata.msg_id;
  runtime->client_id = metadata.client_id;
  runtime->name = metadata.name.empty() ? "anonymous" : std::move(metadata.name);
  runtime->source = metadata.source.empty() ? "executor" : std::move(metadata.source);
  runtime->handler_key = metadata.handler_key;
  runtime->last_state_reason = "queued";
  runtime->status = CoroutineStatus::kQueued;

  {
    std::lock_guard<std::mutex> lock(coroutine_mutex_);
    active_coroutines_[runtime->coroutine_id] = runtime;

    if (!runtime->handler_key.empty()) {
      const int64_t next = ++handler_inflight_counts_[runtime->handler_key];
      const std::string metric = BuildHandlerGaugeMetricName(runtime->handler_key);
      handler_metric_names_[runtime->handler_key] = metric;
      SetMetricGauge(metric, static_cast<double>(next));
    }

    SetMetricGauge(kMetricActiveTrackedCount,
                   static_cast<double>(active_coroutines_.size()));
  }

  EmitTraceInstant("i", *runtime, now, "created");
  return runtime;
}

void CoroutineExecutor::CompleteCoroutine(
    const std::shared_ptr<CoroutineRuntimeState>& runtime) {
  if (!runtime) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  bool removed = false;
  {
    std::lock_guard<std::mutex> lock(coroutine_mutex_);
    auto it = active_coroutines_.find(runtime->coroutine_id);
    if (it != active_coroutines_.end()) {
      active_coroutines_.erase(it);
      removed = true;
    }

    if (removed && !runtime->handler_key.empty()) {
      auto count_it = handler_inflight_counts_.find(runtime->handler_key);
      if (count_it != handler_inflight_counts_.end()) {
        count_it->second = std::max<int64_t>(0, count_it->second - 1);
        auto metric_it = handler_metric_names_.find(runtime->handler_key);
        if (metric_it != handler_metric_names_.end()) {
          SetMetricGauge(metric_it->second, static_cast<double>(count_it->second));
        }
      }
    }

    SetMetricGauge(kMetricActiveTrackedCount,
                   static_cast<double>(active_coroutines_.size()));
  }

  if (removed) {
    EmitTraceInstant("E", *runtime, now, "completed");
  }
}

void CoroutineExecutor::MarkCoroutineRunning(
    const std::shared_ptr<CoroutineRuntimeState>& runtime,
    const char* reason) {
  if (!runtime) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  bool should_emit = false;
  {
    std::lock_guard<std::mutex> lock(coroutine_mutex_);
    if (active_coroutines_.find(runtime->coroutine_id) == active_coroutines_.end()) {
      return;
    }

    runtime->status = CoroutineStatus::kRunning;
    runtime->last_state_time = now;
    runtime->last_state_reason = reason == nullptr ? "running" : reason;
    runtime->hung_reported = false;
    runtime->starvation_reported = false;
    ++runtime->resume_count;
    should_emit = true;
  }

  if (should_emit) {
    EmitTraceInstant("B", *runtime, now, reason);
  }
}

void CoroutineExecutor::MarkCoroutineSuspended(
    const std::shared_ptr<CoroutineRuntimeState>& runtime,
    const char* reason) {
  if (!runtime) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  bool should_emit = false;
  {
    std::lock_guard<std::mutex> lock(coroutine_mutex_);
    if (active_coroutines_.find(runtime->coroutine_id) == active_coroutines_.end()) {
      return;
    }

    const std::string text = reason == nullptr ? "suspended" : std::string(reason);
    runtime->status = ResolveSuspendedStatus(text);
    runtime->last_state_time = now;
    runtime->last_suspend_time = now;
    runtime->last_state_reason = text;
    runtime->starvation_reported = false;
    ++runtime->suspend_count;
    should_emit = true;
  }

  if (should_emit) {
    EmitTraceInstant("E", *runtime, now, reason);
  }
}

void CoroutineExecutor::ResumeHandle(
    std::coroutine_handle<> handle,
    const std::shared_ptr<CoroutineRuntimeState>& runtime,
    const char* reason) {
  if (!handle) {
    return;
  }

  if (runtime) {
    MarkCoroutineRunning(runtime, reason);
  }

  log::ScopedTraceLogContext trace_scope(
      runtime ? runtime->trace_id : 0,
      runtime ? runtime->coroutine_id : 0);
  auto previous_runtime = CurrentRuntimeSlot();
  CurrentRuntimeSlot() = runtime;
  handle.resume();
  CurrentRuntimeSlot() = std::move(previous_runtime);
}

CoroutineSnapshot CoroutineExecutor::BuildSnapshotLocked(
    const CoroutineRuntimeState& runtime,
    std::chrono::steady_clock::time_point now) const {
  CoroutineSnapshot snapshot;
  snapshot.coroutine_id = runtime.coroutine_id;
  snapshot.trace_id = runtime.trace_id;
  snapshot.name = runtime.name;
  snapshot.source = runtime.source;
  snapshot.handler_key = runtime.handler_key;
  snapshot.msg_id = runtime.msg_id;
  snapshot.client_id = runtime.client_id;
  snapshot.status = runtime.status;
  snapshot.last_state_reason = runtime.last_state_reason;
  snapshot.age_ms = DurationToMilliseconds(now - runtime.created_at);
  snapshot.since_last_state_ms = DurationToMilliseconds(now - runtime.last_state_time);
  snapshot.suspended_for_ms =
      IsSuspendedStatus(runtime.status)
          ? DurationToMilliseconds(now - runtime.last_suspend_time)
          : 0;
  snapshot.resume_count = runtime.resume_count;
  snapshot.suspend_count = runtime.suspend_count;
  return snapshot;
}

std::vector<CoroutineSnapshot> CoroutineExecutor::SnapshotActiveCoroutines(
    size_t max_entries) const {
  const auto now = std::chrono::steady_clock::now();
  std::vector<CoroutineSnapshot> snapshots;
  {
    std::lock_guard<std::mutex> lock(coroutine_mutex_);
    snapshots.reserve(active_coroutines_.size());
    for (const auto& [id, runtime] : active_coroutines_) {
      (void)id;
      snapshots.push_back(BuildSnapshotLocked(*runtime, now));
    }
  }

  std::sort(snapshots.begin(), snapshots.end(),
            [](const CoroutineSnapshot& lhs, const CoroutineSnapshot& rhs) {
              if (lhs.suspended_for_ms != rhs.suspended_for_ms) {
                return lhs.suspended_for_ms > rhs.suspended_for_ms;
              }
              return lhs.age_ms > rhs.age_ms;
            });

  if (max_entries > 0 && snapshots.size() > max_entries) {
    snapshots.resize(max_entries);
  }

  IncrementMetricCounter(kMetricSnapshotTotal);
  return snapshots;
}

std::optional<CoroutineSnapshot> CoroutineExecutor::SnapshotCoroutine(
    uint64_t coroutine_id) const {
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(coroutine_mutex_);
  auto it = active_coroutines_.find(coroutine_id);
  if (it == active_coroutines_.end()) {
    return std::nullopt;
  }
  return BuildSnapshotLocked(*it->second, now);
}

std::vector<CoroutineSnapshot> CoroutineExecutor::DetectHungCoroutines(
    std::chrono::steady_clock::duration threshold,
    size_t max_entries) {
  if (threshold <= std::chrono::steady_clock::duration::zero()) {
    return {};
  }

  const auto now = std::chrono::steady_clock::now();
  std::vector<CoroutineSnapshot> hung;
  {
    std::lock_guard<std::mutex> lock(coroutine_mutex_);
    for (const auto& [id, runtime] : active_coroutines_) {
      (void)id;
      if (!IsSuspendedStatus(runtime->status)) {
        continue;
      }
      if (runtime->hung_reported) {
        continue;
      }
      if (now - runtime->last_suspend_time < threshold) {
        continue;
      }

      runtime->hung_reported = true;
      hung.push_back(BuildSnapshotLocked(*runtime, now));
    }
  }

  std::sort(hung.begin(), hung.end(),
            [](const CoroutineSnapshot& lhs, const CoroutineSnapshot& rhs) {
              return lhs.suspended_for_ms > rhs.suspended_for_ms;
            });

  if (max_entries > 0 && hung.size() > max_entries) {
    hung.resize(max_entries);
  }

  if (!hung.empty()) {
    IncrementMetricCounter(kMetricHungDetectedTotal,
                           static_cast<uint64_t>(hung.size()));
  }

  return hung;
}

std::vector<CoroutineSnapshot> CoroutineExecutor::DetectStarvingCoroutines(
    std::chrono::steady_clock::duration threshold,
    size_t max_entries) {
  if (threshold <= std::chrono::steady_clock::duration::zero()) {
    return {};
  }

  const auto now = std::chrono::steady_clock::now();
  std::vector<CoroutineSnapshot> starving;
  {
    std::lock_guard<std::mutex> lock(coroutine_mutex_);
    for (const auto& [id, runtime] : active_coroutines_) {
      (void)id;
      if (runtime->status != CoroutineStatus::kRunning) {
        continue;
      }
      if (runtime->starvation_reported) {
        continue;
      }
      if (now - runtime->last_state_time < threshold) {
        continue;
      }

      runtime->starvation_reported = true;
      starving.push_back(BuildSnapshotLocked(*runtime, now));
    }
  }

  std::sort(starving.begin(), starving.end(),
            [](const CoroutineSnapshot& lhs, const CoroutineSnapshot& rhs) {
              return lhs.since_last_state_ms > rhs.since_last_state_ms;
            });

  if (max_entries > 0 && starving.size() > max_entries) {
    starving.resize(max_entries);
  }

  if (!starving.empty()) {
    IncrementMetricCounter(kMetricStarvationDetectedTotal,
                           static_cast<uint64_t>(starving.size()));
  }

  return starving;
}

void CoroutineExecutor::ConfigureStarvationWatchdog(
    std::chrono::steady_clock::duration threshold,
    std::chrono::steady_clock::duration poll_interval) {
  StopStarvationWatchdog();

  if (threshold <= std::chrono::steady_clock::duration::zero()) {
    starvation_watchdog_threshold_ = std::chrono::steady_clock::duration::zero();
    starvation_watchdog_poll_interval_ = std::chrono::steady_clock::duration::zero();
    return;
  }

  starvation_watchdog_threshold_ = threshold;
  starvation_watchdog_poll_interval_ =
      ResolveStarvationWatchdogPollInterval(threshold, poll_interval);
  starvation_watchdog_thread_ =
      std::jthread([this](std::stop_token stop_token) {
        RunStarvationWatchdog(stop_token);
      });
}

bool CoroutineExecutor::ConfigureTraceExport(const std::string& trace_file_path) {
  if (trace_file_path.empty()) {
    std::shared_ptr<TraceWriter> old_writer;
    {
      std::lock_guard<std::mutex> lock(coroutine_mutex_);
      old_writer = std::move(trace_writer_);
      SetMetricGauge(kMetricTraceExportEnabled, 0);
    }
    if (old_writer) {
      old_writer->Close();
    }
    return true;
  }

  auto writer = std::make_shared<TraceWriter>();
  if (!writer->Open(trace_file_path)) {
    return false;
  }

  std::shared_ptr<TraceWriter> old_writer;
  {
    std::lock_guard<std::mutex> lock(coroutine_mutex_);
    old_writer = std::move(trace_writer_);
    trace_writer_ = writer;
    SetMetricGauge(kMetricTraceExportEnabled, 1);
  }

  if (old_writer) {
    old_writer->Close();
  }

  SYSLOG_INFO("Coroutine trace export enabled path={}", trace_file_path);
  return true;
}

bool CoroutineExecutor::IsTraceExportEnabled() const noexcept {
  std::shared_ptr<TraceWriter> writer;
  {
    std::lock_guard<std::mutex> lock(coroutine_mutex_);
    writer = trace_writer_;
  }
  return writer && writer->IsEnabled();
}

void CoroutineExecutor::IncrementMetricCounter(const std::string& name,
                                               uint64_t delta) const {
  if (!metrics_sink_) {
    return;
  }
  if (delta == 1) {
    metrics_sink_->IncrementCounter(name);
    return;
  }
  metrics_sink_->IncrementCounter(name, delta);
}

void CoroutineExecutor::SetMetricGauge(const std::string& name, double value) const {
  if (!metrics_sink_) {
    return;
  }
  metrics_sink_->SetGauge(name, value);
}

std::shared_ptr<asio::steady_timer> CoroutineExecutor::AcquireReusableTimer() {
  auto pool = timer_pool_;
  if (!pool) {
    return std::make_shared<asio::steady_timer>(io_context_);
  }

  std::unique_ptr<asio::steady_timer> timer;
  {
    std::lock_guard<std::mutex> lock(pool->mutex);
    if (!pool->free_timers.empty()) {
      timer = std::move(pool->free_timers.back());
      pool->free_timers.pop_back();
    }
  }

  if (!timer) {
    timer = std::make_unique<asio::steady_timer>(pool->io_context);
  }

  return std::shared_ptr<asio::steady_timer>(
      timer.release(),
      [weak_pool = std::weak_ptr<ReusableTimerPool>(pool)](asio::steady_timer* raw_timer) noexcept {
        RecycleTimer(std::move(weak_pool), raw_timer);
      });
}

void CoroutineExecutor::RecycleTimer(std::weak_ptr<ReusableTimerPool> weak_pool,
                                     asio::steady_timer* timer) noexcept {
  if (timer == nullptr) {
    return;
  }

  std::unique_ptr<asio::steady_timer> owned(timer);
  auto pool = weak_pool.lock();
  if (!pool || !pool->recycle_enabled.load(std::memory_order_acquire)) {
    return;
  }

  asio::error_code ignored;
  owned->cancel(ignored);

  std::lock_guard<std::mutex> lock(pool->mutex);
  if (!pool->recycle_enabled.load(std::memory_order_acquire) ||
      pool->free_timers.size() >= pool->max_cached) {
    return;
  }
  pool->free_timers.push_back(std::move(owned));
}

void CoroutineExecutor::EmitTraceInstant(const char* phase,
                                         const CoroutineRuntimeState& runtime,
                                         std::chrono::steady_clock::time_point ts,
                                         const char* reason) {
  std::shared_ptr<TraceWriter> writer;
  {
    std::lock_guard<std::mutex> lock(coroutine_mutex_);
    writer = trace_writer_;
  }

  if (!writer) {
    return;
  }
  writer->WriteEvent(phase == nullptr ? "i" : phase, runtime, ts, reason);
}

SpawnResult CoroutineExecutor::BeginSpawn() {
  if (!accepting_tasks_.load(std::memory_order_acquire)) {
    RecordSpawnRejected(SpawnResult::kNotAccepting);
    return SpawnResult::kNotAccepting;
  }

  while (true) {
    int64_t running = running_count_.load(std::memory_order_acquire);
    if (running >= max_running_tasks_) {
      RecordSpawnRejected(SpawnResult::kOverLimit);
      return SpawnResult::kOverLimit;
    }
    if (!accepting_tasks_.load(std::memory_order_acquire)) {
      RecordSpawnRejected(SpawnResult::kNotAccepting);
      return SpawnResult::kNotAccepting;
    }
    if (!running_count_.compare_exchange_weak(running,
                                              running + 1,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire)) {
      continue;
    }

    SetMetricGauge(kMetricRunningCount, running + 1);
    NotifyDrainWaiters();

    if (!accepting_tasks_.load(std::memory_order_acquire)) {
      DecrementRunning();
      RecordSpawnRejected(SpawnResult::kNotAccepting);
      return SpawnResult::kNotAccepting;
    }

    return SpawnResult::kSpawned;
  }
}

void CoroutineExecutor::RecordSpawnRejected(SpawnResult reason) noexcept {
  IncrementMetricCounter(kMetricSpawnRejectedTotal);
  switch (reason) {
    case SpawnResult::kInvalidTask:
      IncrementMetricCounter(kMetricSpawnRejectedInvalidTaskTotal);
      break;
    case SpawnResult::kNotAccepting:
      IncrementMetricCounter(kMetricSpawnRejectedNotAcceptingTotal);
      break;
    case SpawnResult::kOverLimit:
      IncrementMetricCounter(kMetricSpawnRejectedOverLimitTotal);
      break;
    case SpawnResult::kSpawned:
      break;
  }
}

void CoroutineExecutor::StopStarvationWatchdog() noexcept {
  if (!starvation_watchdog_thread_.joinable()) {
    return;
  }

  starvation_watchdog_thread_.request_stop();
  starvation_watchdog_thread_.join();
}

void CoroutineExecutor::RunStarvationWatchdog(std::stop_token stop_token) {
  while (!stop_token.stop_requested()) {
    if (!SleepWithStop(stop_token, starvation_watchdog_poll_interval_)) {
      return;
    }

    const auto starving = DetectStarvingCoroutines(starvation_watchdog_threshold_, 32);
    for (const auto& snapshot : starving) {
      SYSLOG_WARN(
          "Coroutine starvation detected: coroutine_id={} trace_id={} name={} source={} "
          "handler_key={} msg_id={} client_id={} running_ms={} status={}",
          snapshot.coroutine_id,
          snapshot.trace_id,
          snapshot.name,
          snapshot.source,
          snapshot.handler_key,
          snapshot.msg_id,
          snapshot.client_id,
          snapshot.since_last_state_ms,
          ToString(snapshot.status));
    }
  }
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
  const auto value = suspended_count_.fetch_add(1, std::memory_order_acq_rel) + 1;
  SetMetricGauge(kMetricSuspendedCount, value);
  NotifyDrainWaiters();
}

void CoroutineExecutor::DecrementSuspended() {
  const DecrementResult result = DecrementClampNonNegative(&suspended_count_);
  if (result.underflow) {
    IncrementMetricCounter(kMetricSuspendedUnderflowTotal);
  }
  SetMetricGauge(kMetricSuspendedCount, result.value);
  NotifyDrainWaiters();
}

void CoroutineExecutor::IncrementRunning() {
  const auto value = running_count_.fetch_add(1, std::memory_order_acq_rel) + 1;
  SetMetricGauge(kMetricRunningCount, value);
  NotifyDrainWaiters();
}

void CoroutineExecutor::DecrementRunning() {
  const DecrementResult result = DecrementClampNonNegative(&running_count_);
  if (result.underflow) {
    IncrementMetricCounter(kMetricRunningUnderflowTotal);
  }
  SetMetricGauge(kMetricRunningCount, result.value);
  NotifyDrainWaiters();
}

void CoroutineExecutor::IncrementPendingResumeCallbacks() {
  const auto value =
      pending_resume_callbacks_.fetch_add(1, std::memory_order_acq_rel) + 1;
  SetMetricGauge(kMetricPendingResumeCallbacks, value);
  NotifyDrainWaiters();
}

void CoroutineExecutor::DecrementPendingResumeCallbacks() {
  const DecrementResult result =
      DecrementClampNonNegative(&pending_resume_callbacks_);
  if (result.underflow) {
    IncrementMetricCounter(kMetricPendingResumeUnderflowTotal);
  }
  SetMetricGauge(kMetricPendingResumeCallbacks, result.value);
  NotifyDrainWaiters();
}

void CoroutineExecutor::IncrementTimeoutBackgroundInflight() {
  const auto value =
      timeout_background_inflight_.fetch_add(1, std::memory_order_acq_rel) + 1;
  SetMetricGauge(kMetricTimeoutBackgroundInflight, value);
}

void CoroutineExecutor::DecrementTimeoutBackgroundInflight() {
  const DecrementResult result =
      DecrementClampNonNegative(&timeout_background_inflight_);
  if (result.underflow) {
    IncrementMetricCounter(kMetricTimeoutBackgroundInflightUnderflowTotal);
  }
  SetMetricGauge(kMetricTimeoutBackgroundInflight, result.value);
}

void CoroutineExecutor::IncrementTimeoutBackgroundTotal() {
  IncrementMetricCounter(kMetricTimeoutBackgroundTotal);
}

}  // namespace mir2::logic
