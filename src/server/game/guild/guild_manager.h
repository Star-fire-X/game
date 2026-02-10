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
  entt::entity CreateGuild(uint32_t guild_id, const std::string& name,
                           entt::entity leader, entt::registry& registry);
  bool DeleteGuild(uint32_t guild_id, entt::registry& registry);
  entt::entity GetGuildEntity(uint32_t guild_id) const;
  entt::entity GetGuildByName(const std::string& name) const;
  ecs::GuildComponent* GetGuild(uint32_t guild_id, entt::registry& registry);
  bool AddMember(uint32_t guild_id, entt::entity member,
                 entt::registry& registry);
  bool RemoveMember(uint32_t guild_id, entt::entity member,
                    entt::registry& registry);
  bool DeclareWar(uint32_t attacker_id, uint32_t target_id,
                  entt::registry& registry);
  bool CancelWar(uint32_t guild1_id, uint32_t guild2_id,
                 entt::registry& registry);
  bool IsAtWar(uint32_t guild1_id, uint32_t guild2_id) const;
  bool MakeAlliance(uint32_t guild1_id, uint32_t guild2_id,
                    entt::registry& registry);
  bool BreakAlliance(uint32_t guild1_id, uint32_t guild2_id,
                     entt::registry& registry);
  bool IsAllied(uint32_t guild1_id, uint32_t guild2_id) const;
  int GetGuildRelation(uint32_t guild1_id, uint32_t guild2_id) const;
  uint8_t GetMemberColor(uint32_t viewer_guild_id,
                         uint32_t target_guild_id) const;
  uint32_t GenerateGuildId();
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

  std::unordered_map<uint32_t, entt::entity> guilds_;
  std::unordered_map<std::string, uint32_t> name_to_id_;
  uint32_t next_guild_id_ = 1;
  mutable std::mutex mutex_;
};

}  // namespace mir2::game::guild

#endif  // MIR2_GAME_GUILD_GUILD_MANAGER_H_
