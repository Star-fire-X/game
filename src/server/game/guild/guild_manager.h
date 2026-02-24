/**
 * @file guild_manager.h
 * @brief 行会管理器
 *
 * 负责行会创建、成员管理和关系维护。
 * 使用单例模式确保全局唯一实例。
 */

#ifndef MIR2_GAME_GUILD_GUILD_MANAGER_H_
#define MIR2_GAME_GUILD_GUILD_MANAGER_H_

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include <entt/entt.hpp>

#include "core/singleton.h"
#include "ecs/components/guild_component.h"

namespace mir2::game::guild {

/**
 * @brief 行会管理器
 *
 * 单例类，负责行会实体的创建、成员管理与关系维护。
 *
 * @note 线程安全：所有公共方法都是线程安全的
 */
class GuildManager : public core::Singleton<GuildManager> {
  friend class core::Singleton<GuildManager>;

 public:
  entt::entity CreateGuild(ecs::GuildId guild_id, const std::string& name,
                           entt::entity leader, entt::registry& registry);
  bool DeleteGuild(ecs::GuildId guild_id, entt::registry& registry);
  entt::entity GetGuildEntity(ecs::GuildId guild_id) const;
  entt::entity GetGuildByName(const std::string& name) const;
  ecs::GuildComponent* GetGuild(ecs::GuildId guild_id, entt::registry& registry);
  bool AddMember(ecs::GuildId guild_id, entt::entity member,
                 entt::registry& registry);
  bool RemoveMember(ecs::GuildId guild_id, entt::entity member,
                    entt::registry& registry);
  bool DeclareWar(ecs::GuildId attacker_id, ecs::GuildId target_id,
                  entt::registry& registry);
  bool CancelWar(ecs::GuildId guild1_id, ecs::GuildId guild2_id,
                 entt::registry& registry);
  bool IsAtWar(ecs::GuildId guild1_id, ecs::GuildId guild2_id,
               entt::registry& registry) const;
  bool MakeAlliance(ecs::GuildId guild1_id, ecs::GuildId guild2_id,
                    entt::registry& registry);
  bool BreakAlliance(ecs::GuildId guild1_id, ecs::GuildId guild2_id,
                     entt::registry& registry);
  bool IsAllied(ecs::GuildId guild1_id, ecs::GuildId guild2_id,
                entt::registry& registry) const;
  int GetGuildRelation(ecs::GuildId guild1_id, ecs::GuildId guild2_id,
                       entt::registry& registry) const;
  uint8_t GetMemberColor(ecs::GuildId viewer_guild_id,
                         ecs::GuildId target_guild_id,
                         entt::registry& registry) const;
  ecs::GuildId GenerateGuildId();
  size_t GuildCount() const;

  template <typename Func>
  void ForEach(Func&& func) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [guild_id, entity] : guilds_) {
      (void)guild_id;
      func(entity);
    }
  }

  void Clear(entt::registry& registry);

 private:
  GuildManager() = default;
  ~GuildManager() = default;

  std::unordered_map<ecs::GuildId, entt::entity> guilds_;
  std::unordered_map<std::string, ecs::GuildId> name_to_id_;
  ecs::GuildId next_guild_id_ = ecs::kInvalidGuildId + 1;
  mutable std::mutex mutex_;
};

}  // namespace mir2::game::guild

#endif  // MIR2_GAME_GUILD_GUILD_MANAGER_H_
