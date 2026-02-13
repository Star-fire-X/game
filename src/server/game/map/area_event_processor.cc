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
  state.elapsed = 0.0f;
  state.tick_accumulator = 0.0f;
  effects_[effect.effect_id] = state;
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
    if (state.effect.tick_interval <= 0.0f) {
      ticks = state.tick_accumulator > 0.0f ? 1 : 0;
      state.tick_accumulator = 0.0f;
    } else {
      ticks = static_cast<int>(state.tick_accumulator / state.effect.tick_interval);
      if (ticks > 0) {
        state.tick_accumulator -= ticks * state.effect.tick_interval;
      }
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
    ecs::events::AreaEnterEvent event{
        player,
        trigger.trigger_id,
        trigger.effect_type
    };
    event_bus_->Publish(event);
  }
}

void AreaEventProcessor::DispatchExit(entt::entity player,
                                      const AreaTrigger& trigger) {
  if (trigger.on_exit) {
    trigger.on_exit(player);
  }

  if (event_bus_) {
    ecs::events::AreaExitEvent event{
        player,
        trigger.trigger_id,
        trigger.effect_type
    };
    event_bus_->Publish(event);
  }
}

void AreaEventProcessor::DispatchTick(entt::entity entity,
                                      const ContinuousAreaEffect& effect) const {
  if (!event_bus_) {
    return;
  }

  switch (effect.type) {
    case AreaEffectType::kDamage: {
      ecs::events::AreaDamageTickEvent event{
          entity,
          effect.effect_id,
          effect.damage_per_tick
      };
      event_bus_->Publish(event);
      break;
    }
    case AreaEffectType::kHeal: {
      ecs::events::AreaHealTickEvent event{
          entity,
          effect.effect_id,
          effect.damage_per_tick
      };
      event_bus_->Publish(event);
      break;
    }
    case AreaEffectType::kFire: {
      ecs::events::FireBurnTickEvent event{
          effect.effect_id,
          entity,
          effect.damage_per_tick,
          effect.duration
      };
      event_bus_->Publish(event);
      break;
    }
    case AreaEffectType::kMine: {
      ecs::events::MineEvent event{
          effect.effect_id,
          entity,
          effect.radius
      };
      event_bus_->Publish(event);
      break;
    }
    case AreaEffectType::kHolyCurtain: {
      ecs::events::HolyCurtainTickEvent event{
          effect.effect_id,
          effect.caster,
          entity,
          effect.damage_per_tick
      };
      event_bus_->Publish(event);
      break;
    }
    default:
      break;
  }
}

}  // namespace mir2::game::map
