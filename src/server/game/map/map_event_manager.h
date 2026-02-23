/**
 * @file map_event_manager.h
 * @brief 地图事件管理器定义
 */

#ifndef MIR2_GAME_MAP_MAP_EVENT_MANAGER_H_
#define MIR2_GAME_MAP_MAP_EVENT_MANAGER_H_

#include <entt/entt.hpp>

#include <cstdint>
#include <vector>

#include "game/map/area_trigger.h"

namespace mir2::game::map {

class MapInstance;

/**
 * @brief 地图事件管理器
 */
class MapEventManager {
 public:
  // Experimental module: currently maintained as an isolated map-event helper.
  static constexpr bool kExperimental = true;

  struct EventRecord {
    uint32_t event_id = 0;
    uint32_t trigger_id = 0;
    ContinuousAreaEffect effect;
    float elapsed_time = 0.0f;
    bool registered = false;  // 是否已注册到 MapInstance
    bool closed = false;  // 标记为已关闭
  };

  uint32_t AddFireEvent(int32_t x, int32_t y, float duration, int32_t damage);
  uint32_t AddHolyCurtainEvent(int32_t x, int32_t y, float duration,
                               entt::entity caster, int32_t shield);
  uint32_t AddMineEvent(int32_t x, int32_t y, int32_t radius);

  void RemoveEvent(uint32_t event_id, MapInstance* map = nullptr);
  void Update(float delta_time, entt::registry& registry, MapInstance* map);

  void CleanupClosedEvents();  // 清理已关闭>5分钟的事件

 private:
  std::vector<EventRecord> active_events_;
  std::vector<EventRecord> closed_list_;
  uint32_t next_event_id_ = 1;
};

}  // namespace mir2::game::map

#endif  // MIR2_GAME_MAP_MAP_EVENT_MANAGER_H_
