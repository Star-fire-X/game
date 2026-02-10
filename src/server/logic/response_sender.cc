#include "logic/response_sender.h"

#include <utility>

#include "common/enums.h"
#include "common/internal_message_helper.h"
#include "network/network_manager.h"

namespace mir2::logic {

ResponseSender::ResponseSender(mir2::network::NetworkManager& network,
                               SendHook send_hook)
    : network_(network), send_hook_(std::move(send_hook)) {}

void ResponseSender::Send(uint64_t client_id,
                          uint16_t msg_id,
                          const std::vector<uint8_t>& payload) {
  if (send_hook_) {
    send_hook_(client_id, msg_id, payload);
    return;
  }
  const auto routed_payload = mir2::common::BuildRoutedMessage(client_id, msg_id, payload);
  network_.Broadcast(
      static_cast<uint16_t>(mir2::common::InternalMsgId::kRoutedMessage),
      routed_payload);
}

Task<void> ResponseSender::SendAsync(uint64_t client_id,
                                     uint16_t msg_id,
                                     std::vector<uint8_t> payload) {
  Send(client_id, msg_id, payload);
  co_return;
}

}  // namespace mir2::logic
