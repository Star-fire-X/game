/**
 * @file boss_behavior.cc
 * @brief BOSS行为控制实现
 */

#include "game/entity/boss_behavior.h"

#include <algorithm>

#include "ecs/event_bus.h"
#include "ecs/events/boss_events.h"
#include "log/logger.h"

namespace mir2::game::entity {

BossBehavior::BossBehavior(uint32_t boss_id, const BossConfig& config,
                           ecs::EventBus* event_bus)
    : boss_id_(boss_id),
      config_(config),
      phase_(BossPhase::kNormal),
      current_hp_(config.max_hp),
      max_hp_(config.max_hp),
      base_attack_(config.attack),
      base_attack_speed_(config.attack_speed),
      attack_(config.attack),
      attack_speed_(config.attack_speed),
      event_bus_(event_bus) {
  if (config_.monster_id == 0) {
    config_.monster_id = boss_id_;
  }
  if (config_.name.empty()) {
    config_.name = "Boss_" + std::to_string(config_.monster_id);
  }
  max_hp_ = std::max(max_hp_, 1);
  current_hp_ = std::clamp(current_hp_, 0, max_hp_);
  base_attack_ = std::max(base_attack_, 0);
  base_attack_speed_ = std::max(base_attack_speed_, 0.0f);
  attack_ = base_attack_;
  attack_speed_ = base_attack_speed_;
  config_.rage_hp_threshold = std::max(config_.rage_hp_threshold, 0.0f);
  config_.summon_hp_threshold = std::max(config_.summon_hp_threshold, 0.0f);
  config_.special_skill_interval = std::max(config_.special_skill_interval, 0.0f);
  config_.rage_attack_multiplier = std::max(config_.rage_attack_multiplier, 0.0f);
  config_.rage_attack_speed_multiplier = std::max(config_.rage_attack_speed_multiplier, 0.0f);
}

void BossBehavior::Update(float delta_time) {
  if (dead_) {
    return;
  }
  if (config_.special_skill_interval <= 0.0f) {
    return;
  }

  special_skill_timer_ += delta_time;
  while (special_skill_timer_ >= config_.special_skill_interval) {
    special_skill_timer_ -= config_.special_skill_interval;
    CastSpecialSkill();
  }
}

void BossBehavior::OnHpChange(int32_t current_hp, int32_t max_hp) {
  max_hp_ = max_hp > 0 ? max_hp : 1;
  current_hp_ = std::clamp(current_hp, 0, max_hp_);

  if (current_hp_ <= 0) {
    if (!dead_) {
      dead_ = true;
      if (event_bus_) {
        ecs::events::BossDeadEvent event{boss_id_, config_.monster_id};
        event_bus_->Publish(event);
      }
      SYSLOG_INFO("Boss {} (monster {}) dead", boss_id_, config_.monster_id);
    }
    return;
  }

  float hp_percent = static_cast<float>(current_hp_) / static_cast<float>(max_hp_);
  if (!summon_triggered_ && hp_percent <= config_.summon_hp_threshold) {
    SummonMinions(config_.summon_count);
  }
  if (!rage_triggered_ && hp_percent <= config_.rage_hp_threshold) {
    EnterRageMode();
  }
}

void BossBehavior::EnterRageMode() {
  if (dead_ || rage_triggered_) {
    return;
  }

  rage_triggered_ = true;
  phase_ = BossPhase::kRage;
  attack_ = static_cast<int32_t>(base_attack_ * config_.rage_attack_multiplier);
  attack_speed_ = base_attack_speed_ * config_.rage_attack_speed_multiplier;

  if (event_bus_) {
    ecs::events::BossEnterRageEvent event{boss_id_, config_.monster_id, attack_,
                                          attack_speed_};
    event_bus_->Publish(event);
  }
  SYSLOG_INFO("Boss {} (monster {}) enters rage", boss_id_, config_.monster_id);
}

void BossBehavior::SummonMinions(uint32_t count) {
  if (dead_ || summon_triggered_) {
    return;
  }

  summon_triggered_ = true;
  phase_ = BossPhase::kSummon;

  if (event_bus_) {
    ecs::events::BossSummonEvent event{boss_id_, config_.monster_id, count,
                                       config_.summon_monster_ids};
    event_bus_->Publish(event);
  }
  SYSLOG_INFO("Boss {} (monster {}) summons {} minions", boss_id_,
              config_.monster_id, count);
}

void BossBehavior::CastSpecialSkill() {
  if (dead_) {
    return;
  }

  if (event_bus_) {
    ecs::events::BossSpecialSkillEvent event{boss_id_, config_.monster_id,
                                             config_.special_skill_damage};
    event_bus_->Publish(event);
  }
  SYSLOG_INFO("Boss {} (monster {}) casts special skill", boss_id_,
              config_.monster_id);
}

}  // namespace mir2::game::entity
