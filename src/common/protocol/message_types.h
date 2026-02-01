// =============================================================================
// 网络消息类型定义
//
// 功能说明:
//   - 统一使用 mir2::common::MsgId
// =============================================================================

#ifndef LEGEND2_COMMON_PROTOCOL_MESSAGE_TYPES_H
#define LEGEND2_COMMON_PROTOCOL_MESSAGE_TYPES_H

#include "common/enums.h"

namespace mir2::common {

/// 网络消息类型
using MessageType = MsgId;

} // namespace mir2::common

#endif // LEGEND2_COMMON_PROTOCOL_MESSAGE_TYPES_H
