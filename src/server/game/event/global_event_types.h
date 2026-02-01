/**
 * @file global_event_types.h
 * @brief 全局事件类型与配置定义
 */

#ifndef LEGEND2_SERVER_GAME_EVENT_GLOBAL_EVENT_TYPES_H
#define LEGEND2_SERVER_GAME_EVENT_GLOBAL_EVENT_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace legend2::game::event {

/**
 * @brief 全局事件类型
 */
enum class GlobalEventType : uint8_t {
    kDoubleExp = 0,
    kDoubleDrop = 1,
    kInvasion = 2,
    kFestival = 3
};

/**
 * @brief 全局事件配置
 */
struct GlobalEventConfig {
    uint32_t event_id = 0;
    std::string event_name;
    GlobalEventType type = GlobalEventType::kDoubleExp;
    int32_t duration_seconds = 0;
    float rate = 1.0f;
    std::vector<uint32_t> monster_ids;
};

/**
 * @brief 全局事件开始事件
 */
struct GlobalEventStartedEvent {
    GlobalEventConfig config;
};

/**
 * @brief 全局事件结束事件
 */
struct GlobalEventEndedEvent {
    GlobalEventConfig config;
};

}  // namespace legend2::game::event

#endif  // LEGEND2_SERVER_GAME_EVENT_GLOBAL_EVENT_TYPES_H
