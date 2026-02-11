/**
 * @file map_event_manager.cc
 * @brief 地图事件管理器实现
 */

#include "game/map/map_event_manager.h"

#include <algorithm>

#include "game/map/map_instance.h"

namespace mir2::game::map {
namespace {
static_assert(MapEventManager::kExperimental,
              "MapEventManager is expected to stay experimental in current phase.");

constexpr float kDefaultTickInterval = 1.0f;
constexpr float kClosedRetentionSeconds = 5.0f * 60.0f;
constexpr int32_t kDefaultFireRadius = 0;
constexpr int32_t kDefaultHolyCurtainRadius = 0;

MapEventManager::EventRecord MakeBaseRecord(uint32_t event_id,
                                            AreaEffectType type,
                                            int32_t x,
                                            int32_t y) {
  MapEventManager::EventRecord record;
  record.event_id = event_id;
  record.trigger_id = event_id;
  record.effect.effect_id = event_id;
  record.effect.type = type;
  record.effect.center_x = x;
  record.effect.center_y = y;
  record.effect.tick_interval = kDefaultTickInterval;
  return record;
}
}  // namespace

uint32_t MapEventManager::AddFireEvent(int32_t x, int32_t y, float duration,
                                       int32_t damage) {
  const uint32_t event_id = next_event_id_++;
  EventRecord record = MakeBaseRecord(event_id, AreaEffectType::kFire, x, y);
  record.effect.radius = kDefaultFireRadius;
  record.effect.damage_per_tick = damage;
  record.effect.duration = duration;
  active_events_.push_back(record);
  return event_id;
}

uint32_t MapEventManager::AddHolyCurtainEvent(int32_t x, int32_t y,
                                              float duration,
                                              entt::entity caster,
                                              int32_t shield) {
  const uint32_t event_id = next_event_id_++;
  EventRecord record =
      MakeBaseRecord(event_id, AreaEffectType::kHolyCurtain, x, y);
  record.effect.caster = caster;
  record.effect.radius = kDefaultHolyCurtainRadius;
  record.effect.damage_per_tick = shield;
  record.effect.duration = duration;
  active_events_.push_back(record);
  return event_id;
}

uint32_t MapEventManager::AddMineEvent(int32_t x, int32_t y, int32_t radius) {
  const uint32_t event_id = next_event_id_++;
  EventRecord record = MakeBaseRecord(event_id, AreaEffectType::kMine, x, y);
  record.effect.radius = radius;
  record.effect.duration = 0.0f;
  active_events_.push_back(record);
  return event_id;
}

void MapEventManager::RemoveEvent(uint32_t event_id) {
  auto it = std::find_if(active_events_.begin(), active_events_.end(),
                         [event_id](const EventRecord& record) {
                           return record.event_id == event_id;
                         });
  if (it == active_events_.end()) {
    return;
  }

  EventRecord closed = *it;
  active_events_.erase(it);
  closed.closed = true;
  closed.registered = false;
  closed.elapsed_time = 0.0f;
  closed_list_.push_back(closed);
}

void MapEventManager::Update(float delta_time, entt::registry& registry,
                             MapInstance* map) {
  if (delta_time < 0.0f) {
    delta_time = 0.0f;
  }

  if (map) {
    for (auto& record : active_events_) {
      if (!record.closed && !record.registered) {
        map->AddContinuousAreaEffect(record.effect);
        record.registered = true;
      }
    }
    map->UpdateAreaEvents(delta_time, registry);
  } else {
    static_cast<void>(registry);
  }

  for (auto it = active_events_.begin(); it != active_events_.end();) {
    EventRecord& record = *it;
    record.elapsed_time += delta_time;

    if (record.effect.duration > 0.0f &&
        record.elapsed_time >= record.effect.duration) {
      EventRecord closed = record;
      closed.closed = true;
      closed.registered = false;
      closed.elapsed_time = 0.0f;
      closed_list_.push_back(closed);
      it = active_events_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto& record : closed_list_) {
    record.elapsed_time += delta_time;
  }

  CleanupClosedEvents();
}

void MapEventManager::CleanupClosedEvents() {
  closed_list_.erase(
      std::remove_if(closed_list_.begin(), closed_list_.end(),
                     [](const EventRecord& record) {
                       return record.elapsed_time > kClosedRetentionSeconds;
                     }),
      closed_list_.end());
}

}  // namespace mir2::game::map
