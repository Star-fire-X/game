#include "handlers/base_handler.h"

#include <exception>
#include <utility>

namespace legend2::handlers {

BaseHandler::BaseHandler(mir2::log::LogCategory category)
    : log_category_(category) {}

void BaseHandler::Handle(const HandlerContext& context,
                         uint16_t msg_id,
                         const std::vector<uint8_t>& payload,
                         ResponseCallback callback) {
    LOG_DEBUG(log_category_, "Handle message msg_id={} client_id={}", msg_id, context.client_id);

    mir2::common::ErrorCode error_code = mir2::common::ErrorCode::kOk;
    if (!PreHandle(context, msg_id, payload, &error_code)) {
        LOG_WARN(log_category_, "PreHandle rejected msg_id={} client_id={} code={}",
                 msg_id, context.client_id, static_cast<uint16_t>(error_code));
        OnError(context, msg_id, error_code, std::move(callback));
        return;
    }

    try {
        DoHandle(context, msg_id, payload, std::move(callback));
    } catch (const std::exception& ex) {
        LOG_ERROR(log_category_, "Handler exception msg_id={} client_id={} error={}",
                  msg_id, context.client_id, ex.what());
        OnError(context, msg_id, mir2::common::ErrorCode::kUnknown, std::move(callback));
    } catch (...) {
        LOG_ERROR(log_category_, "Handler unknown exception msg_id={} client_id={}",
                  msg_id, context.client_id);
        OnError(context, msg_id, mir2::common::ErrorCode::kUnknown, std::move(callback));
    }
}

bool BaseHandler::PreHandle(const HandlerContext& /*context*/,
                            uint16_t /*msg_id*/,
                            const std::vector<uint8_t>& /*payload*/,
                            mir2::common::ErrorCode* error_code) {
    if (error_code) {
        *error_code = mir2::common::ErrorCode::kOk;
    }
    return true;
}

void BaseHandler::OnError(const HandlerContext& /*context*/,
                          uint16_t msg_id,
                          mir2::common::ErrorCode error_code,
                          ResponseCallback callback) {
    LOG_WARN(log_category_, "Handler error msg_id={} code={}", msg_id,
             static_cast<uint16_t>(error_code));
    (void)callback;
}

}  // namespace legend2::handlers
