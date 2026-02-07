/**
 * @file chat_handler.h
 * @brief 聊天相关处理
 */

#ifndef LEGEND2_SERVER_HANDLERS_CHAT_HANDLER_H
#define LEGEND2_SERVER_HANDLERS_CHAT_HANDLER_H

#include "handlers/base_handler.h"
#include "handlers/client_registry.h"

namespace legend2::handlers {

/**
 * @brief 聊天Handler
 */
class ChatHandler : public BaseHandler {
public:
    explicit ChatHandler(ClientRegistry& registry);

protected:
    void DoHandle(const HandlerContext& context,
                  uint16_t msg_id,
                  const std::vector<uint8_t>& payload,
                  ResponseCallback callback) override;

    void OnError(const HandlerContext& context,
                 uint16_t msg_id,
                 mir2::common::ErrorCode error_code,
                 ResponseCallback callback) override;

private:
    void HandleChat(const HandlerContext& context,
                    const std::vector<uint8_t>& payload,
                    ResponseCallback callback);

    ClientRegistry& client_registry_;
};

}  // namespace legend2::handlers

#endif  // LEGEND2_SERVER_HANDLERS_CHAT_HANDLER_H
