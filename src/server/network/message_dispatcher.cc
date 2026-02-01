#include "network/message_dispatcher.h"

#include <chrono>

#include "monitor/metrics.h"

namespace mir2::network {

void MessageDispatcher::RegisterHandler(uint16_t msg_id, MessageHandler handler) {
  handlers_[msg_id] = std::move(handler);
}

void MessageDispatcher::Dispatch(const std::shared_ptr<TcpSession>& session, uint16_t msg_id,
                                 const std::vector<uint8_t>& payload) const {
  auto it = handlers_.find(msg_id);
  if (it != handlers_.end() && it->second) {
    const auto start = std::chrono::steady_clock::now();
    it->second(session, payload);
    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - start)
                                .count();
    monitor::Metrics::Instance().ObserveDispatchLatency(elapsed_us);
  }
}

}  // namespace mir2::network
