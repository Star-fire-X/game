/**
 * @file event_types.h
 * @brief 定时事件触发类型定义
 */

#ifndef MIR2_SERVER_GAME_EVENT_EVENT_TYPES_H_
#define MIR2_SERVER_GAME_EVENT_EVENT_TYPES_H_

#include <cstdint>

namespace legend2::game::event {

/**
 * @brief 事件触发类型
 */
enum class EventTriggerType : uint8_t {
    kDaily = 0,
    kWeekly = 1,
    kMonthly = 2,
    kInterval = 3
};

}  // namespace legend2::game::event

#endif  // MIR2_SERVER_GAME_EVENT_EVENT_TYPES_H_
