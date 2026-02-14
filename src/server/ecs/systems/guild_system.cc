#include "ecs/systems/guild_system.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/utils.h"
#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/guild_events.h"

namespace mir2::ecs {

namespace {

constexpr uint32_t kGuildCreateItemId = 0;

std::string GetEntityName(entt::registry& registry, entt::entity entity) {
    auto* identity = registry.try_get<CharacterIdentityComponent>(entity);
    if (!identity) {
        return {};
    }
    return identity->name;
}

CharacterId ResolveCharacterId(const CharacterIdentityComponent& identity,
                               const GuildMemberComponent* member) {
    if (member && member->character_id != kInvalidCharacterId) {
        return member->character_id;
    }
    return identity.id;
}

entt::entity FindGuildCreateItem(entt::registry& registry, entt::entity player) {
    if (kGuildCreateItemId == 0) {
        return entt::null;
    }

    auto view = registry.view<InventoryOwnerComponent, ItemComponent>();
    for (auto item_entity : view) {
        const auto& owner = view.get<InventoryOwnerComponent>(item_entity);
        if (owner.owner != player || owner.slot_index < 0) {
            continue;
        }

        const auto& item = view.get<ItemComponent>(item_entity);
        if (item.item_id == kGuildCreateItemId && item.count > 0) {
            return item_entity;
        }
    }

    return entt::null;
}

void ConsumeGuildCreateItem(entt::registry& registry, entt::entity player,
                            entt::entity item_entity) {
    if (item_entity == entt::null || !registry.valid(item_entity)) {
        return;
    }

    auto* owner = registry.try_get<InventoryOwnerComponent>(item_entity);
    auto* item = registry.try_get<ItemComponent>(item_entity);
    if (!owner || !item || owner->owner != player || owner->slot_index < 0) {
        return;
    }

    if (item->count <= 0) {
        return;
    }

    item->count -= 1;
    dirty_tracker::mark_items_dirty(registry, player);

    if (item->count == 0) {
        registry.destroy(item_entity);
    }
}

uint64_t MakeGuildPairKey(GuildId guild_id_a, GuildId guild_id_b) {
    const GuildId min_id = std::min(guild_id_a, guild_id_b);
    const GuildId max_id = std::max(guild_id_a, guild_id_b);
    return (static_cast<uint64_t>(min_id) << 32) | max_id;
}

bool IsFightMember(const GuildComponent& guild, entt::entity member) {
    return std::find(guild.fight_members.begin(), guild.fight_members.end(),
                     member) != guild.fight_members.end();
}

}  // namespace

GuildSystem::GuildSystem(EventBus& event_bus, game::guild::GuildManager& guild_mgr)
    : System(SystemPriority::kInventory),
      event_bus_(event_bus),
      guild_mgr_(guild_mgr) {}

void GuildSystem::Update(entt::registry& registry, float delta_time) {
    war_check_timer_ += delta_time;
    if (war_check_timer_ < WAR_CHECK_INTERVAL) {
        return;
    }

    war_check_timer_ = 0.0f;
    CheckWarTimeout(registry);
}

int GuildSystem::CreateGuild(entt::registry& registry, entt::entity player,
                             const std::string& guild_name) {
    if (!registry.valid(player)) {
        return -1;
    }

    auto* member = registry.try_get<GuildMemberComponent>(player);
    if (member && member->guild_id != 0) {
        return -1;
    }

    if (guild_name.empty()) {
        return -4;
    }

    auto* attributes = registry.try_get<CharacterAttributesComponent>(player);
    if (!attributes || attributes->gold < static_cast<int>(GUILD_CREATE_FEE)) {
        return -2;
    }

    if (guild_mgr_.GetGuildByName(guild_name) != entt::null) {
        return -4;
    }

    entt::entity required_item = FindGuildCreateItem(registry, player);
    if (kGuildCreateItemId != 0 && required_item == entt::null) {
        return -3;
    }

    const uint32_t guild_id = guild_mgr_.GenerateGuildId();
    entt::entity guild_entity =
        guild_mgr_.CreateGuild(guild_id, guild_name, player, registry);
    if (guild_entity == entt::null) {
        return -1;
    }

    auto* leader_member = registry.try_get<GuildMemberComponent>(player);
    if (!leader_member || leader_member->guild_id != guild_id ||
        leader_member->rank != GUILD_RANK_LEADER) {
        GuildMemberComponent new_member;
        new_member.guild_id = guild_id;
        new_member.rank = GUILD_RANK_LEADER;
        if (const auto* identity =
                registry.try_get<CharacterIdentityComponent>(player)) {
            new_member.character_id = identity->id;
        }
        registry.emplace_or_replace<GuildMemberComponent>(player, new_member);
    }

    attributes->gold -= static_cast<int>(GUILD_CREATE_FEE);
    dirty_tracker::mark_attributes_dirty(registry, player);

    if (required_item != entt::null) {
        ConsumeGuildCreateItem(registry, player, required_item);
    }

    events::GuildCreatedEvent event;
    event.guild_id = guild_id;
    event.leader = player;
    event.guild_name = guild_name;
    event_bus_.Publish(event);

    return 0;
}

bool GuildSystem::JoinGuild(entt::registry& registry, entt::entity player,
                            GuildId guild_id) {
    if (!registry.valid(player) || guild_id == kInvalidGuildId) {
        return false;
    }

    auto* member = registry.try_get<GuildMemberComponent>(player);
    if (member && member->guild_id != 0) {
        return false;
    }

    auto* guild = guild_mgr_.GetGuild(guild_id, registry);
    if (!guild) {
        return false;
    }

    if (guild->IsFull()) {
        return false;
    }

    if (!guild_mgr_.AddMember(guild_id, player, registry)) {
        return false;
    }

    if (auto* joined_member = registry.try_get<GuildMemberComponent>(player)) {
        joined_member->guild_id = guild_id;
        joined_member->rank = GUILD_RANK_MEMBER;
        if (const auto* identity =
                registry.try_get<CharacterIdentityComponent>(player)) {
            joined_member->character_id = identity->id;
        }
    }

    events::GuildMemberJoinedEvent event;
    event.guild_id = guild_id;
    event.member = player;
    event.member_name = GetEntityName(registry, player);
    event_bus_.Publish(event);

    return true;
}

bool GuildSystem::LeaveGuild(entt::registry& registry, entt::entity player) {
    if (!registry.valid(player)) {
        return false;
    }

    auto* member = registry.try_get<GuildMemberComponent>(player);
    if (!member || member->guild_id == kInvalidGuildId) {
        return false;
    }

    const uint32_t guild_id = member->guild_id;
    const std::string member_name = GetEntityName(registry, player);

    auto* guild = guild_mgr_.GetGuild(guild_id, registry);
    if (!guild) {
        return false;
    }

    if (guild->IsLeader(player)) {
        return false;
    }

    if (!guild_mgr_.RemoveMember(guild_id, player, registry)) {
        return false;
    }

    if (registry.try_get<GuildMemberComponent>(player)) {
        registry.remove<GuildMemberComponent>(player);
    }

    events::GuildMemberLeftEvent event;
    event.guild_id = guild_id;
    event.member = player;
    event.member_name = member_name;
    event.is_kicked = false;
    event_bus_.Publish(event);

    if (auto* guild = guild_mgr_.GetGuild(guild_id, registry)) {
        if (guild->members.empty()) {
            guild_mgr_.DeleteGuild(guild_id, registry);
        }
    }

    return true;
}

int GuildSystem::DissolveGuild(entt::registry& registry, entt::entity player) {
    if (!registry.valid(player)) {
        return -1;
    }

    auto* member = registry.try_get<GuildMemberComponent>(player);
    if (!member || member->guild_id == kInvalidGuildId) {
        return -1;
    }

    auto* guild = guild_mgr_.GetGuild(member->guild_id, registry);
    if (!guild) {
        return -1;
    }

    if (!guild->IsLeader(player) && member->rank != GUILD_RANK_LEADER) {
        return -1;
    }

    if (guild->members.size() > 1) {
        return -2;
    }

    const uint32_t guild_id = guild->guild_id;
    for (const auto& guild_member : guild->members) {
        if (!registry.valid(guild_member)) {
            continue;
        }
        auto* member_comp = registry.try_get<GuildMemberComponent>(guild_member);
        if (member_comp && member_comp->guild_id == guild_id) {
            registry.remove<GuildMemberComponent>(guild_member);
        }
    }

    if (!guild_mgr_.DeleteGuild(guild_id, registry)) {
        return -1;
    }

    return 0;
}

bool GuildSystem::KickMember(entt::registry& registry, entt::entity kicker,
                             entt::entity target) {
    if (!registry.valid(kicker) || !registry.valid(target)) {
        return false;
    }

    if (kicker == target) {
        return false;
    }

    auto* kicker_member = registry.try_get<GuildMemberComponent>(kicker);
    auto* target_member = registry.try_get<GuildMemberComponent>(target);
    if (!kicker_member || !target_member) {
        return false;
    }

    if (kicker_member->guild_id == 0 ||
        kicker_member->guild_id != target_member->guild_id) {
        return false;
    }

    auto* guild = guild_mgr_.GetGuild(kicker_member->guild_id, registry);
    if (!guild || !guild->IsLeader(kicker)) {
        return false;
    }

    if (guild->IsLeader(target)) {
        return false;
    }

    const uint32_t guild_id = kicker_member->guild_id;
    const std::string target_name = GetEntityName(registry, target);

    if (!guild_mgr_.RemoveMember(guild_id, target, registry)) {
        return false;
    }

    events::GuildMemberLeftEvent event;
    event.guild_id = guild_id;
    event.member = target;
    event.member_name = target_name;
    event.is_kicked = true;
    event_bus_.Publish(event);

    return true;
}

int GuildSystem::TransferLeadership(entt::registry& registry,
                                    entt::entity current_leader,
                                    entt::entity new_leader) {
    if (!registry.valid(current_leader)) {
        return -1;
    }

    auto* current_member = registry.try_get<GuildMemberComponent>(current_leader);
    if (!current_member || current_member->guild_id == kInvalidGuildId) {
        return -1;
    }

    auto* guild = guild_mgr_.GetGuild(current_member->guild_id, registry);
    if (!guild || !guild->IsLeader(current_leader)) {
        return -1;
    }

    if (!registry.valid(new_leader)) {
        return -2;
    }

    auto* new_member = registry.try_get<GuildMemberComponent>(new_leader);
    if (!new_member || new_member->guild_id != current_member->guild_id) {
        return -2;
    }

    if (!guild->IsMember(new_leader)) {
        return -2;
    }

    guild->leader = new_leader;

    if (current_leader != new_leader) {
        current_member->rank = GUILD_RANK_MEMBER;
    }
    new_member->rank = GUILD_RANK_LEADER;

    return 0;
}

int GuildSystem::UpdateRankStructure(entt::registry& registry,
                                     entt::entity player,
                                     const std::vector<GuildRank>& new_ranks) {
    if (!registry.valid(player)) {
        return -7;
    }

    auto* member = registry.try_get<GuildMemberComponent>(player);
    if (!member || member->guild_id == kInvalidGuildId) {
        return -7;
    }

    auto* guild = guild_mgr_.GetGuild(member->guild_id, registry);
    if (!guild) {
        return -7;
    }

    if (member->rank != GUILD_RANK_LEADER && !guild->IsLeader(player)) {
        return -7;
    }

    bool has_leader_rank = false;
    bool leader_name_empty = false;
    bool invalid_rank = false;
    bool member_mismatch = false;
    std::unordered_set<uint8_t> seen_ranks;
    std::unordered_set<CharacterId> guild_member_ids;
    std::unordered_map<CharacterId, std::string> guild_member_names;
    std::unordered_map<std::string, CharacterId> guild_member_name_to_id;

    auto member_view = registry.view<CharacterIdentityComponent, GuildMemberComponent>();
    for (auto entity : member_view) {
        auto& member_comp = member_view.get<GuildMemberComponent>(entity);
        if (member_comp.guild_id != guild->guild_id) {
            continue;
        }

        const auto& identity = member_view.get<CharacterIdentityComponent>(entity);
        const CharacterId character_id = ResolveCharacterId(identity, &member_comp);
        if (character_id == kInvalidCharacterId) {
            member_mismatch = true;
            continue;
        }

        member_comp.character_id = character_id;
        if (!guild_member_ids.insert(character_id).second) {
            member_mismatch = true;
            continue;
        }

        if (!identity.name.empty()) {
            const auto inserted =
                guild_member_name_to_id.emplace(identity.name, character_id);
            if (!inserted.second && inserted.first->second != character_id) {
                member_mismatch = true;
            }
            guild_member_names[character_id] = identity.name;
        }
    }

    std::unordered_set<CharacterId> new_member_ids;
    std::unordered_set<CharacterId> leader_ids;
    std::vector<GuildRank> canonical_ranks;
    canonical_ranks.reserve(new_ranks.size());

    for (const auto& rank : new_ranks) {
        if (rank.rank < GUILD_RANK_LEADER || rank.rank > GUILD_RANK_MEMBER) {
            invalid_rank = true;
        }
        if (!seen_ranks.insert(rank.rank).second) {
            invalid_rank = true;
        }

        if (rank.rank == GUILD_RANK_LEADER) {
            has_leader_rank = true;
            if (rank.rank_name.empty()) {
                leader_name_empty = true;
            }
        }

        GuildRank canonical_rank;
        canonical_rank.rank = rank.rank;
        canonical_rank.rank_name = rank.rank_name;
        std::unordered_set<CharacterId> rank_member_ids;
        rank_member_ids.reserve(rank.member_ids.size() + rank.member_names.size());

        auto add_rank_member = [&](CharacterId character_id) {
            if (character_id == kInvalidCharacterId ||
                guild_member_ids.find(character_id) == guild_member_ids.end()) {
                member_mismatch = true;
                return;
            }

            if (!rank_member_ids.insert(character_id).second) {
                member_mismatch = true;
                return;
            }

            canonical_rank.member_ids.push_back(character_id);
            if (const auto name_it = guild_member_names.find(character_id);
                name_it != guild_member_names.end()) {
                canonical_rank.member_names.push_back(name_it->second);
            }

            if (!new_member_ids.insert(character_id).second) {
                member_mismatch = true;
            }
            if (rank.rank == GUILD_RANK_LEADER) {
                leader_ids.insert(character_id);
            }
        };

        for (CharacterId character_id : rank.member_ids) {
            add_rank_member(character_id);
        }

        for (const auto& name : rank.member_names) {
            if (name.empty()) {
                member_mismatch = true;
                continue;
            }
            const auto name_it = guild_member_name_to_id.find(name);
            if (name_it == guild_member_name_to_id.end()) {
                member_mismatch = true;
                continue;
            }
            add_rank_member(name_it->second);
        }

        canonical_ranks.push_back(std::move(canonical_rank));
    }

    if (!has_leader_rank) {
        return -2;
    }
    if (leader_name_empty) {
        return -3;
    }
    if (leader_ids.size() > 2) {
        return -4;
    }

    if (leader_ids.empty()) {
        return -5;
    }

    if (guild_member_ids.size() != new_member_ids.size()) {
        member_mismatch = true;
    } else {
        for (CharacterId character_id : guild_member_ids) {
            if (new_member_ids.find(character_id) == new_member_ids.end()) {
                member_mismatch = true;
                break;
            }
        }
    }

    if (member_mismatch) {
        return -6;
    }
    if (invalid_rank) {
        return -7;
    }

    guild->ranks = std::move(canonical_ranks);

    std::unordered_map<CharacterId, std::pair<uint8_t, std::string>> rank_lookup;
    rank_lookup.reserve(new_member_ids.size());
    for (const auto& rank : guild->ranks) {
        for (CharacterId character_id : rank.member_ids) {
            rank_lookup[character_id] = std::make_pair(rank.rank, rank.rank_name);
        }
    }

    for (auto entity : member_view) {
        auto& member_comp = member_view.get<GuildMemberComponent>(entity);
        if (member_comp.guild_id != guild->guild_id) {
            continue;
        }

        const auto& identity = member_view.get<CharacterIdentityComponent>(entity);
        const CharacterId character_id = ResolveCharacterId(identity, &member_comp);
        if (character_id == kInvalidCharacterId) {
            continue;
        }
        member_comp.character_id = character_id;

        auto it = rank_lookup.find(character_id);
        if (it == rank_lookup.end()) {
            continue;
        }

        member_comp.rank = it->second.first;
        member_comp.rank_name = it->second.second;
    }

    return 0;
}

bool GuildSystem::SetMemberRank(entt::registry& registry,
                                entt::entity operator_entity,
                                const std::string& member_name,
                                uint8_t new_rank) {
    if (!registry.valid(operator_entity) || member_name.empty()) {
        return false;
    }

    auto* operator_member = registry.try_get<GuildMemberComponent>(operator_entity);
    if (!operator_member || operator_member->guild_id == kInvalidGuildId) {
        return false;
    }

    auto* guild = guild_mgr_.GetGuild(operator_member->guild_id, registry);
    if (!guild) {
        return false;
    }

    if (operator_member->rank != GUILD_RANK_LEADER &&
        !guild->IsLeader(operator_entity)) {
        return false;
    }

    if (new_rank < GUILD_RANK_LEADER || new_rank > GUILD_RANK_MEMBER) {
        return false;
    }

    auto rank_it =
        std::find_if(guild->ranks.begin(), guild->ranks.end(),
                     [new_rank](const GuildRank& rank) {
                         return rank.rank == new_rank;
                     });
    if (rank_it == guild->ranks.end()) {
        return false;
    }

    entt::entity target = entt::null;
    CharacterId target_character_id = kInvalidCharacterId;
    auto view = registry.view<CharacterIdentityComponent, GuildMemberComponent>();
    for (auto entity : view) {
        const auto& member_comp = view.get<GuildMemberComponent>(entity);
        if (member_comp.guild_id != operator_member->guild_id) {
            continue;
        }

        const auto& identity = view.get<CharacterIdentityComponent>(entity);
        if (identity.name == member_name) {
            target = entity;
            target_character_id = ResolveCharacterId(identity, &member_comp);
            break;
        }
    }

    if (target == entt::null || target_character_id == kInvalidCharacterId) {
        return false;
    }

    for (auto& rank : guild->ranks) {
        auto& member_ids = rank.member_ids;
        auto id_it =
            std::remove(member_ids.begin(), member_ids.end(), target_character_id);
        if (id_it != member_ids.end()) {
            member_ids.erase(id_it, member_ids.end());
        }

        auto& names = rank.member_names;
        auto name_it = std::remove(names.begin(), names.end(), member_name);
        if (name_it != names.end()) {
            names.erase(name_it, names.end());
        }
    }

    if (std::find(rank_it->member_ids.begin(), rank_it->member_ids.end(),
                  target_character_id) == rank_it->member_ids.end()) {
        rank_it->member_ids.push_back(target_character_id);
    }
    if (std::find(rank_it->member_names.begin(), rank_it->member_names.end(),
                  member_name) == rank_it->member_names.end()) {
        rank_it->member_names.push_back(member_name);
    }

    auto* target_member = registry.try_get<GuildMemberComponent>(target);
    if (!target_member) {
        return false;
    }

    target_member->character_id = target_character_id;
    target_member->rank = new_rank;
    target_member->rank_name = rank_it->rank_name;

    return true;
}

int GuildSystem::DeclareWar(entt::registry& registry, entt::entity player,
                            GuildId target_guild_id) {
    if (!registry.valid(player) || target_guild_id == kInvalidGuildId) {
        return -3;
    }

    auto* member = registry.try_get<GuildMemberComponent>(player);
    if (!member || member->guild_id == 0 ||
        member->rank != GUILD_RANK_LEADER) {
        return -1;
    }

    auto* attributes = registry.try_get<CharacterAttributesComponent>(player);
    if (!attributes || attributes->gold < static_cast<int>(GUILD_WAR_FEE)) {
        return -2;
    }

    if (guild_mgr_.GetGuildEntity(target_guild_id) == entt::null) {
        return -3;
    }

    if (guild_mgr_.IsAllied(member->guild_id, target_guild_id)) {
        return -4;
    }

    if (guild_mgr_.IsAtWar(member->guild_id, target_guild_id)) {
        return 0;
    }

    if (!guild_mgr_.DeclareWar(member->guild_id, target_guild_id, registry)) {
        return -3;
    }

    attributes->gold -= static_cast<int>(GUILD_WAR_FEE);
    dirty_tracker::mark_attributes_dirty(registry, player);

    events::GuildWarDeclaredEvent event;
    event.attacker_guild_id = member->guild_id;
    event.target_guild_id = target_guild_id;
    event.duration_ms = GUILD_WAR_DURATION;
    event_bus_.Publish(event);

    return 0;
}

bool GuildSystem::CancelWar(entt::registry& registry, entt::entity player,
                            GuildId target_guild_id) {
    if (!registry.valid(player) || target_guild_id == kInvalidGuildId) {
        return false;
    }

    auto* member = registry.try_get<GuildMemberComponent>(player);
    if (!member || member->guild_id == 0 ||
        member->rank != GUILD_RANK_LEADER) {
        return false;
    }

    if (!guild_mgr_.CancelWar(member->guild_id, target_guild_id, registry)) {
        return false;
    }

    events::GuildWarEndedEvent event;
    event.guild1_id = member->guild_id;
    event.guild2_id = target_guild_id;
    event.is_timeout = false;
    event_bus_.Publish(event);

    return true;
}

int GuildSystem::MakeAlliance(entt::registry& registry, entt::entity player,
                              GuildId target_guild_id) {
    if (!registry.valid(player)) {
        return -1;
    }

    auto* member = registry.try_get<GuildMemberComponent>(player);
    if (!member || member->guild_id == kInvalidGuildId) {
        return -1;
    }

    auto* guild = guild_mgr_.GetGuild(member->guild_id, registry);
    if (!guild || !guild->IsLeader(player)) {
        return -1;
    }

    if (target_guild_id == kInvalidGuildId || target_guild_id == member->guild_id) {
        return -2;
    }

    auto* target_guild = guild_mgr_.GetGuild(target_guild_id, registry);
    if (!target_guild) {
        return -2;
    }

    if (guild_mgr_.IsAtWar(member->guild_id, target_guild_id)) {
        return -3;
    }

    if (!target_guild->allow_ally) {
        return -4;
    }

    bool alliance_added = false;
    if (!guild->IsAlliedWith(target_guild_id)) {
        guild->ally_guild_ids.push_back(target_guild_id);
        alliance_added = true;
    }
    if (!target_guild->IsAlliedWith(member->guild_id)) {
        target_guild->ally_guild_ids.push_back(member->guild_id);
        alliance_added = true;
    }

    if (alliance_added) {
        events::GuildAllianceFormedEvent event;
        event.guild1_id = member->guild_id;
        event.guild2_id = target_guild_id;
        event_bus_.Publish(event);
    }

    return 0;
}

bool GuildSystem::BreakAlliance(entt::registry& registry, entt::entity player,
                                GuildId target_guild_id) {
    if (!registry.valid(player) || target_guild_id == kInvalidGuildId) {
        return false;
    }

    auto* member = registry.try_get<GuildMemberComponent>(player);
    if (!member || member->guild_id == kInvalidGuildId) {
        return false;
    }

    auto* guild = guild_mgr_.GetGuild(member->guild_id, registry);
    if (!guild || !guild->IsLeader(player)) {
        return false;
    }

    if (!guild_mgr_.BreakAlliance(member->guild_id, target_guild_id, registry)) {
        return false;
    }

    events::GuildAllianceBrokenEvent event;
    event.guild1_id = member->guild_id;
    event.guild2_id = target_guild_id;
    event_bus_.Publish(event);

    return true;
}

bool GuildSystem::UpdateNotice(entt::registry& registry, entt::entity player,
                               const std::vector<std::string>& notice_lines) {
    if (!registry.valid(player)) {
        return false;
    }

    auto* member = registry.try_get<GuildMemberComponent>(player);
    if (!member || member->guild_id == 0 ||
        member->rank > GUILD_RANK_VICE_LEADER) {
        return false;
    }

    auto* guild = guild_mgr_.GetGuild(member->guild_id, registry);
    if (!guild) {
        return false;
    }

    guild->notice_list = notice_lines;
    return true;
}

std::vector<std::string> GuildSystem::GetNotice(entt::registry& registry,
                                                entt::entity player) {
    if (!registry.valid(player)) {
        return {};
    }

    auto* member = registry.try_get<GuildMemberComponent>(player);
    if (!member || member->guild_id == kInvalidGuildId) {
        return {};
    }

    auto* guild = guild_mgr_.GetGuild(member->guild_id, registry);
    if (!guild) {
        return {};
    }

    return guild->notice_list;
}

bool GuildSystem::StartTeamFight(entt::registry& registry, entt::entity player) {
    if (!registry.valid(player)) {
        return false;
    }

    auto* member = registry.try_get<GuildMemberComponent>(player);
    if (!member || member->guild_id == kInvalidGuildId) {
        return false;
    }

    auto* guild = guild_mgr_.GetGuild(member->guild_id, registry);
    if (!guild) {
        return false;
    }

    if (!guild->IsLeader(player) && member->rank != GUILD_RANK_LEADER) {
        return false;
    }

    guild->in_team_fight = true;
    guild->match_point = 0;
    guild->fight_members.clear();
    return true;
}

bool GuildSystem::EndTeamFight(entt::registry& registry, entt::entity player) {
    if (!registry.valid(player)) {
        return false;
    }

    auto* member = registry.try_get<GuildMemberComponent>(player);
    if (!member || member->guild_id == kInvalidGuildId) {
        return false;
    }

    auto* guild = guild_mgr_.GetGuild(member->guild_id, registry);
    if (!guild) {
        return false;
    }

    if (!guild->IsLeader(player) && member->rank != GUILD_RANK_LEADER) {
        return false;
    }

    guild->in_team_fight = false;
    return true;
}

bool GuildSystem::JoinTeamFight(entt::registry& registry, entt::entity player) {
    if (!registry.valid(player)) {
        return false;
    }

    auto* member = registry.try_get<GuildMemberComponent>(player);
    if (!member || member->guild_id == kInvalidGuildId) {
        return false;
    }

    auto* guild = guild_mgr_.GetGuild(member->guild_id, registry);
    if (!guild || !guild->in_team_fight) {
        return false;
    }

    if (!IsFightMember(*guild, player)) {
        guild->fight_members.push_back(player);
    }

    return true;
}

void GuildSystem::RecordTeamFightKill(entt::registry& registry,
                                      entt::entity killer,
                                      entt::entity victim) {
    if (!registry.valid(killer) || !registry.valid(victim)) {
        return;
    }

    if (killer == victim) {
        return;
    }

    auto* killer_member = registry.try_get<GuildMemberComponent>(killer);
    auto* victim_member = registry.try_get<GuildMemberComponent>(victim);
    if (!killer_member || !victim_member) {
        return;
    }

    if (killer_member->guild_id == 0 || victim_member->guild_id == kInvalidGuildId) {
        return;
    }

    if (killer_member->guild_id == victim_member->guild_id) {
        return;
    }

    auto* killer_guild = guild_mgr_.GetGuild(killer_member->guild_id, registry);
    auto* victim_guild = guild_mgr_.GetGuild(victim_member->guild_id, registry);
    if (!killer_guild || !victim_guild) {
        return;
    }

    if (!killer_guild->in_team_fight || !victim_guild->in_team_fight) {
        return;
    }

    if (!IsFightMember(*killer_guild, killer) ||
        !IsFightMember(*victim_guild, victim)) {
        return;
    }

    killer_guild->match_point += 1;
}

void GuildSystem::CheckWarTimeout(entt::registry& registry) {
    const uint64_t now = static_cast<uint64_t>(core::GetCurrentTimestampMs());
    std::vector<std::pair<uint32_t, uint32_t>> expired;
    std::unordered_set<uint64_t> seen_pairs;

    auto view = registry.view<GuildComponent>();
    for (auto entity : view) {
        auto& guild = view.get<GuildComponent>(entity);
        for (auto& war : guild.war_guilds) {
            if (guild.guild_id == 0 || war.enemy_guild_id == kInvalidGuildId) {
                continue;
            }

            if (war.start_time == 0) {
                war.start_time = now;
                war.remain_time = GUILD_WAR_DURATION;
                continue;
            }

            if (now <= war.start_time) {
                war.remain_time = GUILD_WAR_DURATION;
                continue;
            }

            const uint64_t elapsed = now - war.start_time;
            if (elapsed >= GUILD_WAR_DURATION) {
                const uint64_t key = MakeGuildPairKey(guild.guild_id, war.enemy_guild_id);
                if (seen_pairs.insert(key).second) {
                    expired.emplace_back(std::min(guild.guild_id, war.enemy_guild_id),
                                         std::max(guild.guild_id, war.enemy_guild_id));
                }
            } else {
                war.remain_time = GUILD_WAR_DURATION - elapsed;
            }
        }
    }

    for (const auto& pair : expired) {
        if (!guild_mgr_.CancelWar(pair.first, pair.second, registry)) {
            continue;
        }

        events::GuildWarEndedEvent event;
        event.guild1_id = pair.first;
        event.guild2_id = pair.second;
        event.is_timeout = true;
        event_bus_.Publish(event);
    }
}

}  // namespace mir2::ecs
