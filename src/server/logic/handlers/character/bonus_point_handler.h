/**
 * @file bonus_point_handler.h
 * @brief 奖励点数分配请求处理器
 */

#ifndef MIR2_SERVER_LOGIC_HANDLERS_BONUS_POINT_HANDLER_H_
#define MIR2_SERVER_LOGIC_HANDLERS_BONUS_POINT_HANDLER_H_

#include "logic/handler_context.h"
#include "logic/task.h"

namespace mir2::logic {

Task<void> HandleBonusPointReq(const HandlerContext& context,
                               const uint8_t* payload,
                               size_t payload_size);

}  // namespace mir2::logic

#endif  // MIR2_SERVER_LOGIC_HANDLERS_BONUS_POINT_HANDLER_H_
