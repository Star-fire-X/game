#include "game/item/item_effect_processor.h"

#include <algorithm>
#include <array>
#include <random>
#include <string>
#include <utility>

#include "common/item_constants.h"
#include "common/types/constants.h"
#include "core/utils.h"
#include "data/item_template.h"
#include "ecs/components/character_components.h"
#include "ecs/systems/effect_system.h"
#include "ecs/systems/level_up_system.h"
#include "ecs/components/equipment_component.h"
#include "ecs/components/guild_component.h"
#include "ecs/components/item_component.h"
#include "ecs/components/skill_component.h"
#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/inventory_events.h"
#include "ecs/events/map_events.h"
#include "ecs/systems/combat_system.h"
#include "ecs/systems/inventory_system.h"
#include "game/guild/guild_manager.h"
#include "game/map/scroll_teleport.h"
#include "game/map/map_instance.h"

namespace mir2::game::item {
namespace {

const map::MapInstance* ResolveMapInstance(entt::registry& registry) {
  if (auto* map_ptr = registry.ctx().find<map::MapInstance*>()) {
    return *map_ptr;
  }
  if (auto* map_ptr = registry.ctx().find<const map::MapInstance*>()) {
    return *map_ptr;
  }
  return nullptr;
}

bool HasLearnedSkill(entt::registry& registry,
                     entt::entity character,
                     uint32_t skill_id) {
  if (skill_id == 0 || character == entt::null || !registry.valid(character)) {
    return false;
  }

  if (const auto* skill_list = registry.try_get<ecs::SkillListComponent>(character)) {
    if (skill_list->has_skill(skill_id)) {
      return true;
    }
  }

  auto view = registry.view<ecs::InventoryOwnerComponent, ecs::SkillComponent>();
  for (auto entity : view) {
    const auto& owner = view.get<ecs::InventoryOwnerComponent>(entity);
    if (owner.owner != character) {
      continue;
    }
    const auto& skill = view.get<ecs::SkillComponent>(entity);
    if (skill.skill_id == skill_id) {
      return true;
    }
  }

  return false;
}

std::mt19937& ItemEffectRng() {
  static thread_local std::mt19937 rng(std::random_device{}());
  return rng;
}

constexpr int32_t kDefaultCastleMapId = 3;
constexpr int32_t kDefaultCastleX = 330;
constexpr int32_t kDefaultCastleY = 330;
constexpr int32_t kRandomTeleportAttempts = 30;
constexpr int64_t kSiegeRandomScrollCooldownMs = 10000;

bool RollOneIn(int chance) {
  if (chance <= 1) {
    return chance == 1;
  }
  std::uniform_int_distribution<int> dist(1, chance);
  return dist(ItemEffectRng()) == 1;
}

std::optional<std::pair<int32_t, int32_t>> FindRandomWalkablePosition(
    const map::MapInstance& map_instance) {
  const int32_t width = map_instance.GetMapWidth();
  const int32_t height = map_instance.GetMapHeight();
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }

  std::uniform_int_distribution<int32_t> dist_x(0, width - 1);
  std::uniform_int_distribution<int32_t> dist_y(0, height - 1);

  auto& rng = ItemEffectRng();
  for (int32_t attempt = 0; attempt < kRandomTeleportAttempts; ++attempt) {
    const int32_t x = dist_x(rng);
    const int32_t y = dist_y(rng);
    if (map_instance.IsWalkable(x, y)) {
      return std::make_pair(x, y);
    }
  }

  for (int32_t y = 0; y < height; ++y) {
    for (int32_t x = 0; x < width; ++x) {
      if (map_instance.IsWalkable(x, y)) {
        return std::make_pair(x, y);
      }
    }
  }

  return std::nullopt;
}

int CountFreeInventorySlots(entt::registry& registry, entt::entity character) {
  constexpr int kMaxSlots = mir2::common::constants::MAX_INVENTORY_SIZE;
  std::array<bool, static_cast<std::size_t>(kMaxSlots)> occupied{};
  auto view = registry.view<ecs::InventoryOwnerComponent, ecs::ItemComponent>();
  for (auto entity : view) {
    const auto& owner = view.get<ecs::InventoryOwnerComponent>(entity);
    if (owner.owner != character) {
      continue;
    }
    if (owner.slot_index >= 0 && owner.slot_index < kMaxSlots) {
      occupied[static_cast<std::size_t>(owner.slot_index)] = true;
    }
  }

  int free_slots = 0;
  for (bool used : occupied) {
    if (!used) {
      ++free_slots;
    }
  }
  return free_slots;
}

entt::entity FindItemEntity(entt::registry& registry,
                            const ecs::ItemComponent& item) {
  auto view = registry.view<ecs::ItemComponent>();
  for (auto entity : view) {
    if (&view.get<ecs::ItemComponent>(entity) == &item) {
      return entity;
    }
  }
  return entt::null;
}

}  // namespace

ItemEffectProcessor::ItemEffectProcessor(entt::registry& registry,
                                         ecs::EventBus& event_bus)
    : registry_(registry),
      event_bus_(event_bus) {}

bool ItemEffectProcessor::ProcessItemUse(entt::entity character, entt::entity item) {
  if (!registry_.valid(character) || !registry_.valid(item)) {
    return false;
  }

  auto* item_component = registry_.try_get<ecs::ItemComponent>(item);
  if (!item_component) {
    return false;
  }

  switch (static_cast<uint8_t>(item_component->std_mode)) {
    case common::item_std_mode::kDrug:
      return ProcessDrug(character, *item_component);
    case common::item_std_mode::kScroll:
      return ProcessScroll(character, *item_component);
    case common::item_std_mode::kSkillBook:
      return ProcessSkillBook(character, *item_component);
    case common::item_std_mode::kBundledDrug:
      return ProcessBundledDrug(character, *item_component);
    default:
      return false;
  }
}

bool ItemEffectProcessor::ProcessDrug(entt::entity character,
                                      const ecs::ItemComponent& item) {
  auto* attributes = registry_.try_get<ecs::CharacterAttributesComponent>(character);
  if (!attributes) {
    return false;
  }

  const auto* tmpl = data::ItemTemplateManager::Instance().GetTemplate(item.item_id);
  if (!tmpl) {
    return false;
  }

  if (const map::MapInstance* map = ResolveMapInstance(registry_)) {
    if (map->GetAttributes().no_drug) {
      return false;
    }
  }

  const int hp_recover = static_cast<int>(tmpl->ac);
  const int mp_recover = static_cast<int>(tmpl->mac);

  switch (tmpl->shape) {
    case common::drug_shape::kNormal: {
      const int old_inc_health = attributes->inc_health;
      const int old_inc_spell = attributes->inc_spell;
      const int max_inc = common::recovery::kMaxIncHealthSpell;

      if (hp_recover > 0) {
        attributes->inc_health = std::min(max_inc, attributes->inc_health + hp_recover);
      }
      if (mp_recover > 0) {
        attributes->inc_spell = std::min(max_inc, attributes->inc_spell + mp_recover);
      }

      if (attributes->inc_health != old_inc_health ||
          attributes->inc_spell != old_inc_spell) {
        ecs::dirty_tracker::mark_attributes_dirty(registry_, character);
      }
      return true;
    }
    case common::drug_shape::kFastFill: {
      ecs::CombatSystem::Heal(registry_, character, hp_recover);
      ecs::CombatSystem::RestoreMP(registry_, character, mp_recover);
      return true;
    }
    case common::drug_shape::kFreeCurse: {
      if (!attributes->next_free_curse_item) {
        attributes->next_free_curse_item = true;
        ecs::dirty_tracker::mark_attributes_dirty(registry_, character);
      }
      return true;
    }
    default:
      return false;
  }
}

bool ItemEffectProcessor::ProcessScroll(entt::entity character,
                                        const ecs::ItemComponent& item) {
  auto* state = registry_.try_get<ecs::CharacterStateComponent>(character);
  if (!state) {
    return false;
  }

  const auto* tmpl = data::ItemTemplateManager::Instance().GetTemplate(item.item_id);
  if (!tmpl || tmpl->std_mode != common::item_std_mode::kScroll) {
    return false;
  }

  switch (tmpl->shape) {
    case common::scroll_shape::kDungeonTeleport:
      return ProcessRandomScroll(character);
    case common::scroll_shape::kHomeScroll:
      return ProcessHomeScroll(character);
    case common::scroll_shape::kCastleScroll:
      return ProcessCastleScroll(character);
    case common::scroll_shape::kBlessOil:
      return ProcessBlessOil(character);
    case common::scroll_shape::kRepairOil:
      return ProcessRepairOil(character, false);
    case common::scroll_shape::kPerfectRepairOil:
      return ProcessRepairOil(character, true);
    case common::scroll_shape::kLottery:
      return ProcessLottery(character, item);
    case common::scroll_shape::kAbilityDrug: {
      active_scroll_item_id_ = item.item_id;
      const bool handled = ProcessAbilityDrug(character);
      active_scroll_item_id_ = 0;
      return handled;
    }
    case common::scroll_shape::kExpDrug: {
      active_scroll_item_id_ = item.item_id;
      const bool handled = ProcessExpDrug(character);
      active_scroll_item_id_ = 0;
      return handled;
    }
    default:
      return false;
  }
}

bool ItemEffectProcessor::ProcessAbilityDrug(entt::entity character) {
  auto* attributes = registry_.try_get<ecs::CharacterAttributesComponent>(character);
  if (!attributes) {
    return false;
  }

  if (active_scroll_item_id_ == 0) {
    return false;
  }

  const auto* tmpl =
      data::ItemTemplateManager::Instance().GetTemplate(active_scroll_item_id_);
  if (!tmpl || tmpl->std_mode != common::item_std_mode::kScroll ||
      tmpl->shape != common::scroll_shape::kAbilityDrug) {
    return false;
  }

  const auto low_byte = [](uint16_t value) {
    return static_cast<int>(value & 0xFF);
  };
  const auto high_byte = [](uint16_t value) {
    return static_cast<int>((value >> 8) & 0xFF);
  };

  const int dc_bonus = low_byte(tmpl->dc);
  const int mc_bonus = low_byte(tmpl->mc);
  const int sc_bonus = low_byte(tmpl->sc);
  const int speed_bonus = high_byte(tmpl->ac);
  const int hp_bonus = low_byte(tmpl->ac);
  const int mp_bonus = low_byte(tmpl->mac);
  const int duration_seconds = high_byte(tmpl->mac);

  if (duration_seconds <= 0) {
    return false;
  }

  if (dc_bonus == 0 && mc_bonus == 0 && sc_bonus == 0 &&
      speed_bonus == 0 && hp_bonus == 0 && mp_bonus == 0) {
    return false;
  }

  const int64_t now_ms = mir2::core::GetCurrentTimestampMs();
  const int64_t duration_ms = static_cast<int64_t>(duration_seconds) * 1000;

  ecs::ActiveEffect effect;
  effect.skill_id = active_scroll_item_id_;
  effect.source_entity = static_cast<uint32_t>(character);
  effect.category = ecs::EffectCategory::STAT_BUFF;
  effect.value = dc_bonus;
  effect.magic_attack_bonus = mc_bonus;
  effect.sc_bonus = sc_bonus;
  effect.speed_bonus = speed_bonus;
  effect.max_hp_bonus = hp_bonus;
  effect.max_mp_bonus = mp_bonus;
  effect.start_time_ms = now_ms;
  effect.end_time_ms = now_ms + duration_ms;
  effect.last_tick_ms = now_ms;

  ecs::EffectSystem effect_system(registry_);
  effect_system.apply_effect(character, effect);
  return true;
}

bool ItemEffectProcessor::ProcessExpDrug(entt::entity character) {
  if (!registry_.valid(character)) {
    return false;
  }

  if (active_scroll_item_id_ == 0) {
    return false;
  }

  const auto* tmpl =
      data::ItemTemplateManager::Instance().GetTemplate(active_scroll_item_id_);
  if (!tmpl || tmpl->std_mode != common::item_std_mode::kScroll ||
      tmpl->shape != common::scroll_shape::kExpDrug) {
    return false;
  }

  const int exp_amount = static_cast<int>(tmpl->ac & 0xFF) * 100;
  if (exp_amount <= 0) {
    return false;
  }

  ecs::LevelUpSystem::GainExperience(registry_, character, exp_amount, &event_bus_);
  return true;
}

bool ItemEffectProcessor::ProcessSkillBook(entt::entity character,
                                           const ecs::ItemComponent& item) {
  auto* identity = registry_.try_get<ecs::CharacterIdentityComponent>(character);
  auto* attributes = registry_.try_get<ecs::CharacterAttributesComponent>(character);
  if (!identity || !attributes) {
    return false;
  }

  const auto* tmpl = data::ItemTemplateManager::Instance().GetTemplate(item.item_id);
  if (!tmpl || tmpl->std_mode != common::item_std_mode::kSkillBook) {
    return false;
  }

  uint32_t skill_id = item.item_id;
  if (tmpl->shape != 0) {
    skill_id = static_cast<uint32_t>(tmpl->shape);
  }
  if (skill_id == 0) {
    return false;
  }

  if (HasLearnedSkill(registry_, character, skill_id)) {
    return false;
  }

  if (tmpl->need_class != 99 &&
      static_cast<uint8_t>(identity->char_class) != tmpl->need_class) {
    return false;
  }

  if (attributes->level < tmpl->need_level) {
    return false;
  }

  return ecs::InventorySystem::LearnSkill(registry_, character, skill_id, 1,
                                          &event_bus_).has_value();
}

bool ItemEffectProcessor::ProcessBundledDrug(entt::entity character,
                                             const ecs::ItemComponent& item) {
  if (!registry_.valid(character)) {
    return false;
  }

  const auto* tmpl = data::ItemTemplateManager::Instance().GetTemplate(item.item_id);
  if (!tmpl || tmpl->std_mode != common::item_std_mode::kBundledDrug) {
    return false;
  }

  uint32_t target_item_id = 0;
  if (tmpl->shape != 0) {
    target_item_id = static_cast<uint32_t>(tmpl->shape);
  } else if (item.shape != 0) {
    target_item_id = static_cast<uint32_t>(item.shape);
  }
  if (target_item_id == 0) {
    return false;
  }
  const auto* target_tmpl =
      data::ItemTemplateManager::Instance().GetTemplate(target_item_id);
  if (!target_tmpl) {
    return false;
  }

  constexpr int kBundledDrugCount = 6;
  const int free_slots = CountFreeInventorySlots(registry_, character);
  const int required_slots = (item.count > 1) ? kBundledDrugCount
                                              : kBundledDrugCount - 1;
  if (free_slots < required_slots) {
    return false;
  }

  entt::entity item_entity = FindItemEntity(registry_, item);
  if (item_entity == entt::null) {
    return false;
  }
  auto* owner = registry_.try_get<ecs::InventoryOwnerComponent>(item_entity);
  if (!owner || owner->owner != character || owner->slot_index < 0) {
    return false;
  }

  if (item.count == 1) {
    for (int i = 0; i < kBundledDrugCount - 1; ++i) {
      if (!ecs::InventorySystem::AddItem(
              registry_, character, target_item_id, 1, &event_bus_)) {
        return false;
      }
    }

    auto* item_component = registry_.try_get<ecs::ItemComponent>(item_entity);
    if (!item_component) {
      return false;
    }
    item_component->item_id = target_item_id;
    // Keep one potion in this slot after the caller consumes one unit.
    item_component->count = 2;
    item_component->std_mode = target_tmpl->std_mode;
    item_component->shape = target_tmpl->shape;
    item_component->max_durability = target_tmpl->dura_max;
    item_component->durability = target_tmpl->dura_max;
    ecs::dirty_tracker::mark_items_dirty(registry_, character);
    return true;
  }

  for (int i = 0; i < kBundledDrugCount; ++i) {
    if (!ecs::InventorySystem::AddItem(
            registry_, character, target_item_id, 1, &event_bus_)) {
      return false;
    }
  }

  return true;
}

bool ItemEffectProcessor::ProcessRandomScroll(entt::entity character) {
  auto* state = registry_.try_get<ecs::CharacterStateComponent>(character);
  if (!state) {
    return false;
  }

  if (state->is_in_activity) {
    return false;
  }

  const map::MapInstance* map = ResolveMapInstance(registry_);
  if (!map) {
    return false;
  }

  if (!map->CanRandomMove()) {
    return false;
  }

  const map::MapAttributes attributes_snapshot = map->GetAttributes();
  const bool in_siege = attributes_snapshot.fight3_zone;
  const int64_t now_ms = mir2::core::GetCurrentTimestampMs();
  if (in_siege && state->last_random_scroll_time_ms > 0 &&
      now_ms - state->last_random_scroll_time_ms < kSiegeRandomScrollCooldownMs) {
    return false;
  }

  const auto command_opt = map::ScrollTeleport::UseDungeonScroll(
      character,
      std::to_string(map->GetMapId()),
      static_cast<uint64_t>(state->last_random_scroll_time_ms),
      in_siege,
      attributes_snapshot);
  std::optional<map::TeleportCommand> final_command = command_opt;
  if (!final_command) {
    const auto random_pos = FindRandomWalkablePosition(*map);
    if (!random_pos) {
      return false;
    }
    final_command = map::TeleportCommand{
        character, map->GetMapId(), random_pos->first, random_pos->second};
  }

  state->last_random_scroll_time_ms = now_ms;

  ecs::events::TeleportRequestEvent event;
  event.entity = final_command->entity;
  event.target_map_id = final_command->target_map_id;
  event.target_x = final_command->target_x;
  event.target_y = final_command->target_y;
  event_bus_.Publish(event);
  return true;
}

bool ItemEffectProcessor::ProcessHomeScroll(entt::entity character) {
  auto* state = registry_.try_get<ecs::CharacterStateComponent>(character);
  auto* attributes = registry_.try_get<ecs::CharacterAttributesComponent>(character);
  if (!state || !attributes) {
    return false;
  }

  if (state->is_in_activity) {
    return false;
  }

  const map::MapInstance* map = ResolveMapInstance(registry_);
  if (!map) {
    return false;
  }

  const map::MapAttributes attributes_snapshot = map->GetAttributes();
  const auto command_opt = map::ScrollTeleport::UseTownScroll(
      character,
      std::to_string(map->GetMapId()),
      attributes->pk_level,
      attributes_snapshot);
  if (!command_opt) {
    return false;
  }

  ecs::events::TeleportRequestEvent event;
  event.entity = command_opt->entity;
  event.target_map_id = command_opt->target_map_id;
  event.target_x = command_opt->target_x;
  event.target_y = command_opt->target_y;
  event_bus_.Publish(event);
  return true;
}

bool ItemEffectProcessor::ProcessCastleScroll(entt::entity character) {
  auto* state = registry_.try_get<ecs::CharacterStateComponent>(character);
  if (!state) {
    return false;
  }

  if (state->is_in_activity) {
    return false;
  }

  const map::MapInstance* map = ResolveMapInstance(registry_);
  if (!map) {
    return false;
  }

  if (map->IsFightZone()) {
    return false;
  }

  auto* guild_member = registry_.try_get<ecs::GuildMemberComponent>(character);
  if (!guild_member || guild_member->guild_id == 0) {
    return false;
  }

  auto* guild =
      game::guild::GuildManager::Instance().GetGuild(guild_member->guild_id, registry_);
  if (!guild || !guild->owns_castle) {
    return false;
  }

  ecs::events::TeleportRequestEvent event;
  event.entity = character;
  event.target_map_id = kDefaultCastleMapId;
  event.target_x = kDefaultCastleX;
  event.target_y = kDefaultCastleY;
  event_bus_.Publish(event);
  return true;
}

bool ItemEffectProcessor::ProcessLottery(entt::entity character,
                                         const ecs::ItemComponent& item) {
  if (!registry_.valid(character)) {
    return false;
  }

  ecs::events::LotteryDrawEvent event;
  event.character = character;
  event.item_id = item.item_id;
  event_bus_.Publish(event);
  return true;
}

bool ItemEffectProcessor::ProcessRepairOil(entt::entity character, bool perfect) {
  if (!registry_.valid(character)) {
    return false;
  }

  const auto* equipment = registry_.try_get<ecs::EquipmentSlotComponent>(character);
  if (!equipment) {
    return false;
  }

  const std::size_t weapon_slot =
      static_cast<std::size_t>(mir2::common::EquipSlot::WEAPON);
  if (weapon_slot >= equipment->slots.size()) {
    return false;
  }

  const entt::entity weapon_entity = equipment->slots[weapon_slot];
  if (weapon_entity == entt::null || !registry_.valid(weapon_entity)) {
    return false;
  }

  auto* weapon = registry_.try_get<ecs::ItemComponent>(weapon_entity);
  if (!weapon) {
    return false;
  }

  const int max_durability = weapon->max_durability;
  if (max_durability <= 0) {
    return false;
  }

  const int original_durability = weapon->durability;
  int repaired_durability = original_durability;

  if (perfect) {
    repaired_durability = max_durability;
  } else {
    const int missing = max_durability - original_durability;
    if (missing > 0) {
      repaired_durability = original_durability + (missing / 3);
    }
  }

  repaired_durability = std::clamp(repaired_durability, 0, max_durability);

  if (repaired_durability != original_durability) {
    weapon->durability = repaired_durability;
    ecs::dirty_tracker::mark_items_dirty(registry_, character);
    ecs::dirty_tracker::mark_equipment_dirty(registry_, character);
  }

  return true;
}

bool ItemEffectProcessor::ProcessBlessOil(entt::entity character) {
  if (!registry_.valid(character)) {
    return false;
  }

  const auto* equipment = registry_.try_get<ecs::EquipmentSlotComponent>(character);
  if (!equipment) {
    return false;
  }

  const std::size_t weapon_slot =
      static_cast<std::size_t>(mir2::common::EquipSlot::WEAPON);
  const entt::entity weapon_entity = equipment->slots[weapon_slot];
  if (weapon_entity == entt::null || !registry_.valid(weapon_entity)) {
    return false;
  }

  auto* weapon = registry_.try_get<ecs::ItemComponent>(weapon_entity);
  if (!weapon) {
    return false;
  }

  const int original_luck = weapon->luck;

  if (RollOneIn(20)) {
    weapon->luck = original_luck - 1;
  } else if (original_luck < 0) {
    weapon->luck = original_luck + 1;
  } else if (original_luck == 0) {
    weapon->luck = original_luck + 1;
  } else if (original_luck < 3) {
    if (RollOneIn(6)) {
      weapon->luck = original_luck + 1;
    }
  } else if (original_luck < 7) {
    if (RollOneIn(30)) {
      weapon->luck = original_luck + 1;
    }
  }

  if (weapon->luck != original_luck) {
    ecs::dirty_tracker::mark_items_dirty(registry_, character);
    ecs::dirty_tracker::mark_equipment_dirty(registry_, character);
  }

  return true;
}

}  // namespace mir2::game::item
