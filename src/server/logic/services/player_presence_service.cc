#include "logic/services/player_presence_service.h"

#include <memory>

#include "ecs/components/character_components.h"

namespace mir2::logic {

namespace {

std::optional<entt::entity> FindEntityById(entt::registry& registry,
                                           uint64_t player_id) {
  auto view =
      registry.view<ecs::CharacterIdentityComponent, ecs::CharacterStateComponent>();
  for (auto entity : view) {
    const auto& identity = view.get<ecs::CharacterIdentityComponent>(entity);
    if (identity.id != player_id) {
      continue;
    }
    return entity;
  }
  return std::nullopt;
}

}  // namespace

std::unique_ptr<PlayerPresenceService> PlayerPresenceService::CreateDefault(
    entt::registry& registry) {
  return std::make_unique<PlayerPresenceService>(registry);
}

PlayerPresenceService::PlayerPresenceService(entt::registry& registry)
    : registry_(registry) {}

bool PlayerPresenceService::IsOnline(uint64_t player_id) const {
  auto entity = FindEntityById(registry_, player_id);
  if (!entity.has_value()) {
    return false;
  }
  const auto* state = registry_.try_get<ecs::CharacterStateComponent>(*entity);
  return state && state->is_online;
}

std::optional<entt::entity> PlayerPresenceService::FindEntity(uint64_t player_id) const {
  auto entity = FindEntityById(registry_, player_id);
  if (!entity.has_value()) {
    return std::nullopt;
  }
  const auto* state = registry_.try_get<ecs::CharacterStateComponent>(*entity);
  if (!state || !state->is_online) {
    return std::nullopt;
  }
  return entity;
}

std::optional<uint64_t> PlayerPresenceService::FindPlayerIdByName(
    const std::string& name) const {
  if (name.empty()) {
    return std::nullopt;
  }

  auto cached = name_lookup_cache_.find(name);
  if (cached != name_lookup_cache_.end()) {
    auto entity_opt = FindEntityById(registry_, cached->second);
    if (entity_opt.has_value()) {
      const auto* identity = registry_.try_get<ecs::CharacterIdentityComponent>(*entity_opt);
      const auto* state = registry_.try_get<ecs::CharacterStateComponent>(*entity_opt);
      if (identity && state && state->is_online && identity->name == name) {
        return cached->second;
      }
    }
    name_lookup_cache_.erase(cached);
  }

  auto view =
      registry_.view<ecs::CharacterIdentityComponent, ecs::CharacterStateComponent>();
  for (auto entity : view) {
    const auto& identity = view.get<ecs::CharacterIdentityComponent>(entity);
    if (identity.name != name) {
      continue;
    }
    const auto& state = view.get<ecs::CharacterStateComponent>(entity);
    if (!state.is_online) {
      name_lookup_cache_.erase(name);
      return std::nullopt;
    }
    if (name_lookup_cache_.size() >= kNameLookupCacheMaxEntries) {
      name_lookup_cache_.clear();
    }
    name_lookup_cache_[name] = identity.id;
    return identity.id;
  }
  name_lookup_cache_.erase(name);
  return std::nullopt;
}

std::optional<std::string> PlayerPresenceService::FindName(uint64_t player_id) const {
  auto entity = FindEntity(player_id);
  if (!entity.has_value()) {
    return std::nullopt;
  }
  const auto* identity = registry_.try_get<ecs::CharacterIdentityComponent>(*entity);
  if (!identity) {
    return std::nullopt;
  }
  return identity->name;
}

std::optional<uint32_t> PlayerPresenceService::FindMapId(uint64_t player_id) const {
  auto entity = FindEntity(player_id);
  if (!entity.has_value()) {
    return std::nullopt;
  }
  const auto* state = registry_.try_get<ecs::CharacterStateComponent>(*entity);
  if (!state) {
    return std::nullopt;
  }
  return state->map_id;
}

std::vector<uint64_t> PlayerPresenceService::GetOnlinePlayerIdsOnMap(uint32_t map_id) const {
  std::vector<uint64_t> player_ids;
  auto view =
      registry_.view<ecs::CharacterIdentityComponent, ecs::CharacterStateComponent>();
  for (auto entity : view) {
    const auto& state = view.get<ecs::CharacterStateComponent>(entity);
    if (!state.is_online || state.map_id != map_id) {
      continue;
    }
    const auto& identity = view.get<ecs::CharacterIdentityComponent>(entity);
    player_ids.push_back(identity.id);
  }
  return player_ids;
}

std::vector<uint64_t> PlayerPresenceService::GetAllOnlinePlayerIds() const {
  std::vector<uint64_t> player_ids;
  auto view =
      registry_.view<ecs::CharacterIdentityComponent, ecs::CharacterStateComponent>();
  for (auto entity : view) {
    const auto& state = view.get<ecs::CharacterStateComponent>(entity);
    if (!state.is_online) {
      continue;
    }
    const auto& identity = view.get<ecs::CharacterIdentityComponent>(entity);
    player_ids.push_back(identity.id);
  }
  return player_ids;
}

bool PlayerPresenceService::CanHearWhisper(uint64_t player_id) const {
  auto entity = FindEntity(player_id);
  if (!entity.has_value()) {
    return false;
  }
  if (const auto* pref = registry_.try_get<ecs::ChatPreferenceComponent>(*entity)) {
    return pref->hear_whisper;
  }
  return true;
}

bool PlayerPresenceService::CanHearCry(uint64_t player_id) const {
  auto entity = FindEntity(player_id);
  if (!entity.has_value()) {
    return false;
  }
  if (const auto* pref = registry_.try_get<ecs::ChatPreferenceComponent>(*entity)) {
    return pref->hear_cry;
  }
  return true;
}

bool PlayerPresenceService::CanHearGuildMessage(uint64_t player_id) const {
  auto entity = FindEntity(player_id);
  if (!entity.has_value()) {
    return false;
  }
  if (const auto* pref = registry_.try_get<ecs::ChatPreferenceComponent>(*entity)) {
    return pref->hear_guild_msg;
  }
  return true;
}

bool PlayerPresenceService::IsDead(uint64_t player_id) const {
  auto entity = FindEntity(player_id);
  if (!entity.has_value()) {
    return true;
  }
  const auto* attr = registry_.try_get<ecs::CharacterAttributesComponent>(*entity);
  return !attr || attr->hp <= 0;
}

bool PlayerPresenceService::IsBlocked(uint64_t owner_id, uint64_t target_id) const {
  auto owner = FindEntity(owner_id);
  if (!owner.has_value()) {
    return false;
  }
  const auto* pref = registry_.try_get<ecs::ChatPreferenceComponent>(*owner);
  if (!pref) {
    return false;
  }
  return pref->IsBlocked(static_cast<uint32_t>(target_id));
}

bool PlayerPresenceService::AddBlock(uint64_t owner_id, uint64_t target_id) {
  auto owner = FindEntity(owner_id);
  if (!owner.has_value() || owner_id == target_id) {
    return false;
  }
  auto& pref = registry_.get_or_emplace<ecs::ChatPreferenceComponent>(*owner);
  return pref.AddBlock(static_cast<uint32_t>(target_id));
}

bool PlayerPresenceService::RemoveBlock(uint64_t owner_id, uint64_t target_id) {
  auto owner = FindEntity(owner_id);
  if (!owner.has_value()) {
    return false;
  }
  auto& pref = registry_.get_or_emplace<ecs::ChatPreferenceComponent>(*owner);
  pref.RemoveBlock(static_cast<uint32_t>(target_id));
  return true;
}

bool PlayerPresenceService::SetHearWhisper(uint64_t player_id, bool enabled) {
  auto entity = FindEntity(player_id);
  if (!entity.has_value()) {
    return false;
  }
  auto& pref = registry_.get_or_emplace<ecs::ChatPreferenceComponent>(*entity);
  pref.hear_whisper = enabled;
  return true;
}

bool PlayerPresenceService::SetHearCry(uint64_t player_id, bool enabled) {
  auto entity = FindEntity(player_id);
  if (!entity.has_value()) {
    return false;
  }
  auto& pref = registry_.get_or_emplace<ecs::ChatPreferenceComponent>(*entity);
  pref.hear_cry = enabled;
  return true;
}

bool PlayerPresenceService::SetHearGuildMessage(uint64_t player_id, bool enabled) {
  auto entity = FindEntity(player_id);
  if (!entity.has_value()) {
    return false;
  }
  auto& pref = registry_.get_or_emplace<ecs::ChatPreferenceComponent>(*entity);
  pref.hear_guild_msg = enabled;
  return true;
}

}  // namespace mir2::logic
