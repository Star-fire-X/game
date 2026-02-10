/**
 * @file handler_registry.h
 * @brief Coroutine-aware handler registry.
 */

#ifndef MIR2_LOGIC_HANDLER_REGISTRY_H_
#define MIR2_LOGIC_HANDLER_REGISTRY_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>

#include "logic/handler_context.h"
#include "logic/task.h"

namespace mir2::logic {

class CoroutineExecutor;

class HandlerRegistry {
 public:
  using HandlerFunc = std::function<Task<void>(HandlerContext, const uint8_t*, size_t)>;

  explicit HandlerRegistry(CoroutineExecutor& executor);

  void RegisterHandler(uint16_t msg_id, HandlerFunc handler);

  bool CreateTask(const HandlerContext& context,
                  uint16_t msg_id,
                  const uint8_t* payload,
                  size_t payload_size,
                  Task<void>* out_task) const;

  bool DispatchMessage(const HandlerContext& context,
                       uint16_t msg_id,
                       const uint8_t* payload,
                       size_t payload_size) const;

 private:
  CoroutineExecutor& executor_;
  std::unordered_map<uint16_t, HandlerFunc> handlers_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLER_REGISTRY_H_
