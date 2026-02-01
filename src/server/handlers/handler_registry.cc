#include "handlers/handler_registry.h"

#include <utility>

namespace legend2::handlers {

void HandlerRegistry::Register(uint16_t msg_id, std::shared_ptr<IMessageHandler> handler) {
    handlers_[msg_id] = std::move(handler);
}

bool HandlerRegistry::Dispatch(const HandlerContext& context,
                               uint16_t msg_id,
                               const std::vector<uint8_t>& payload,
                               ResponseCallback callback) const {
    auto it = handlers_.find(msg_id);
    if (it == handlers_.end() || !it->second) {
        return false;
    }
    it->second->Handle(context, msg_id, payload, std::move(callback));
    return true;
}

}  // namespace legend2::handlers
