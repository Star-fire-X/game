/**
 * @file area_event_processor.cc
 * @brief 区域事件处理器实现
 */

#include "game/map/area_event_processor.h"

#include "ecs/components/character_components.h"
#include "ecs/event_bus.h"
#include "ecs/events/area_events.h"
#include "game/map/aoi_manager.h"

namespace mir2::game::map {
namespace {
constexpr float kMinTickInterval = 0.05f;
}  // namespace

AreaEventProcessor::AreaEventProcessor(AOIManager* aoi_manager,
                                       ecs::EventBus* event_bus)
    : aoi_manager_(aoi_manager), event_bus_(event_bus) {}

void AreaEventProcessor::AddTrigger(const AreaTrigger& trigger) {
  triggers_[trigger.trigger_id] = trigger;
}

void AreaEventProcessor::RemoveTrigger(uint32_t trigger_id) {
  triggers_.erase(trigger_id);
}

void AreaEventProcessor::AddContinuousEffect(const ContinuousAreaEffect& effect) {
  ActiveEffect state;
  state.effect = effect;
  if (state.effect.tick_interval < kMinTickInterval) {
    state.effect.tick_interval = kMinTickInterval;
  }
  state.elapsed = 0.0f;
  state.tick_accumulator = 0.0f;
  effects_[effect.effect_id] = state;
}

void AreaEventProcessor::RemoveContinuousEffect(uint32_t effect_id) {
  effects_.erase(effect_id);
}

void AreaEventProcessor::Update(float delta_time, entt::registry& registry) {
  if (effects_.empty()) {
    return;
  }

  for (auto it = effects_.begin(); it != effects_.end();) {
    ActiveEffect& state = it->second;
    state.elapsed += delta_time;
    state.tick_accumulator += delta_time;

    int ticks = 0;
    ticks = static_cast<int>(state.tick_accumulator / state.effect.tick_interval);
    if (ticks > 0) {
      state.tick_accumulator -= ticks * state.effect.tick_interval;
    }

    if (ticks > 0) {
      auto candidates = CollectCandidates(state.effect, registry);
      for (entt::entity entity : candidates) {
        if (!registry.valid(entity)) {
          continue;
        }
        if (!registry.any_of<ecs::CharacterStateComponent>(entity)) {
          continue;
        }
        int32_t x = 0;
        int32_t y = 0;
        if (!GetEntityPosition(registry, entity, x, y)) {
          continue;
        }
        if (!IsInside(x, y, state.effect.center_x, state.effect.center_y,
                      state.effect.radius)) {
          continue;
        }
        if (state.effect.type == AreaEffectType::kMine) {
          const uint64_t entity_id = static_cast<uint64_t>(entt::to_integral(entity));
          if (state.triggered_entities.find(entity_id) !=
              state.triggered_entities.end()) {
            continue;
          }
          DispatchTick(entity, state.effect);
          state.triggered_entities.insert(entity_id);
          continue;
        }
        for (int i = 0; i < ticks; ++i) {
          DispatchTick(entity, state.effect);
        }
      }
    }

    if (state.effect.duration > 0.0f && state.elapsed >= state.effect.duration) {
      if (state.effect.type == AreaEffectType::kTrap) {
        triggers_.erase(state.effect.effect_id);
      }
      it = effects_.erase(it);
    } else {
      ++it;
    }
  }
}

void AreaEventProcessor::CheckPlayerEnterExit(entt::entity player,
                                              int32_t old_x, int32_t old_y,
                                              int32_t new_x, int32_t new_y) {
  if (triggers_.empty()) {
    return;
  }

  const bool old_valid = old_x >= 0 && old_y >= 0;
  const bool new_valid = new_x >= 0 && new_y >= 0;

  for (const auto& [trigger_id, trigger] : triggers_) {
    (void)trigger_id;
    const bool old_inside = old_valid &&
                            IsInside(old_x, old_y, trigger.center_x,
                                     trigger.center_y, trigger.radius);
    const bool new_inside = new_valid &&
                            IsInside(new_x, new_y, trigger.center_x,
                                     trigger.center_y, trigger.radius);

    if (!old_inside && new_inside) {
      DispatchEnter(player, trigger);
    } else if (old_inside && !new_inside) {
      DispatchExit(player, trigger);
    }
  }
}

bool AreaEventProcessor::IsInside(int32_t x, int32_t y, int32_t center_x,
                                  int32_t center_y, int32_t radius) {
  const int64_t dx = static_cast<int64_t>(x) - center_x;
  const int64_t dy = static_cast<int64_t>(y) - center_y;
  const int64_t r = radius > 0 ? radius : 0;
  return dx * dx + dy * dy <= r * r;
}

bool AreaEventProcessor::GetEntityPosition(entt::registry& registry,
                                           entt::entity entity,
                                           int32_t& out_x,
                                           int32_t& out_y) const {
  if (aoi_manager_) {
    if (aoi_manager_->GetEntityPosition(static_cast<uint64_t>(entity), out_x, out_y)) {
      return true;
    }
  }

  if (auto* state = registry.try_get<ecs::CharacterStateComponent>(entity)) {
    out_x = state->position.x;
    out_y = state->position.y;
    return true;
  }

  return false;
}

std::vector<entt::entity> AreaEventProcessor::CollectCandidates(
    const ContinuousAreaEffect& effect,
    entt::registry& registry) const {
  std::vector<entt::entity> entities;
  if (aoi_manager_) {
    auto ids = aoi_manager_->GetEntitiesInView(effect.center_x, effect.center_y);
    if (!ids.empty()) {
      entities.reserve(ids.size());
      for (uint64_t id : ids) {
        entities.push_back(static_cast<entt::entity>(id));
      }
      return entities;
    }
  }

  auto view = registry.view<ecs::CharacterStateComponent>();
  for (auto entity : view) {
    entities.push_back(entity);
  }
  return entities;
}

void AreaEventProcessor::DispatchEnter(entt::entity player,
                                       const AreaTrigger& trigger) {
  if (trigger.on_enter) {
    trigger.on_enter(player);
  }

  if (event_bus_) {
    EnqueueOrPublish(ecs::events::AreaEnterEvent{
        player,
        trigger.trigger_id,
        trigger.effect_type
    });
  }
}

void AreaEventProcessor::DispatchExit(entt::entity player,
                                      const AreaTrigger& trigger) {
  if (trigger.on_exit) {
    trigger.on_exit(player);
  }

  if (event_bus_) {
    EnqueueOrPublish(ecs::events::AreaExitEvent{
        player,
        trigger.trigger_id,
        trigger.effect_type
    });
  }
}

void AreaEventProcessor::DispatchTick(entt::entity entity,
                                      const ContinuousAreaEffect& effect) {
  if (!event_bus_) {
    return;
  }

  switch (effect.type) {
    case AreaEffectType::kDamage: {
      EnqueueOrPublish(ecs::events::AreaDamageTickEvent{
          entity,
          effect.effect_id,
          effect.damage_per_tick
      });
      break;
    }
    case AreaEffectType::kHeal: {
      EnqueueOrPublish(ecs::events::AreaHealTickEvent{
          entity,
          effect.effect_id,
          effect.damage_per_tick
      });
      break;
    }
    case AreaEffectType::kFire: {
      EnqueueOrPublish(ecs::events::FireBurnTickEvent{
          effect.effect_id,
          entity,
          effect.damage_per_tick,
          effect.duration
      });
      break;
    }
    case AreaEffectType::kMine: {
      EnqueueOrPublish(ecs::events::MineEvent{
          effect.effect_id,
          entity,
          effect.radius
      });
      break;
    }
    case AreaEffectType::kHolyCurtain: {
      EnqueueOrPublish(ecs::events::HolyCurtainTickEvent{
          effect.effect_id,
          effect.caster,
          entity,
          effect.damage_per_tick
      });
      break;
    }
    default:
      break;
  }
}

void AreaEventProcessor::EnqueueOrPublish(const PendingEvent& event) {
  if (!event_bus_) {
    return;
  }

  if (!defer_dispatch_) {
    std::visit([this](const auto& value) { event_bus_->Publish(value); }, event);
    return;
  }

  std::lock_guard<std::mutex> lock(pending_events_mutex_);
  pending_events_.push_back(event);
}

void AreaEventProcessor::FlushPendingEvents() {
  if (!event_bus_) {
    std::lock_guard<std::mutex> lock(pending_events_mutex_);
    pending_events_.clear();
    return;
  }

  std::vector<PendingEvent> pending;
  {
    std::lock_guard<std::mutex> lock(pending_events_mutex_);
    if (pending_events_.empty()) {
      return;
    }
    pending.swap(pending_events_);
  }

  for (const auto& event : pending) {
    std::visit([this](const auto& value) { event_bus_->Publish(value); }, event);
  }
}

}  // namespace mir2::game::map
