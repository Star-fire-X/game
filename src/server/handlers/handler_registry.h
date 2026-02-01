/**
 * @file handler_registry.h
 * @brief Handler注册表
 */

#ifndef LEGEND2_SERVER_HANDLERS_HANDLER_REGISTRY_H
#define LEGEND2_SERVER_HANDLERS_HANDLER_REGISTRY_H

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "handlers/base_handler.h"

namespace legend2::handlers {

/**
 * @brief Handler注册与分发
 */
class HandlerRegistry {
public:
    void Register(uint16_t msg_id, std::shared_ptr<IMessageHandler> handler);

    bool Dispatch(const HandlerContext& context,
                  uint16_t msg_id,
                  const std::vector<uint8_t>& payload,
                  ResponseCallback callback) const;

private:
    std::unordered_map<uint16_t, std::shared_ptr<IMessageHandler>> handlers_;
};

}  // namespace legend2::handlers

#endif  // LEGEND2_SERVER_HANDLERS_HANDLER_REGISTRY_H
