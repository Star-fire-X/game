/**
 * @file event_handler.h
 * @brief 定时事件处理器基类
 */

#ifndef MIR2_SERVER_GAME_EVENT_EVENT_HANDLER_H_
#define MIR2_SERVER_GAME_EVENT_EVENT_HANDLER_H_

#include <cstdint>

namespace legend2::game::event {

/**
 * @brief 定时事件处理器接口
 */
class EventHandler {
 public:
    virtual ~EventHandler();

    /**
     * @brief 事件触发回调
     * @param event_id 事件ID
     */
    virtual void OnEventTrigger(uint32_t event_id) = 0;
};

}  // namespace legend2::game::event

#endif  // MIR2_SERVER_GAME_EVENT_EVENT_HANDLER_H_
