#include "logic/thread_affinity.h"

#include <functional>
#include <mutex>

#include "log/logger.h"
#include "monitor/metrics.h"

namespace mir2::logic {

namespace {

constexpr const char* kMetricThreadAffinityViolationTotal =
    "logic.thread_affinity_violation_total";

std::mutex g_logic_thread_mutex;
std::thread::id g_logic_thread_id;

void RecordViolation(const char* tag, const char* reason) {
  monitor::Metrics::Instance().IncrementCounter(kMetricThreadAffinityViolationTotal);
  SYSLOG_ERROR("Thread affinity violation tag={} reason={} current_thread={}",
               tag == nullptr ? "unknown" : tag,
               reason == nullptr ? "unknown" : reason,
               std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

}  // namespace

void BindLogicThread(std::thread::id thread_id) {
  std::lock_guard<std::mutex> lock(g_logic_thread_mutex);
  g_logic_thread_id = thread_id;
}

void ClearLogicThread() {
  std::lock_guard<std::mutex> lock(g_logic_thread_mutex);
  g_logic_thread_id = std::thread::id();
}

bool IsLogicThreadBound() {
  std::lock_guard<std::mutex> lock(g_logic_thread_mutex);
  return g_logic_thread_id != std::thread::id();
}

bool IsOnLogicThread() {
  std::lock_guard<std::mutex> lock(g_logic_thread_mutex);
  if (g_logic_thread_id == std::thread::id()) {
    return false;
  }
  return std::this_thread::get_id() == g_logic_thread_id;
}

bool AssertOnLogicThread(const char* tag) {
  std::lock_guard<std::mutex> lock(g_logic_thread_mutex);
  if (g_logic_thread_id == std::thread::id()) {
    return true;
  }
  if (std::this_thread::get_id() != g_logic_thread_id) {
    RecordViolation(tag, "wrong_thread");
    return false;
  }
  return true;
}

bool AssertNotOnLogicThread(const char* tag) {
  std::lock_guard<std::mutex> lock(g_logic_thread_mutex);
  if (g_logic_thread_id == std::thread::id()) {
    return true;
  }
  if (std::this_thread::get_id() == g_logic_thread_id) {
    RecordViolation(tag, "forbidden_on_logic_thread");
    return false;
  }
  return true;
}

}  // namespace mir2::logic
