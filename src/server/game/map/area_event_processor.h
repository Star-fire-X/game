/**
 * @file area_event_processor.h
 * @brief 区域事件处理器定义
 */

#ifndef MIR2_GAME_MAP_AREA_EVENT_PROCESSOR_H_
#define MIR2_GAME_MAP_AREA_EVENT_PROCESSOR_H_

#include <entt/entt.hpp>

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "ecs/events/area_events.h"
#include "game/map/area_trigger.h"

namespace mir2::ecs {
class EventBus;
}  // namespace mir2::ecs

namespace mir2::game::map {

class AOIManager;

/**
 * @brief 区域事件处理器
 */
class AreaEventProcessor {
 public:
  AreaEventProcessor() = default;
  explicit AreaEventProcessor(AOIManager* aoi_manager,
                              ecs::EventBus* event_bus = nullptr);

  /**
   * @brief 设置 AOI 管理器
   */
  void SetAOIManager(AOIManager* aoi_manager) { aoi_manager_ = aoi_manager; }

  /**
   * @brief 设置事件总线
   */
  void SetEventBus(ecs::EventBus* event_bus) { event_bus_ = event_bus; }

  /**
   * @brief 设置是否延后事件分发
   *
   * 当开启后，事件会先入队，由调用方在安全时机调用 FlushPendingEvents()。
   */
  void SetDeferredDispatch(bool defer) { defer_dispatch_ = defer; }

  /**
   * @brief 添加触发器
   */
  void AddTrigger(const AreaTrigger& trigger);

  /**
   * @brief 移除触发器
   */
  void RemoveTrigger(uint32_t trigger_id);

  /**
   * @brief 添加持续效果
   */
  void AddContinuousEffect(const ContinuousAreaEffect& effect);

  /**
   * @brief 移除持续效果
   */
  void RemoveContinuousEffect(uint32_t effect_id);

  /**
   * @brief 更新持续效果
   */
  void Update(float delta_time, entt::registry& registry);

  /**
   * @brief 检测玩家进入/离开触发区域
   */
  void CheckPlayerEnterExit(entt::entity player, int32_t old_x, int32_t old_y,
                            int32_t new_x, int32_t new_y);

  /**
   * @brief 刷新并发布延后事件
   */
  void FlushPendingEvents();

 private:
  using PendingEvent = std::variant<
      ecs::events::AreaEnterEvent,
      ecs::events::AreaExitEvent,
      ecs::events::AreaDamageTickEvent,
      ecs::events::AreaHealTickEvent,
      ecs::events::FireBurnTickEvent,
      ecs::events::MineEvent,
      ecs::events::HolyCurtainTickEvent>;

  struct ActiveEffect {
    ContinuousAreaEffect effect;
    float elapsed = 0.0f;
    float tick_accumulator = 0.0f;
    std::unordered_set<uint64_t> triggered_entities;
  };

  static bool IsInside(int32_t x, int32_t y, int32_t center_x, int32_t center_y,
                       int32_t radius);
  bool GetEntityPosition(entt::registry& registry, entt::entity entity,
                         int32_t& out_x, int32_t& out_y) const;
  std::vector<entt::entity> CollectCandidates(const ContinuousAreaEffect& effect,
                                              entt::registry& registry) const;
  void DispatchEnter(entt::entity player, const AreaTrigger& trigger);
  void DispatchExit(entt::entity player, const AreaTrigger& trigger);
  void DispatchTick(entt::entity entity, const ContinuousAreaEffect& effect);
  void EnqueueOrPublish(const PendingEvent& event);

  std::unordered_map<uint32_t, AreaTrigger> triggers_;
  std::unordered_map<uint32_t, ActiveEffect> effects_;
  std::vector<PendingEvent> pending_events_;
  std::mutex pending_events_mutex_;
  AOIManager* aoi_manager_ = nullptr;
  ecs::EventBus* event_bus_ = nullptr;
  bool defer_dispatch_ = false;
};

}  // namespace mir2::game::map

#endif  // MIR2_GAME_MAP_AREA_EVENT_PROCESSOR_H_
