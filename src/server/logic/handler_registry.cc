#include "logic/handler_registry.h"

#include <chrono>
#include <memory>
#include <utility>
#include <vector>

#include "logic/coroutine_executor.h"
#include "monitor/metrics.h"

namespace mir2::logic {
namespace {

Task<void> RunMeasuredTask(std::shared_ptr<std::vector<uint8_t>> payload_copy,
                           Task<void> task,
                           std::chrono::steady_clock::time_point start) {
  auto keep_payload_alive = std::move(payload_copy);
  (void)keep_payload_alive;

  struct LatencyGuard {
    std::chrono::steady_clock::time_point start_time;
    ~LatencyGuard() noexcept {
      const auto elapsed_ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                    start_time)
              .count();
      monitor::Metrics::Instance().ObserveHandlerLatency(elapsed_ms);
    }
  } guard{start};

  co_await std::move(task);
}

}  // namespace

HandlerRegistry::HandlerRegistry(CoroutineExecutor& executor)
    : executor_(executor) {}

void HandlerRegistry::RegisterHandler(uint16_t msg_id, HandlerFunc handler) {
  handlers_[msg_id] = std::move(handler);
}

bool HandlerRegistry::DispatchMessage(const HandlerContext& context,
                                      uint16_t msg_id,
                                      const uint8_t* payload,
                                      size_t payload_size) const {
  Task<void> task(Task<void>::Handle{});
  if (!CreateTask(context, msg_id, payload, payload_size, &task)) {
    return false;
  }
  return executor_.Spawn(std::move(task));
}

bool HandlerRegistry::CreateTask(const HandlerContext& context,
                                 uint16_t msg_id,
                                 const uint8_t* payload,
                                 size_t payload_size,
                                 Task<void>* out_task) const {
  if (!out_task) {
    return false;
  }

  auto it = handlers_.find(msg_id);
  if (it == handlers_.end() || !it->second) {
    return false;
  }

  auto payload_copy = std::make_shared<std::vector<uint8_t>>();
  if (payload && payload_size > 0) {
    payload_copy->assign(payload, payload + payload_size);
  }

  const auto start = std::chrono::steady_clock::now();
  HandlerContext dispatch_context = context;
  dispatch_context.msg_id = msg_id;
  auto task = it->second(dispatch_context, payload_copy->data(), payload_copy->size());
  *out_task = RunMeasuredTask(std::move(payload_copy), std::move(task), start);
  return true;
}

}  // namespace mir2::logic
