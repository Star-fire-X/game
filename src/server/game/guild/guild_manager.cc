/**
 * @file guild_manager.cc
 * @brief 行会管理器实现
 */

#include "game/guild/guild_manager.h"

#include <algorithm>

#include "core/utils.h"
#include "ecs/components/character_components.h"

namespace {

entt::registry* g_registry = nullptr;

void CacheRegistry(entt::registry& registry) {
  g_registry = &registry;
}

mir2::ecs::GuildComponent* TryGetGuildComponent(entt::registry* registry,
                                                entt::entity entity) {
  if (!registry) {
    return nullptr;
  }
  if (!registry->valid(entity)) {
    return nullptr;
  }
  return registry->try_get<mir2::ecs::GuildComponent>(entity);
}

bool RemoveWar(mir2::ecs::GuildComponent& guild, mir2::ecs::GuildId enemy_id) {
  auto& wars = guild.war_guilds;
  auto it = std::remove_if(
      wars.begin(), wars.end(),
      [enemy_id](const mir2::ecs::GuildWarInfo& info) {
        return info.enemy_guild_id == enemy_id;
      });
  if (it == wars.end()) {
    return false;
  }
  wars.erase(it, wars.end());
  return true;
}

bool RemoveAlliance(mir2::ecs::GuildComponent& guild,
                    mir2::ecs::GuildId ally_id) {
  auto& allies = guild.ally_guild_ids;
  auto it = std::remove(allies.begin(), allies.end(), ally_id);
  if (it == allies.end()) {
    return false;
  }
  allies.erase(it, allies.end());
  return true;
}

}  // namespace

namespace mir2::game::guild {

entt::entity GuildManager::CreateGuild(ecs::GuildId guild_id,
                                       const std::string& name,
                                       entt::entity leader,
                                       entt::registry& registry) {
  std::lock_guard<std::mutex> lock(mutex_);
  CacheRegistry(registry);

  if (guild_id == ecs::kInvalidGuildId || name.empty()) {
    return entt::null;
  }

  if (guilds_.find(guild_id) != guilds_.end()) {
    return entt::null;
  }

  if (name_to_id_.find(name) != name_to_id_.end()) {
    return entt::null;
  }

  if (!registry.valid(leader)) {
    return entt::null;
  }

  auto* existing_member =
      registry.try_get<ecs::GuildMemberComponent>(leader);
  if (existing_member &&
      existing_member->guild_id != ecs::kInvalidGuildId) {
    return entt::null;
  }

  entt::entity entity = registry.create();
  auto& guild = registry.emplace<ecs::GuildComponent>(entity);
  guild.guild_id = guild_id;
  guild.guild_name = name;
  guild.leader = leader;
  guild.AddMember(leader);

  ecs::GuildMemberComponent leader_member;
  leader_member.guild_id = guild_id;
  leader_member.rank = ecs::GUILD_RANK_LEADER;
  if (const auto* identity =
          registry.try_get<ecs::CharacterIdentityComponent>(leader)) {
    leader_member.character_id = identity->id;
  }
  registry.emplace_or_replace<ecs::GuildMemberComponent>(leader, leader_member);

  guilds_[guild_id] = entity;
  name_to_id_[name] = guild_id;
  if (guild_id >= next_guild_id_) {
    next_guild_id_ = guild_id + 1;
  }

  return entity;
}

bool GuildManager::DeleteGuild(ecs::GuildId guild_id, entt::registry& registry) {
  std::lock_guard<std::mutex> lock(mutex_);
  CacheRegistry(registry);

  auto it = guilds_.find(guild_id);
  if (it == guilds_.end()) {
    return false;
  }

  entt::entity entity = it->second;
  ecs::GuildComponent* guild = TryGetGuildComponent(&registry, entity);

  if (guild) {
    for (const auto& member : guild->members) {
      if (!registry.valid(member)) {
        continue;
      }
      auto* member_comp = registry.try_get<ecs::GuildMemberComponent>(member);
      if (member_comp && member_comp->guild_id == guild_id) {
        registry.remove<ecs::GuildMemberComponent>(member);
      }
    }
    name_to_id_.erase(guild->guild_name);
  } else {
    for (auto name_it = name_to_id_.begin();
         name_it != name_to_id_.end();) {
      if (name_it->second == guild_id) {
        name_it = name_to_id_.erase(name_it);
      } else {
        ++name_it;
      }
    }
  }

  if (registry.valid(entity)) {
    registry.destroy(entity);
  }

  guilds_.erase(it);
  return true;
}

entt::entity GuildManager::GetGuildEntity(ecs::GuildId guild_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = guilds_.find(guild_id);
  if (it == guilds_.end()) {
    return entt::null;
  }
  return it->second;
}

entt::entity GuildManager::GetGuildByName(const std::string& name) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto name_it = name_to_id_.find(name);
  if (name_it == name_to_id_.end()) {
    return entt::null;
  }

  auto guild_it = guilds_.find(name_it->second);
  if (guild_it == guilds_.end()) {
    return entt::null;
  }

  return guild_it->second;
}

ecs::GuildComponent* GuildManager::GetGuild(ecs::GuildId guild_id,
                                            entt::registry& registry) {
  std::lock_guard<std::mutex> lock(mutex_);
  CacheRegistry(registry);

  auto it = guilds_.find(guild_id);
  if (it == guilds_.end()) {
    return nullptr;
  }

  return TryGetGuildComponent(&registry, it->second);
}

bool GuildManager::AddMember(ecs::GuildId guild_id, entt::entity member,
                             entt::registry& registry) {
  std::lock_guard<std::mutex> lock(mutex_);
  CacheRegistry(registry);

  auto it = guilds_.find(guild_id);
  if (it == guilds_.end()) {
    return false;
  }

  if (!registry.valid(member)) {
    return false;
  }

  ecs::GuildComponent* guild = TryGetGuildComponent(&registry, it->second);
  if (!guild) {
    return false;
  }

  if (guild->IsFull() || guild->IsMember(member)) {
    return false;
  }

  auto* member_comp = registry.try_get<ecs::GuildMemberComponent>(member);
  if (member_comp && member_comp->guild_id != ecs::kInvalidGuildId &&
      member_comp->guild_id != guild_id) {
    return false;
  }

  if (!guild->AddMember(member)) {
    return false;
  }

  ecs::GuildMemberComponent new_member;
  new_member.guild_id = guild_id;
  new_member.rank = ecs::GUILD_RANK_MEMBER;
  if (const auto* identity =
          registry.try_get<ecs::CharacterIdentityComponent>(member)) {
    new_member.character_id = identity->id;
  }
  registry.emplace_or_replace<ecs::GuildMemberComponent>(member, new_member);

  return true;
}

bool GuildManager::RemoveMember(ecs::GuildId guild_id, entt::entity member,
                                entt::registry& registry) {
  std::lock_guard<std::mutex> lock(mutex_);
  CacheRegistry(registry);

  auto it = guilds_.find(guild_id);
  if (it == guilds_.end()) {
    return false;
  }

  ecs::GuildComponent* guild = TryGetGuildComponent(&registry, it->second);
  if (!guild) {
    return false;
  }

  if (!guild->RemoveMember(member)) {
    return false;
  }

  if (guild->leader == member) {
    guild->leader = entt::null;
  }

  if (registry.valid(member)) {
    auto* member_comp = registry.try_get<ecs::GuildMemberComponent>(member);
    if (member_comp && member_comp->guild_id == guild_id) {
      registry.remove<ecs::GuildMemberComponent>(member);
    }
  }

  return true;
}

bool GuildManager::DeclareWar(ecs::GuildId attacker_id, ecs::GuildId target_id,
                              entt::registry& registry) {
  std::lock_guard<std::mutex> lock(mutex_);
  CacheRegistry(registry);

  if (attacker_id == target_id) {
    return false;
  }

  auto attacker_it = guilds_.find(attacker_id);
  auto target_it = guilds_.find(target_id);
  if (attacker_it == guilds_.end() || target_it == guilds_.end()) {
    return false;
  }

  ecs::GuildComponent* attacker =
      TryGetGuildComponent(&registry, attacker_it->second);
  ecs::GuildComponent* target =
      TryGetGuildComponent(&registry, target_it->second);
  if (!attacker || !target) {
    return false;
  }

  if (attacker->IsAtWarWith(target_id) || target->IsAtWarWith(attacker_id)) {
    return false;
  }

  uint64_t now = static_cast<uint64_t>(core::GetCurrentTimestampMs());

  ecs::GuildWarInfo attacker_war;
  attacker_war.enemy_guild_id = target_id;
  attacker_war.start_time = now;
  attacker_war.remain_time = ecs::GUILD_WAR_DURATION;
  attacker->war_guilds.push_back(attacker_war);

  ecs::GuildWarInfo target_war;
  target_war.enemy_guild_id = attacker_id;
  target_war.start_time = now;
  target_war.remain_time = ecs::GUILD_WAR_DURATION;
  target->war_guilds.push_back(target_war);

  return true;
}

bool GuildManager::CancelWar(ecs::GuildId guild1_id, ecs::GuildId guild2_id,
                             entt::registry& registry) {
  std::lock_guard<std::mutex> lock(mutex_);
  CacheRegistry(registry);

  if (guild1_id == guild2_id) {
    return false;
  }

  auto guild1_it = guilds_.find(guild1_id);
  auto guild2_it = guilds_.find(guild2_id);
  if (guild1_it == guilds_.end() || guild2_it == guilds_.end()) {
    return false;
  }

  ecs::GuildComponent* guild1 =
      TryGetGuildComponent(&registry, guild1_it->second);
  ecs::GuildComponent* guild2 =
      TryGetGuildComponent(&registry, guild2_it->second);
  if (!guild1 || !guild2) {
    return false;
  }

  bool removed1 = RemoveWar(*guild1, guild2_id);
  bool removed2 = RemoveWar(*guild2, guild1_id);
  return removed1 || removed2;
}

bool GuildManager::IsAtWar(ecs::GuildId guild1_id, ecs::GuildId guild2_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  if (guild1_id == guild2_id) {
    return false;
  }

  if (!g_registry) {
    return false;
  }

  auto guild1_it = guilds_.find(guild1_id);
  auto guild2_it = guilds_.find(guild2_id);
  if (guild1_it == guilds_.end() || guild2_it == guilds_.end()) {
    return false;
  }

  ecs::GuildComponent* guild1 =
      TryGetGuildComponent(g_registry, guild1_it->second);
  ecs::GuildComponent* guild2 =
      TryGetGuildComponent(g_registry, guild2_it->second);
  if (!guild1 || !guild2) {
    return false;
  }

  return guild1->IsAtWarWith(guild2_id) || guild2->IsAtWarWith(guild1_id);
}

bool GuildManager::MakeAlliance(ecs::GuildId guild1_id, ecs::GuildId guild2_id,
                                entt::registry& registry) {
  std::lock_guard<std::mutex> lock(mutex_);
  CacheRegistry(registry);

  if (guild1_id == guild2_id) {
    return false;
  }

  auto guild1_it = guilds_.find(guild1_id);
  auto guild2_it = guilds_.find(guild2_id);
  if (guild1_it == guilds_.end() || guild2_it == guilds_.end()) {
    return false;
  }

  ecs::GuildComponent* guild1 =
      TryGetGuildComponent(&registry, guild1_it->second);
  ecs::GuildComponent* guild2 =
      TryGetGuildComponent(&registry, guild2_it->second);
  if (!guild1 || !guild2) {
    return false;
  }

  if (!guild1->allow_ally || !guild2->allow_ally) {
    return false;
  }

  if (guild1->IsAlliedWith(guild2_id) || guild2->IsAlliedWith(guild1_id)) {
    return false;
  }

  guild1->ally_guild_ids.push_back(guild2_id);
  guild2->ally_guild_ids.push_back(guild1_id);

  return true;
}

bool GuildManager::BreakAlliance(ecs::GuildId guild1_id, ecs::GuildId guild2_id,
                                 entt::registry& registry) {
  std::lock_guard<std::mutex> lock(mutex_);
  CacheRegistry(registry);

  if (guild1_id == guild2_id) {
    return false;
  }

  auto guild1_it = guilds_.find(guild1_id);
  auto guild2_it = guilds_.find(guild2_id);
  if (guild1_it == guilds_.end() || guild2_it == guilds_.end()) {
    return false;
  }

  ecs::GuildComponent* guild1 =
      TryGetGuildComponent(&registry, guild1_it->second);
  ecs::GuildComponent* guild2 =
      TryGetGuildComponent(&registry, guild2_it->second);
  if (!guild1 || !guild2) {
    return false;
  }

  bool removed1 = RemoveAlliance(*guild1, guild2_id);
  bool removed2 = RemoveAlliance(*guild2, guild1_id);
  return removed1 || removed2;
}

bool GuildManager::IsAllied(ecs::GuildId guild1_id, ecs::GuildId guild2_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  if (guild1_id == guild2_id) {
    return false;
  }

  if (!g_registry) {
    return false;
  }

  auto guild1_it = guilds_.find(guild1_id);
  auto guild2_it = guilds_.find(guild2_id);
  if (guild1_it == guilds_.end() || guild2_it == guilds_.end()) {
    return false;
  }

  ecs::GuildComponent* guild1 =
      TryGetGuildComponent(g_registry, guild1_it->second);
  ecs::GuildComponent* guild2 =
      TryGetGuildComponent(g_registry, guild2_it->second);
  if (!guild1 || !guild2) {
    return false;
  }

  return guild1->IsAlliedWith(guild2_id) || guild2->IsAlliedWith(guild1_id);
}

int GuildManager::GetGuildRelation(ecs::GuildId guild1_id,
                                   ecs::GuildId guild2_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  if (guild1_id == guild2_id) {
    return ecs::GUILD_RELATION_SAME;
  }

  if (!g_registry) {
    return ecs::GUILD_RELATION_NONE;
  }

  auto guild1_it = guilds_.find(guild1_id);
  auto guild2_it = guilds_.find(guild2_id);
  if (guild1_it == guilds_.end() || guild2_it == guilds_.end()) {
    return ecs::GUILD_RELATION_NONE;
  }

  ecs::GuildComponent* guild1 =
      TryGetGuildComponent(g_registry, guild1_it->second);
  ecs::GuildComponent* guild2 =
      TryGetGuildComponent(g_registry, guild2_it->second);
  if (!guild1 || !guild2) {
    return ecs::GUILD_RELATION_NONE;
  }

  if (guild1->IsAtWarWith(guild2_id) || guild2->IsAtWarWith(guild1_id)) {
    return ecs::GUILD_RELATION_ENEMY;
  }

  if (guild1->IsAlliedWith(guild2_id) || guild2->IsAlliedWith(guild1_id)) {
    return ecs::GUILD_RELATION_ALLY;
  }

  return ecs::GUILD_RELATION_NONE;
}

uint8_t GuildManager::GetMemberColor(ecs::GuildId viewer_guild_id,
                                     ecs::GuildId target_guild_id) const {
  if (viewer_guild_id == ecs::kInvalidGuildId ||
      target_guild_id == ecs::kInvalidGuildId) {
    return ecs::GUILD_COLOR_NONE;
  }

  const int relation = GetGuildRelation(viewer_guild_id, target_guild_id);
  switch (relation) {
    case ecs::GUILD_RELATION_SAME:
    case ecs::GUILD_RELATION_ALLY:
      return ecs::GUILD_COLOR_SAME;
    case ecs::GUILD_RELATION_ENEMY:
      return ecs::GUILD_COLOR_ENEMY;
    default:
      return ecs::GUILD_COLOR_NONE;
  }
}

ecs::GuildId GuildManager::GenerateGuildId() {
  std::lock_guard<std::mutex> lock(mutex_);

  while (guilds_.find(next_guild_id_) != guilds_.end()) {
    ++next_guild_id_;
  }
  return next_guild_id_++;
}

size_t GuildManager::GuildCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return guilds_.size();
}

void GuildManager::Clear(entt::registry& registry) {
  std::lock_guard<std::mutex> lock(mutex_);
  CacheRegistry(registry);

  for (const auto& [guild_id, entity] : guilds_) {
    ecs::GuildComponent* guild = TryGetGuildComponent(&registry, entity);
    if (guild) {
      for (const auto& member : guild->members) {
        if (!registry.valid(member)) {
          continue;
        }
        auto* member_comp =
            registry.try_get<ecs::GuildMemberComponent>(member);
        if (member_comp && member_comp->guild_id == guild_id) {
          registry.remove<ecs::GuildMemberComponent>(member);
        }
      }
    }

    if (registry.valid(entity)) {
      registry.destroy(entity);
    }
  }

  guilds_.clear();
  name_to_id_.clear();
  next_guild_id_ = ecs::kInvalidGuildId + 1;
}

}  // namespace mir2::game::guild
