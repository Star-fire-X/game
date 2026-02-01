/**
 * @file timed_event_scheduler.h
 * @brief 定时事件调度器定义
 */

#ifndef LEGEND2_SERVER_GAME_EVENT_TIMED_EVENT_SCHEDULER_H
#define LEGEND2_SERVER_GAME_EVENT_TIMED_EVENT_SCHEDULER_H

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/singleton.h"
#include "game/event/event_types.h"

namespace legend2::game::event {

class EventHandler;

/**
 * @brief 定时事件配置
 */
struct TimedEventConfig {
    uint32_t event_id = 0;
    std::string event_name;
    EventTriggerType trigger_type = EventTriggerType::kDaily;
    uint32_t hour = 0;          ///< 日触发时间（小时）
    uint32_t minute = 0;        ///< 日触发时间（分钟）
    uint32_t day_of_week = 0;   ///< 周触发日（0=周日，6=周六）
    uint32_t day_of_month = 1;  ///< 月触发日（1-31）
    float interval_seconds = 0.0f;  ///< 间隔触发（秒）
};

/**
 * @brief 定时事件调度器
 *
 * 单例类，负责按配置触发定时事件。
 */
class TimedEventScheduler : public mir2::core::Singleton<TimedEventScheduler> {
  friend class mir2::core::Singleton<TimedEventScheduler>;

 public:
    /**
     * @brief 注册定时事件
     */
    void RegisterEvent(const TimedEventConfig& config);

    /**
     * @brief 每帧更新，检查是否需要触发事件
     */
    void Update(float delta_time);

    /**
     * @brief 手动触发事件
     */
    void TriggerEvent(uint32_t event_id);

    /**
     * @brief 获取所有已注册事件
     */
    std::vector<TimedEventConfig> GetAllEvents() const;

    /**
     * @brief 注册事件处理器
     */
    void RegisterHandler(EventHandler* handler);

    /**
     * @brief 取消注册事件处理器
     */
    void UnregisterHandler(EventHandler* handler);

 private:
    TimedEventScheduler() = default;
    ~TimedEventScheduler() = default;

    struct TimedEventState {
        TimedEventConfig config;
        float interval_accumulator = 0.0f;
        int last_trigger_year = 0;
        int last_trigger_month = 0;
        int last_trigger_day = 0;
    };

    void DispatchEvent(uint32_t event_id);
    static bool IsTimeValid(const TimedEventConfig& config);

    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, TimedEventState> events_;
    std::vector<EventHandler*> handlers_;
};

}  // namespace legend2::game::event

#endif  // LEGEND2_SERVER_GAME_EVENT_TIMED_EVENT_SCHEDULER_H
