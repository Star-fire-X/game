/**
 * @file handler_registry.h
 * @brief Coroutine-aware handler registry.
 */

#ifndef MIR2_LOGIC_HANDLER_REGISTRY_H_
#define MIR2_LOGIC_HANDLER_REGISTRY_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "logic/handler_context.h"
#include "logic/task.h"

namespace mir2::logic {

class CoroutineExecutor;
struct CoroutineMetadata;
enum class SpawnResult : uint8_t;

class HandlerRegistry {
 public:
  using HandlerFunc = std::function<Task<void>(HandlerContext, const uint8_t*, size_t)>;

  explicit HandlerRegistry(CoroutineExecutor& executor);

  void RegisterHandler(uint16_t msg_id,
                       HandlerFunc handler,
                       std::string handler_name = {});

  bool CreateTask(const HandlerContext& context,
                  uint16_t msg_id,
                  const uint8_t* payload,
                  size_t payload_size,
                  Task<void>* out_task,
                  CoroutineMetadata* out_metadata = nullptr) const;

  SpawnResult DispatchMessage(const HandlerContext& context,
                              uint16_t msg_id,
                              const uint8_t* payload,
                              size_t payload_size) const;

 private:
  static constexpr size_t kHandlerTableSize =
      static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1;

  struct HandlerEntry {
    HandlerFunc handler;
    std::string name;
    std::string handler_key;
  };

  CoroutineExecutor& executor_;
  std::vector<HandlerEntry> handlers_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLER_REGISTRY_H_
