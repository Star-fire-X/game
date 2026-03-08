/**
 * @file chat_service.cc
 * @brief 聊天服务实现
 */

#include "game/chat/chat_service.h"

#include <chrono>
#include <optional>

#include <flatbuffers/flatbuffers.h>

#include "chat_generated.h"
#include "ecs/components/character_components.h"
#include "ecs/components/guild_component.h"
#include "ecs/components/party_component.h"
#include "game/map/aoi_manager.h"
#include "logic/services/player_presence_service.h"

namespace mir2::game::chat {

namespace {

struct CharacterLookup {
  entt::entity entity = entt::null;
  const ecs::CharacterIdentityComponent* identity = nullptr;
  const ecs::CharacterStateComponent* state = nullptr;
};

uint32_t NowSeconds() {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::seconds>(now).count());
}

std::optional<CharacterLookup> FindCharacterById(entt::registry& registry,
                                                 uint64_t character_id) {
  auto view =
      registry.view<ecs::CharacterIdentityComponent, ecs::CharacterStateComponent>();
  for (auto entity : view) {
    const auto& identity = view.get<ecs::CharacterIdentityComponent>(entity);
    if (identity.id != character_id) {
      continue;
    }

    const auto& state = view.get<ecs::CharacterStateComponent>(entity);
    return CharacterLookup{entity, &identity, &state};
  }
  return std::nullopt;
}

bool IsOnline(const CharacterLookup& lookup) {
  return lookup.state && lookup.state->is_online;
}

bool CanHearWhisper(mir2::logic::PlayerPresenceService& player_presence,
                    entt::registry& registry,
                    entt::entity entity,
                    uint64_t player_id) {
  if (const auto* pref = registry.try_get<ecs::ChatPreferenceComponent>(entity)) {
    return pref->hear_whisper;
  }
  return player_presence.CanHearWhisper(player_id);
}

bool CanHearCry(mir2::logic::PlayerPresenceService& player_presence,
                entt::registry& registry,
                entt::entity entity,
                uint64_t player_id) {
  if (const auto* pref = registry.try_get<ecs::ChatPreferenceComponent>(entity)) {
    return pref->hear_cry;
  }
  return player_presence.CanHearCry(player_id);
}

bool CanHearGuildMessage(mir2::logic::PlayerPresenceService& player_presence,
                         entt::registry& registry,
                         entt::entity entity,
                         uint64_t player_id) {
  if (const auto* pref = registry.try_get<ecs::ChatPreferenceComponent>(entity)) {
    return pref->hear_guild_msg;
  }
  return player_presence.CanHearGuildMessage(player_id);
}

bool IsBlockedBy(mir2::logic::PlayerPresenceService& player_presence,
                 entt::registry& registry,
                 entt::entity owner_entity,
                 uint64_t owner_id,
                 uint64_t target_id) {
  if (const auto* pref = registry.try_get<ecs::ChatPreferenceComponent>(owner_entity)) {
    return pref->IsBlocked(static_cast<uint32_t>(target_id));
  }
  return player_presence.IsBlocked(owner_id, target_id);
}

bool IsDead(mir2::logic::PlayerPresenceService& player_presence,
            entt::registry& registry,
            entt::entity entity,
            uint64_t player_id) {
  if (const auto* attributes =
          registry.try_get<ecs::CharacterAttributesComponent>(entity)) {
    return attributes->hp <= 0;
  }
  return player_presence.IsDead(player_id);
}

uint64_t ResolveCharacterId(entt::registry& registry, entt::entity entity) {
  if (!registry.valid(entity)) {
    return 0;
  }

  if (const auto* identity =
          registry.try_get<ecs::CharacterIdentityComponent>(entity)) {
    return identity->id;
  }

  if (const auto* party_member = registry.try_get<ecs::PartyMemberComponent>(entity);
      party_member && party_member->character_id != 0) {
    return party_member->character_id;
  }

  if (const auto* guild_member = registry.try_get<ecs::GuildMemberComponent>(entity);
      guild_member && guild_member->character_id != 0) {
    return guild_member->character_id;
  }

  return 0;
}

}  // namespace

ChatService::ChatService(mir2::logic::PlayerPresenceService& player_presence,
                         entt::registry& ecs_registry)
    : player_presence_(player_presence),
      ecs_registry_(ecs_registry) {}

std::vector<uint8_t> ChatService::BuildChatMessage(uint8_t channel,
                                                   uint64_t from_id,
                                                   const std::string& from_name,
                                                   uint64_t to_id,
                                                   const std::string& content,
                                                   uint32_t color) {
  flatbuffers::FlatBufferBuilder builder;
  auto name_offset = builder.CreateString(from_name);
  auto content_offset = builder.CreateString(content);
  auto msg = mir2::proto::CreateChatMessage(
      builder, static_cast<mir2::proto::ChatChannel>(channel), from_id,
      name_offset, to_id, content_offset, color, NowSeconds());
  builder.Finish(msg);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

ChatDispatchList ChatService::SendNormalChat(uint64_t sender_id,
                                             const std::string& content,
                                             map::AOIManager& aoi) {
  ChatDispatchList out;
  auto sender = FindCharacterById(ecs_registry_, sender_id);
  if (!sender.has_value() || !IsOnline(*sender)) {
    return out;
  }

  std::string formatted = sender->identity->name + ": " + content;
  auto payload = BuildChatMessage(
      static_cast<uint8_t>(mir2::proto::ChatChannel::WORLD), sender_id,
      sender->identity->name, 0, formatted, ChatColor::kNormal);

  auto targets = aoi.GetEntitiesInViewOf(sender_id);
  targets.push_back(sender_id);  // 包含自己

  out.reserve(targets.size());
  for (auto target_id : targets) {
    out.emplace_back(target_id, payload);
  }
  return out;
}

ChatDispatchList ChatService::SendWhisper(uint64_t sender_id,
                                          uint64_t target_id,
                                          const std::string& content) {
  ChatDispatchList out;
  auto sender = FindCharacterById(ecs_registry_, sender_id);
  auto target = FindCharacterById(ecs_registry_, target_id);
  if (!sender.has_value() || !target.has_value() ||
      !IsOnline(*sender) || !IsOnline(*target)) {
    return out;
  }
  if (!CanHearWhisper(player_presence_, ecs_registry_, target->entity, target_id) ||
      IsBlockedBy(player_presence_, ecs_registry_, target->entity, target_id, sender_id)) {
    return out;
  }

  std::string formatted = sender->identity->name + "=> " + content;
  auto payload = BuildChatMessage(
      static_cast<uint8_t>(mir2::proto::ChatChannel::PRIVATE), sender_id,
      sender->identity->name, target_id, formatted, ChatColor::kWhisper);

  out.reserve(2);
  out.emplace_back(target_id, payload);
  out.emplace_back(sender_id, payload);  // 发送者也收到
  return out;
}

ChatDispatchList ChatService::SendTeamChat(uint64_t sender_id,
                                           const std::string& content) {
  ChatDispatchList out;
  auto sender = FindCharacterById(ecs_registry_, sender_id);
  if (!sender.has_value() || !IsOnline(*sender)) {
    return out;
  }

  auto entity = sender->entity;
  auto* member_comp = ecs_registry_.try_get<ecs::PartyMemberComponent>(entity);
  if (!member_comp) return out;

  // 查找队伍
  entt::entity party_entity = entt::null;
  auto party_view = ecs_registry_.view<ecs::PartyComponent>();
  for (auto e : party_view) {
    auto& party = party_view.get<ecs::PartyComponent>(e);
    if (party.party_id == member_comp->party_id) {
      party_entity = e;
      break;
    }
  }
  if (party_entity == entt::null) return out;

  auto& party = ecs_registry_.get<ecs::PartyComponent>(party_entity);
  auto payload = BuildChatMessage(
      static_cast<uint8_t>(mir2::proto::ChatChannel::TEAM), sender_id,
      sender->identity->name, 0, content, ChatColor::kTeam);

  for (auto member : party.members) {
    if (!ecs_registry_.valid(member)) {
      continue;
    }
    const auto* member_state =
        ecs_registry_.try_get<ecs::CharacterStateComponent>(member);
    if (!member_state || !member_state->is_online) {
      continue;
    }
    const uint64_t member_id = ResolveCharacterId(ecs_registry_, member);
    if (member_id == 0) {
      continue;
    }
    out.emplace_back(member_id, payload);
  }
  return out;
}

ChatDispatchList ChatService::SendAreaChat(uint64_t sender_id,
                                           const std::string& content) {
  ChatDispatchList out;
  auto sender = FindCharacterById(ecs_registry_, sender_id);
  if (!sender.has_value() || !IsOnline(*sender)) {
    return out;
  }

  std::string formatted = "(!) " + sender->identity->name + ": " + content;
  auto payload = BuildChatMessage(
      static_cast<uint8_t>(mir2::proto::ChatChannel::AREA), sender_id,
      sender->identity->name, 0, formatted, ChatColor::kArea);

  auto view =
      ecs_registry_.view<ecs::CharacterIdentityComponent, ecs::CharacterStateComponent>();
  for (auto entity : view) {
    const auto& state = view.get<ecs::CharacterStateComponent>(entity);
    if (!state.is_online || state.map_id != sender->state->map_id) {
      continue;
    }

    const auto& identity = view.get<ecs::CharacterIdentityComponent>(entity);
    const uint64_t target_id = identity.id;
    if (target_id == sender_id ||
        CanHearCry(player_presence_, ecs_registry_, entity, target_id)) {
      out.emplace_back(target_id, payload);
    }
  }
  return out;
}

ChatDispatchList ChatService::SendGuildChat(uint64_t sender_id,
                                            const std::string& content) {
  ChatDispatchList out;
  auto sender = FindCharacterById(ecs_registry_, sender_id);
  if (!sender.has_value() || !IsOnline(*sender)) {
    return out;
  }

  auto entity = sender->entity;
  auto* guild_member = ecs_registry_.try_get<ecs::GuildMemberComponent>(entity);
  if (!guild_member) return out;

  // 查找行会
  entt::entity guild_entity = entt::null;
  auto guild_view = ecs_registry_.view<ecs::GuildComponent>();
  for (auto e : guild_view) {
    auto& guild = guild_view.get<ecs::GuildComponent>(e);
    if (guild.guild_id == guild_member->guild_id) {
      guild_entity = e;
      break;
    }
  }
  if (guild_entity == entt::null) return out;

  auto& guild = ecs_registry_.get<ecs::GuildComponent>(guild_entity);
  auto payload = BuildChatMessage(
      static_cast<uint8_t>(mir2::proto::ChatChannel::GUILD), sender_id,
      sender->identity->name, 0, content, ChatColor::kGuild);

  for (auto member : guild.members) {
    if (!ecs_registry_.valid(member)) {
      continue;
    }
    const auto* member_state =
        ecs_registry_.try_get<ecs::CharacterStateComponent>(member);
    if (!member_state || !member_state->is_online) {
      continue;
    }
    const auto member_id = ResolveCharacterId(ecs_registry_, member);
    if (member_id == 0) {
      continue;
    }

    if (CanHearGuildMessage(player_presence_, ecs_registry_, member, member_id)) {
      out.emplace_back(member_id, payload);
    }
  }
  return out;
}

ChatDispatchList ChatService::SendSystemMessage(uint64_t player_id,
                                                const std::string& content,
                                                uint8_t mode) {
  ChatDispatchList out;
  uint32_t color = (mode == 2) ? ChatColor::kSystemBlue : ChatColor::kSystemYellow;
  auto payload = BuildChatMessage(
      static_cast<uint8_t>(mir2::proto::ChatChannel::SYSTEM), 0, "System", 0,
      content, color);

  out.emplace_back(player_id, payload);
  return out;
}

ChatDispatchList ChatService::BroadcastSystemMessage(const std::string& content) {
  ChatDispatchList out;
  auto payload = BuildChatMessage(
      static_cast<uint8_t>(mir2::proto::ChatChannel::SYSTEM), 0, "System", 0,
      content, ChatColor::kSystemYellow);

  auto view =
      ecs_registry_.view<ecs::CharacterIdentityComponent, ecs::CharacterStateComponent>();
  for (auto entity : view) {
    const auto& state = view.get<ecs::CharacterStateComponent>(entity);
    if (!state.is_online) {
      continue;
    }

    const auto& identity = view.get<ecs::CharacterIdentityComponent>(entity);
    const uint64_t player_id = identity.id;
    if (!IsDead(player_presence_, ecs_registry_, entity, player_id)) {
      out.emplace_back(player_id, payload);
    }
  }
  return out;
}

}  // namespace mir2::game::chat
