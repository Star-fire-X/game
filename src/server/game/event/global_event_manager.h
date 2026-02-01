/**
 * @file global_event_manager.h
 * @brief 全局事件管理器定义
 */

#ifndef LEGEND2_SERVER_GAME_EVENT_GLOBAL_EVENT_MANAGER_H
#define LEGEND2_SERVER_GAME_EVENT_GLOBAL_EVENT_MANAGER_H

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "core/singleton.h"
#include "game/event/global_event_types.h"

namespace mir2::ecs {
class EventBus;
}  // namespace mir2::ecs

namespace legend2::game::event {

/**
 * @brief 全局事件管理器
 *
 * 单例类，负责管理全局事件的生命周期。
 */
class GlobalEventManager : public mir2::core::Singleton<GlobalEventManager> {
  friend class mir2::core::Singleton<GlobalEventManager>;

 public:
    /**
     * @brief 启动事件
     * @param event_id 事件ID
     * @param type 事件类型
     * @param duration_sec 持续时间（秒，<=0 表示手动结束）
     * @param rate 倍率（用于双倍经验/掉落）
     */
    void StartEvent(uint32_t event_id, GlobalEventType type, int32_t duration_sec,
                    float rate = 1.0f);

    /**
     * @brief 结束事件
     */
    void StopEvent(uint32_t event_id);

    /**
     * @brief 检查事件是否激活
     */
    bool IsEventActive(uint32_t event_id) const;

    /**
     * @brief 应用经验倍率
     */
    float ApplyExpRate(float base_exp) const;

    /**
     * @brief 应用掉落率倍率
     */
    float ApplyDropRate(float base_rate) const;

    /**
     * @brief 更新事件状态
     * @param delta_time 帧时间（秒）
     */
    void Update(float delta_time);

    /**
     * @brief 获取当前激活事件
     */
    std::vector<GlobalEventConfig> GetActiveEvents() const;

    /**
     * @brief 设置事件总线
     */
    void SetEventBus(mir2::ecs::EventBus* event_bus);

 private:
    GlobalEventManager() = default;
    ~GlobalEventManager() = default;

    struct ActiveEventState {
        GlobalEventConfig config;
        float remaining_seconds = 0.0f;
    };

    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, ActiveEventState> active_events_;
    mir2::ecs::EventBus* event_bus_ = nullptr;
};

}  // namespace legend2::game::event

#endif  // LEGEND2_SERVER_GAME_EVENT_GLOBAL_EVENT_MANAGER_H
