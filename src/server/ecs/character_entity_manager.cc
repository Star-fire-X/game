/**
 * @file character_entity_manager.cc
 * @brief 角色实体管理器实现
 */

#include "ecs/character_entity_manager.h"

#include "ecs/components/character_components.h"
#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/lifecycle_events.h"
#include "ecs/registry_manager.h"
#include "ecs/systems/movement_system.h"
#include "legacy/character_factory.h"
#include "log/logger.h"

#include <cassert>
#include <chrono>
#include <exception>
#include <vector>

namespace mir2::ecs {

namespace {

mir2::common::CharacterCreateRequest BuildDefaultCreateRequest(uint32_t character_id) {
  mir2::common::CharacterCreateRequest request;
  request.account_id = "";
  request.name = std::string("Player") + std::to_string(character_id);
  request.char_class = mir2::common::CharacterClass::WARRIOR;
  request.gender = mir2::common::Gender::MALE;
  return request;
}

}  // namespace

CharacterEntityManager::CharacterEntityManager(entt::registry& registry)
    : thread_id_(std::this_thread::get_id()),
      registry_(&registry) {}

CharacterEntityManager::CharacterEntityManager(RegistryManager& registry_manager)
    : thread_id_(std::this_thread::get_id()),
      registry_manager_(&registry_manager) {}

entt::entity CharacterEntityManager::GetOrCreate(uint32_t character_id) {
  AssertSameThread();
  uint32_t map_id = 1;

  if (auto mapped = TryGetMapId(character_id)) {
    map_id = *mapped;
  } else {
    auto stored_it = stored_characters_.find(character_id);
    if (stored_it != stored_characters_.end()) {
      map_id = stored_it->second.map_id == 0 ? map_id : stored_it->second.map_id;
    }
  }

  return GetOrCreate(character_id, map_id);
}

entt::entity CharacterEntityManager::GetOrCreate(uint32_t character_id, uint32_t map_id) {
  AssertSameThread();
  if (map_id == 0) {
    map_id = 1;
  }

  if (auto existing = TryGet(character_id)) {
    Touch(character_id);
    return *existing;
  }

  if (auto existing = FindEntityByComponentId(character_id, map_id)) {
    if (IndexCharacter(character_id, map_id, *existing)) {
      Touch(character_id);
      return *existing;
    }
  } else if (auto existing_any = FindEntityByComponentId(character_id)) {
    auto mapped = TryGetMapId(character_id);
    if (mapped && IndexCharacter(character_id, *mapped, *existing_any)) {
      Touch(character_id);
      return *existing_any;
    }
  }

  entt::registry* registry = EnsureRegistry(map_id);
  if (!registry) {
    SYSLOG_ERROR("CharacterEntityManager missing registry for map_id={} id={}",
                 map_id, character_id);
    return entt::null;
  }

  entt::entity entity = entt::null;
  auto stored_it = stored_characters_.find(character_id);
  if (stored_it != stored_characters_.end()) {
    auto data = stored_it->second;
    if (data.map_id != map_id) {
      data.map_id = map_id;
    }
    entity = legend2::LoadCharacterEntity(*registry, data);
    if (entity != entt::null) {
      stored_characters_[character_id] = data;
    }
  } else {
    entity = legend2::CreateCharacterEntity(
        *registry, character_id, BuildDefaultCreateRequest(character_id));
  }

  if (entity == entt::null || !registry->valid(entity)) {
    SYSLOG_ERROR("CharacterEntityManager failed to create entity id={}", character_id);
    return entt::null;
  }

  if (!IndexCharacter(character_id, map_id, entity)) {
    SYSLOG_ERROR("CharacterEntityManager index failed id={}", character_id);
    registry->destroy(entity);
    return entt::null;
  }

  sessions_.try_emplace(character_id);
  Touch(character_id);

  if (stored_it == stored_characters_.end()) {
    Save(character_id);
  }

  return entity;
}

entt::entity CharacterEntityManager::CreateFromRequest(
    uint32_t character_id,
    const mir2::common::CharacterCreateRequest& request,
    EventBus* event_bus) {
  AssertSameThread();
  if (auto existing = TryGet(character_id)) {
    SYSLOG_WARN("CharacterEntityManager reuse existing entity id={}", character_id);
    return *existing;
  }

  const uint32_t map_id = 1;
  entt::registry* registry = EnsureRegistry(map_id);
  if (!registry) {
    SYSLOG_ERROR("CharacterEntityManager missing registry for map_id={} id={}",
                 map_id, character_id);
    return entt::null;
  }

  entt::entity entity = legend2::CreateCharacterEntity(*registry, character_id, request);
  if (entity == entt::null || !registry->valid(entity)) {
    SYSLOG_ERROR("CharacterEntityManager failed to create entity from request id={}", character_id);
    return entt::null;
  }

  if (!IndexCharacter(character_id, map_id, entity)) {
    SYSLOG_ERROR("CharacterEntityManager index failed for request id={}", character_id);
    registry->destroy(entity);
    return entt::null;
  }

  sessions_.try_emplace(character_id);
  Touch(character_id);
  Save(character_id);

  // 发布角色创建事件
  if (event_bus) {
    events::CharacterCreatedEvent event;
    event.entity = entity;
    event.character_id = character_id;
    event_bus->Publish(event);
  }

  return entity;
}

entt::entity CharacterEntityManager::Preload(const mir2::common::CharacterData& data) {
  stored_characters_[data.id] = data;
  entt::entity entity = GetOrCreate(data.id, data.map_id);
  entt::registry* registry = ResolveRegistry(data.map_id);
  if (entity != entt::null && registry && registry->valid(entity)) {
    auto& state = registry->get_or_emplace<CharacterStateComponent>(entity);
    const int64_t now = NowSeconds();
    state.last_login = now;
    state.last_active = now;
  }
  return entity;
}

std::optional<entt::entity> CharacterEntityManager::TryGet(uint32_t character_id) {
  AssertSameThread();
  auto it = id_index_.find(character_id);
  if (it == id_index_.end()) {
    return std::nullopt;
  }

  auto map_it = character_to_map_.find(character_id);
  if (map_it == character_to_map_.end()) {
    id_index_.erase(it);
    SYSLOG_WARN("CharacterEntityManager: missing map index id={}", character_id);
    return std::nullopt;
  }

  entt::registry* registry = ResolveRegistry(map_it->second);
  if (!registry) {
    SYSLOG_WARN("CharacterEntityManager: registry missing map_id={} id={}",
                map_it->second, character_id);
    return std::nullopt;
  }

  entt::entity entity = it->second;

  if (!registry->valid(entity)) {
    id_index_.erase(it);
    character_to_map_.erase(map_it);
    SYSLOG_WARN("CharacterEntityManager: stale index removed id={}", character_id);
    return std::nullopt;
  }

  const auto* identity = registry->try_get<CharacterIdentityComponent>(entity);
  assert(identity && identity->id == character_id);

  if (!identity || identity->id != character_id) {
    id_index_.erase(it);
    character_to_map_.erase(map_it);
    SYSLOG_ERROR("CharacterEntityManager: index corruption id={}", character_id);
    return std::nullopt;
  }

  return entity;
}

std::optional<uint32_t> CharacterEntityManager::TryGetMapId(uint32_t character_id) const {
  auto it = character_to_map_.find(character_id);
  if (it == character_to_map_.end()) {
    return std::nullopt;
  }
  return it->second;
}

entt::registry* CharacterEntityManager::TryGetRegistry(uint32_t character_id) {
  auto map_id = TryGetMapId(character_id);
  if (!map_id) {
    return nullptr;
  }
  return ResolveRegistry(*map_id);
}

const entt::registry* CharacterEntityManager::TryGetRegistry(uint32_t character_id) const {
  auto map_id = TryGetMapId(character_id);
  if (!map_id) {
    return nullptr;
  }
  return ResolveRegistry(*map_id);
}

bool CharacterEntityManager::SetPosition(uint32_t character_id,
                                         int x,
                                         int y,
                                         uint32_t map_id) {
  if (map_id == 0) {
    map_id = 1;
  }

  auto current_map = TryGetMapId(character_id);
  if (current_map && *current_map != map_id) {
    return MoveToMap(character_id, map_id, x, y);
  }

  entt::entity entity = GetOrCreate(character_id, map_id);
  if (entity == entt::null) {
    return false;
  }

  entt::registry* registry = ResolveRegistry(map_id);
  if (!registry) {
    return false;
  }

  MovementSystem::SetPosition(*registry, entity, x, y);
  MovementSystem::SetMapId(*registry, entity, map_id);
  Touch(character_id);
  return true;
}

bool CharacterEntityManager::MoveToMap(uint32_t character_id, uint32_t new_map_id) {
  AssertSameThread();
  auto entity = TryGet(character_id);
  if (!entity) {
    return false;
  }

  auto map_id = TryGetMapId(character_id);
  entt::registry* registry = map_id ? ResolveRegistry(*map_id) : nullptr;
  if (!registry) {
    return false;
  }

  const auto* state = registry->try_get<CharacterStateComponent>(*entity);
  const int x = state ? state->position.x : 0;
  const int y = state ? state->position.y : 0;
  return MoveToMap(character_id, new_map_id, x, y);
}

bool CharacterEntityManager::MoveToMap(uint32_t character_id,
                                       uint32_t new_map_id,
                                       int x,
                                       int y) {
  AssertSameThread();
  if (new_map_id == 0) {
    new_map_id = 1;
  }

  auto current_map = TryGetMapId(character_id);
  if (current_map && *current_map == new_map_id) {
    return SetPosition(character_id, x, y, new_map_id);
  }

  entt::entity entity = entt::null;
  entt::registry* source_registry = nullptr;
  if (current_map) {
    source_registry = ResolveRegistry(*current_map);
    if (source_registry) {
      auto it = id_index_.find(character_id);
      if (it != id_index_.end()) {
        entity = it->second;
      }
    }
  }

  entt::registry* target_registry = EnsureRegistry(new_map_id);
  if (!target_registry) {
    SYSLOG_ERROR("MoveToMap: missing target registry id={} map_id={}",
                 character_id, new_map_id);
    return false;
  }

  if (source_registry && source_registry == target_registry) {
    if (entity == entt::null || !source_registry->valid(entity)) {
      entity = GetOrCreate(character_id, new_map_id);
    }
    if (entity == entt::null || !source_registry->valid(entity)) {
      return false;
    }
    MovementSystem::SetPosition(*source_registry, entity, x, y);
    MovementSystem::SetMapId(*source_registry, entity, new_map_id);
    character_to_map_[character_id] = new_map_id;
    Touch(character_id);
    return true;
  }

  mir2::common::CharacterData data;
  bool has_source = source_registry && entity != entt::null && source_registry->valid(entity);
  if (has_source) {
    data = legend2::SaveCharacterData(*source_registry, entity);
  } else {
    auto stored_it = stored_characters_.find(character_id);
    if (stored_it == stored_characters_.end()) {
      entity = GetOrCreate(character_id, new_map_id);
      if (entity == entt::null) {
        return false;
      }
      MovementSystem::SetPosition(*target_registry, entity, x, y);
      MovementSystem::SetMapId(*target_registry, entity, new_map_id);
      Touch(character_id);
      return true;
    }
    data = stored_it->second;
  }

  data.map_id = new_map_id;
  data.position = {x, y};

  if (has_source) {
    source_registry->destroy(entity);
    UnindexCharacter(character_id);
  }

  entt::entity new_entity = legend2::LoadCharacterEntity(*target_registry, data);
  if (new_entity == entt::null || !target_registry->valid(new_entity)) {
    SYSLOG_ERROR("MoveToMap: failed to load entity id={} map_id={}",
                 character_id, new_map_id);
    return false;
  }

  if (!IndexCharacter(character_id, new_map_id, new_entity)) {
    SYSLOG_ERROR("MoveToMap: index failed id={} map_id={}", character_id, new_map_id);
    target_registry->destroy(new_entity);
    return false;
  }

  stored_characters_[character_id] = data;
  sessions_.try_emplace(character_id);
  Touch(character_id);
  return true;
}

void CharacterEntityManager::OnLogin(uint32_t character_id, EventBus* event_bus) {
  AssertSameThread();
  entt::entity entity = GetOrCreate(character_id);
  if (entity == entt::null) {
    return;
  }

  entt::registry* registry = TryGetRegistry(character_id);
  if (!registry) {
    return;
  }

  auto& state = registry->get_or_emplace<CharacterStateComponent>(entity);
  const int64_t now = NowSeconds();
  state.last_login = now;
  state.last_active = now;

  auto& session = sessions_[character_id];
  session.connected = true;
  session.time_since_disconnect = 0.0f;

  // 发布登录事件
  if (event_bus) {
    events::CharacterLoginEvent event;
    event.entity = entity;
    event.character_id = character_id;
    event_bus->Publish(event);
  }
}

void CharacterEntityManager::OnDisconnect(uint32_t character_id, EventBus* event_bus) {
  AssertSameThread();
  auto entity = TryGet(character_id);
  SaveIfDirty(character_id);

  auto it = sessions_.find(character_id);
  if (it != sessions_.end()) {
    it->second.connected = false;
    it->second.time_since_disconnect = 0.0f;
  }

  // 发布登出事件
  if (event_bus && entity) {
    events::CharacterLogoutEvent event;
    event.entity = *entity;
    event.character_id = character_id;
    event_bus->Publish(event);
  }
}

void CharacterEntityManager::Update(float delta_time) {
  AssertSameThread();
  std::vector<uint32_t> expired_ids;
  for (auto& [character_id, session] : sessions_) {
    auto entity = TryGet(character_id);
    if (!entity) {
      expired_ids.push_back(character_id);
      continue;
    }

    entt::registry* registry = TryGetRegistry(character_id);
    if (!registry) {
      expired_ids.push_back(character_id);
      continue;
    }

    if (session.connected) {
      session.time_since_last_save += delta_time;
      if (session.time_since_last_save >= save_interval_seconds_) {
        SaveIfDirty(character_id);
        session.time_since_last_save = 0.0f;
      }
      continue;
    }

    session.time_since_disconnect += delta_time;
    if (session.time_since_disconnect < timeout_seconds_) {
      continue;
    }

    const auto save_result = SaveIfDirty(character_id);
    if (save_result == SaveResult::kSaveFailed) {
      // Keep entity alive so we can retry saving later.
      session.time_since_disconnect = 0.0f;
      continue;
    }
    registry->destroy(*entity);
    UnindexCharacter(character_id);
    expired_ids.push_back(character_id);
  }

  for (uint32_t character_id : expired_ids) {
    sessions_.erase(character_id);
  }
}

std::optional<mir2::common::CharacterData> CharacterEntityManager::Save(uint32_t character_id) {
  AssertSameThread();
  auto entity = TryGet(character_id);
  if (!entity) {
    return std::nullopt;
  }

  entt::registry* registry = TryGetRegistry(character_id);
  if (!registry) {
    return std::nullopt;
  }

  auto data = legend2::SaveCharacterData(*registry, *entity);
  stored_characters_[character_id] = data;
  return data;
}

CharacterEntityManager::SaveResult CharacterEntityManager::SaveIfDirty(uint32_t character_id) {
  AssertSameThread();
  auto entity = TryGet(character_id);
  if (!entity) {
    return SaveResult::kEntityNotFound;
  }

  entt::registry* registry = TryGetRegistry(character_id);
  if (!registry) {
    return SaveResult::kEntityNotFound;
  }

  if (!dirty_tracker::is_dirty(*registry, *entity)) {
    return SaveResult::kNotDirty;
  }

  try {
    auto data = legend2::SaveCharacterData(*registry, *entity);
    stored_characters_[character_id] = data;
    dirty_tracker::clear_dirty(*registry, *entity);
    return SaveResult::kSuccess;
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("CharacterEntityManager SaveIfDirty failed id={} error={}",
                 character_id, ex.what());
  } catch (...) {
    SYSLOG_ERROR("CharacterEntityManager SaveIfDirty failed id={} error=unknown",
                 character_id);
  }

  if (error_policy_ == ErrorPolicy::kClearDirtyFlag) {
    dirty_tracker::clear_dirty(*registry, *entity);
  }
  return SaveResult::kSaveFailed;
}

void CharacterEntityManager::SaveAll() {
  std::vector<uint32_t> ids;
  ids.reserve(id_index_.size());

  // 收集所有有效的 character_id
  for (auto it = id_index_.begin(); it != id_index_.end(); ++it) {
    auto* registry = TryGetRegistry(it->first);
    if (!registry) {
      continue;
    }
    if (registry->valid(it->second)) {
      ids.push_back(it->first);
    }
  }

  // 保存所有角色
  for (uint32_t character_id : ids) {
    Save(character_id);
  }
}

void CharacterEntityManager::SaveAllDirty() {
  auto save_registry = [this](entt::registry& registry) {
    auto view = registry.view<DirtyComponent>();
    for (auto entity : view) {
      auto* identity = registry.try_get<CharacterIdentityComponent>(entity);
      if (!identity) {
        continue;
      }

      if (!dirty_tracker::is_dirty(registry, entity)) {
        continue;
      }

      try {
        auto data = legend2::SaveCharacterData(registry, entity);
        stored_characters_[identity->id] = data;
        dirty_tracker::clear_dirty(registry, entity);
      } catch (const std::exception& ex) {
        SYSLOG_ERROR("CharacterEntityManager SaveAllDirty failed id={} error={}",
                     identity->id, ex.what());
        if (error_policy_ == ErrorPolicy::kClearDirtyFlag) {
          dirty_tracker::clear_dirty(registry, entity);
        }
      } catch (...) {
        SYSLOG_ERROR("CharacterEntityManager SaveAllDirty failed id={} error=unknown",
                     identity->id);
        if (error_policy_ == ErrorPolicy::kClearDirtyFlag) {
          dirty_tracker::clear_dirty(registry, entity);
        }
      }
    }
  };

  if (registry_) {
    save_registry(*registry_);
    return;
  }

  if (registry_manager_) {
    registry_manager_->ForEachWorld([&save_registry](uint32_t /*map_id*/, World& world) {
      save_registry(world.Registry());
    });
  }
}

std::optional<mir2::common::CharacterData> CharacterEntityManager::GetStoredData(
    uint32_t character_id) const {
  auto it = stored_characters_.find(character_id);
  if (it == stored_characters_.end()) {
    return std::nullopt;
  }
  return it->second;
}

entt::registry* CharacterEntityManager::ResolveRegistry(uint32_t map_id) const {
  if (registry_) {
    return registry_;
  }
  if (!registry_manager_) {
    return nullptr;
  }
  auto* world = registry_manager_->GetWorld(map_id);
  if (!world) {
    return nullptr;
  }
  return &world->Registry();
}

entt::registry* CharacterEntityManager::EnsureRegistry(uint32_t map_id) {
  if (registry_) {
    return registry_;
  }
  if (!registry_manager_) {
    return nullptr;
  }
  auto* world = registry_manager_->GetWorld(map_id);
  if (!world) {
    world = registry_manager_->CreateWorld(map_id);
  }
  return world ? &world->Registry() : nullptr;
}

std::optional<entt::entity> CharacterEntityManager::FindEntityByComponentId(
    uint32_t character_id,
    uint32_t map_id) {
  entt::registry* registry = ResolveRegistry(map_id);
  if (!registry) {
    return std::nullopt;
  }

  auto view = registry->view<CharacterIdentityComponent>();
  std::optional<entt::entity> found;
  for (auto entity : view) {
    const auto& identity = view.get<CharacterIdentityComponent>(entity);
    if (identity.id != character_id) {
      continue;
    }
    if (!found) {
      found = entity;
      continue;
    }
    SYSLOG_ERROR("CharacterEntityManager duplicate id={} entity={} duplicate={}",
                 character_id, entt::to_integral(*found), entt::to_integral(entity));
  }
  if (found) {
    SYSLOG_WARN("CharacterEntityManager repaired index id={}", character_id);
  }
  return found;
}

std::optional<entt::entity> CharacterEntityManager::FindEntityByComponentId(
    uint32_t character_id) {
  if (registry_) {
    auto found = FindEntityByComponentId(character_id, 1);
    if (found) {
      auto* state = registry_->try_get<CharacterStateComponent>(*found);
      const uint32_t map_id = state ? state->map_id : 1;
      character_to_map_[character_id] = map_id;
    }
    return found;
  }

  if (!registry_manager_) {
    return std::nullopt;
  }

  std::optional<entt::entity> found;
  std::optional<uint32_t> found_map;
  registry_manager_->ForEachWorld([&](uint32_t map_id, World& /*world*/) {
    auto maybe = FindEntityByComponentId(character_id, map_id);
    if (!maybe) {
      return;
    }
    if (!found) {
      found = maybe;
      found_map = map_id;
      character_to_map_[character_id] = map_id;
      return;
    }
    SYSLOG_ERROR("CharacterEntityManager duplicate across worlds id={} map_old={} map_new={}",
                 character_id, found_map.value_or(0), map_id);
  });
  return found;
}

bool CharacterEntityManager::IndexCharacter(uint32_t character_id,
                                            uint32_t map_id,
                                            entt::entity entity) {
  entt::registry* registry = ResolveRegistry(map_id);
  if (!registry || entity == entt::null || !registry->valid(entity)) {
    SYSLOG_ERROR("IndexCharacter: invalid entity id={} map_id={}", character_id, map_id);
    return false;
  }

  const auto* identity = registry->try_get<CharacterIdentityComponent>(entity);
  if (!identity || identity->id != character_id) {
    SYSLOG_ERROR("IndexCharacter: identity mismatch id={} map_id={}", character_id, map_id);
    return false;
  }

  auto it = id_index_.find(character_id);
  if (it != id_index_.end() && it->second != entity) {
    SYSLOG_ERROR("IndexCharacter: duplicate id={} old={} new={}",
                 character_id, entt::to_integral(it->second),
                 entt::to_integral(entity));
    return false;
  }

  id_index_[character_id] = entity;
  character_to_map_[character_id] = map_id;
  return true;
}

void CharacterEntityManager::UnindexCharacter(uint32_t character_id) {
  id_index_.erase(character_id);
  character_to_map_.erase(character_id);
}

void CharacterEntityManager::Touch(uint32_t character_id) {
  auto entity = TryGet(character_id);
  if (!entity) {
    return;
  }

  entt::registry* registry = TryGetRegistry(character_id);
  if (!registry) {
    return;
  }

  auto& state = registry->get_or_emplace<CharacterStateComponent>(*entity);
  state.last_active = NowSeconds();

  auto& session = sessions_[character_id];
  session.connected = true;
  session.time_since_disconnect = 0.0f;
}

size_t CharacterEntityManager::GetIndexSize() const {
  return id_index_.size();
}

int64_t CharacterEntityManager::NowSeconds() {
  auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

}  // namespace mir2::ecs
