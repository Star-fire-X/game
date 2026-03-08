#include "logic/handler_registry.h"

#include <chrono>
#include <cctype>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "logic/coroutine_executor.h"
#include "log/logger.h"
#include "monitor/metrics.h"

namespace mir2::logic {
namespace {

constexpr const char* kMetricHandlerSpawnRejectedTotal =
    "logic.handler.spawn_rejected_total";

std::string BuildDefaultHandlerName(uint16_t msg_id) {
  return "msg_" + std::to_string(msg_id);
}

std::string NormalizeHandlerKey(std::string name) {
  if (name.empty()) {
    return "unknown";
  }
  for (char& ch : name) {
    const unsigned char value = static_cast<unsigned char>(ch);
    if (std::isalnum(value)) {
      ch = static_cast<char>(std::tolower(value));
    } else {
      ch = '_';
    }
  }
  return name;
}

struct PayloadCopy {
  std::unique_ptr<uint8_t[]> bytes;
  size_t size = 0;

  const uint8_t* Data() const noexcept {
    return size == 0 ? nullptr : bytes.get();
  }
};

PayloadCopy CopyPayload(const uint8_t* payload, size_t payload_size) {
  PayloadCopy copy;
  if (payload == nullptr || payload_size == 0) {
    return copy;
  }
  copy.bytes = std::make_unique<uint8_t[]>(payload_size);
  std::memcpy(copy.bytes.get(), payload, payload_size);
  copy.size = payload_size;
  return copy;
}

Task<void> RunMeasuredTask(PayloadCopy payload_copy,
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
    : executor_(executor), handlers_(kHandlerTableSize) {}

void HandlerRegistry::RegisterHandler(uint16_t msg_id,
                                      HandlerFunc handler,
                                      std::string handler_name) {
  HandlerEntry entry;
  entry.handler = std::move(handler);
  entry.name = handler_name.empty() ? BuildDefaultHandlerName(msg_id)
                                    : std::move(handler_name);
  entry.handler_key = NormalizeHandlerKey(entry.name);
  handlers_[msg_id] = std::move(entry);
}

SpawnResult HandlerRegistry::DispatchMessage(const HandlerContext& context,
                                             uint16_t msg_id,
                                             const uint8_t* payload,
                                             size_t payload_size) const {
  Task<void> task(Task<void>::Handle{});
  CoroutineMetadata metadata;
  if (!CreateTask(context, msg_id, payload, payload_size, &task, &metadata)) {
    return SpawnResult::kInvalidTask;
  }

  const uint64_t client_id = context.client_id;
  const uint64_t trace_id = metadata.trace_id;
  return executor_.SpawnOrDrop(
      std::move(task),
      [client_id, msg_id, trace_id](SpawnResult reason) {
        monitor::Metrics::Instance().IncrementCounter(kMetricHandlerSpawnRejectedTotal);
        SYSLOG_WARN("HandlerRegistry dropped task client_id={} msg_id={} trace_id={} reason={}",
                    client_id,
                    msg_id,
                    trace_id,
                    ToString(reason));
      },
      std::move(metadata));
}

bool HandlerRegistry::CreateTask(const HandlerContext& context,
                                 uint16_t msg_id,
                                 const uint8_t* payload,
                                 size_t payload_size,
                                 Task<void>* out_task,
                                 CoroutineMetadata* out_metadata) const {
  if (!out_task) {
    return false;
  }

  const HandlerEntry& entry = handlers_[msg_id];
  if (!entry.handler) {
    return false;
  }

  PayloadCopy payload_copy = CopyPayload(payload, payload_size);

  const auto start = std::chrono::steady_clock::now();
  HandlerContext dispatch_context = context;
  dispatch_context.msg_id = msg_id;
  if (dispatch_context.trace_id == 0) {
    dispatch_context.trace_id = executor_.AllocateTraceId();
  }
  auto task = entry.handler(dispatch_context, payload_copy.Data(), payload_copy.size);
  if (out_metadata != nullptr) {
    out_metadata->trace_id = dispatch_context.trace_id;
    out_metadata->name = "handler." + entry.name;
    out_metadata->source = "handler_registry";
    out_metadata->handler_key = entry.handler_key;
    out_metadata->msg_id = msg_id;
    out_metadata->client_id = dispatch_context.client_id;
  }
  *out_task = RunMeasuredTask(std::move(payload_copy), std::move(task), start);
  return true;
}

}  // namespace mir2::logic
