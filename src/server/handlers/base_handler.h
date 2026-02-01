/**
 * @file base_handler.h
 * @brief 统一消息处理基类
 */

#ifndef LEGEND2_SERVER_HANDLERS_BASE_HANDLER_H
#define LEGEND2_SERVER_HANDLERS_BASE_HANDLER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "server/common/error_codes.h"
#include "log/logger.h"
#include "network/tcp_session.h"

namespace legend2::handlers {

/**
 * @brief 单条响应消息
 */
struct HandlerResponse {
    uint64_t client_id = 0;
    uint16_t msg_id = 0;
    std::vector<uint8_t> payload;
};

using ResponseList = std::vector<HandlerResponse>;
using ResponseCallback = std::function<void(const ResponseList&)>;

/**
 * @brief 处理上下文
 */
struct HandlerContext {
    uint64_t client_id = 0;
    std::shared_ptr<mir2::network::TcpSession> session;
    std::function<void(std::function<void()>)> post;
};

/**
 * @brief 消息处理接口
 */
class IMessageHandler {
public:
    virtual ~IMessageHandler() = default;
    virtual void Handle(const HandlerContext& context,
                        uint16_t msg_id,
                        const std::vector<uint8_t>& payload,
                        ResponseCallback callback) = 0;
};

/**
 * @brief 消息处理基类
 */
class BaseHandler : public IMessageHandler {
public:
    explicit BaseHandler(mir2::log::LogCategory category);

    void Handle(const HandlerContext& context,
                uint16_t msg_id,
                const std::vector<uint8_t>& payload,
                ResponseCallback callback) final;

protected:
    virtual bool PreHandle(const HandlerContext& context,
                           uint16_t msg_id,
                           const std::vector<uint8_t>& payload,
                           mir2::common::ErrorCode* error_code);

    virtual void DoHandle(const HandlerContext& context,
                          uint16_t msg_id,
                          const std::vector<uint8_t>& payload,
                          ResponseCallback callback) = 0;

    virtual void OnError(const HandlerContext& context,
                         uint16_t msg_id,
                         mir2::common::ErrorCode error_code,
                         ResponseCallback callback);

    mir2::log::LogCategory log_category_;
};

}  // namespace legend2::handlers

#endif  // LEGEND2_SERVER_HANDLERS_BASE_HANDLER_H
