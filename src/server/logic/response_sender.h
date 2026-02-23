/**
 * @file response_sender.h
 * @brief Response sender for routing client messages.
 */

#ifndef MIR2_LOGIC_RESPONSE_SENDER_H_
#define MIR2_LOGIC_RESPONSE_SENDER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "logic/task.h"

namespace mir2::network {
class NetworkManager;
}

namespace mir2::logic {

class CoroutineExecutor;

struct SendManyResult {
  size_t attempted = 0;
  size_t sent = 0;
  size_t failed = 0;
  size_t dropped = 0;
};

class ResponseSender {
 public:
  using SendHook =
      std::function<void(uint64_t, uint16_t, const std::vector<uint8_t>&)>;

  explicit ResponseSender(mir2::network::NetworkManager& network,
                          SendHook send_hook = {});
  virtual ~ResponseSender() = default;

  // Sync send.
  virtual void Send(uint64_t client_id,
                    uint16_t msg_id,
                    const std::vector<uint8_t>& payload);

  // Async send (coroutine friendly).
  virtual Task<void> SendAsync(uint64_t client_id,
                               uint16_t msg_id,
                               std::vector<uint8_t> payload);

  // Batch send (coroutine friendly).
  virtual Task<SendManyResult> SendMany(const std::vector<uint64_t>& client_ids,
                                        uint16_t msg_id,
                                        const std::vector<uint8_t>& payload);

 private:
  mir2::network::NetworkManager& network_;
  SendHook send_hook_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_RESPONSE_SENDER_H_
