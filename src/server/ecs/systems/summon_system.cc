#include "ecs/systems/summon_system.h"

#include "ecs/components/character_components.h"
#include "ecs/components/combat_component.h"
#include "ecs/components/skill_component.h"

#include <algorithm>
#include <vector>

namespace mir2::ecs {

namespace {

bool is_valid_entity(const entt::registry& registry, entt::entity entity) {
    return entity != entt::null && registry.valid(entity);
}

int resolve_skill_level(entt::registry& registry, entt::entity summoner, uint32_t skill_id) {
    auto* skill_list = registry.try_get<SkillListComponent>(summoner);
    if (!skill_list) {
        return -1;
    }

    const auto* learned = skill_list->get_skill(skill_id);
    if (!learned) {
        return -1;
    }

    return static_cast<int>(learned->level);
}

void prune_summons(entt::registry& registry, entt::entity summoner, SummonerComponent& summoner_comp) {
    auto& list = summoner_comp.summons;
    list.erase(
        std::remove_if(
            list.begin(),
            list.end(),
            [&](entt::entity summon) {
                if (!is_valid_entity(registry, summon)) {
                    return true;
                }
                const auto* summon_comp = registry.try_get<SummonComponent>(summon);
                return !summon_comp || summon_comp->owner != summoner;
            }),
        list.end());
}

}  // namespace

SummonSystem::SummonSystem(entt::registry& registry)
    : registry_(registry) {}

mir2::common::ErrorCode SummonSystem::summon_creature(entt::entity summoner, uint32_t skill_id,
                                                 const mir2::common::Position& pos) {
    if (!is_valid_entity(registry_, summoner)) {
        return mir2::common::ErrorCode::INVALID_ACTION;
    }

    auto& summoner_comp = registry_.get_or_emplace<SummonerComponent>(summoner);
    prune_summons(registry_, summoner, summoner_comp);

    if (summoner_comp.max_summons <= 0) {
        return mir2::common::ErrorCode::INVALID_ACTION;
    }

    if (static_cast<int>(summoner_comp.summons.size()) >= summoner_comp.max_summons) {
        return mir2::common::ErrorCode::INVALID_ACTION;
    }

    const int skill_level = resolve_skill_level(registry_, summoner, skill_id);
    if (skill_level < 0) {
        return mir2::common::ErrorCode::SKILL_NOT_LEARNED;
    }

    // 根据技能ID选择召唤物类型
    // 技能ID 28: 召唤骷髅, 29: 召唤神兽
    entt::entity summon = entt::null;
    if (skill_id == 29) {
        summon = create_elf_monster(summoner, skill_level, pos);
    } else {
        summon = create_skeleton(summoner, skill_level, pos);
    }

    if (!is_valid_entity(registry_, summon)) {
        return mir2::common::ErrorCode::INTERNAL_ERROR;
    }

    auto& summon_comp = registry_.emplace<SummonComponent>(summon);
    summon_comp.owner = summoner;
    summon_comp.skill_id = skill_id;
    summon_comp.skill_level = static_cast<uint8_t>(std::max(0, skill_level));
    summon_comp.summon_time_ms = current_time_ms_;

    // 设置忠诚度过期时间（10分钟 = 600000ms）
    summon_comp.loyalty_expire_time_ms = current_time_ms_ + 600000;

    summoner_comp.summons.push_back(summon);

    return mir2::common::ErrorCode::SUCCESS;
}

void SummonSystem::dismiss_summon(entt::entity summoner, entt::entity summon) {
    if (!is_valid_entity(registry_, summoner)) {
        return;
    }

    auto* summoner_comp = registry_.try_get<SummonerComponent>(summoner);
    if (!summoner_comp) {
        return;
    }

    auto& list = summoner_comp->summons;
    list.erase(std::remove(list.begin(), list.end(), summon), list.end());

    if (!is_valid_entity(registry_, summon)) {
        return;
    }

    const auto* summon_comp = registry_.try_get<SummonComponent>(summon);
    if (summon_comp && summon_comp->owner == summoner) {
        registry_.destroy(summon);
    }
}

void SummonSystem::dismiss_all_summons(entt::entity summoner) {
    if (!is_valid_entity(registry_, summoner)) {
        return;
    }

    auto* summoner_comp = registry_.try_get<SummonerComponent>(summoner);
    if (!summoner_comp) {
        return;
    }

    const auto summons = summoner_comp->summons;
    summoner_comp->summons.clear();

    for (auto summon : summons) {
        if (!is_valid_entity(registry_, summon)) {
            continue;
        }
        const auto* summon_comp = registry_.try_get<SummonComponent>(summon);
        if (summon_comp && summon_comp->owner == summoner) {
            registry_.destroy(summon);
        }
    }
}

void SummonSystem::update(int64_t current_time_ms) {
    current_time_ms_ = current_time_ms;

    // 检查忠诚度过期
    check_loyalty_expiration();

    auto summoner_view = registry_.view<SummonerComponent>();
    for (auto summoner : summoner_view) {
        auto& summoner_comp = summoner_view.get<SummonerComponent>(summoner);
        prune_summons(registry_, summoner, summoner_comp);
    }

    std::vector<entt::entity> to_destroy;
    auto summon_view = registry_.view<SummonComponent>();
    for (auto summon : summon_view) {
        const auto& summon_comp = summon_view.get<SummonComponent>(summon);
        if (!is_valid_entity(registry_, summon_comp.owner)) {
            to_destroy.push_back(summon);
        }
    }

    for (auto summon : to_destroy) {
        if (registry_.valid(summon)) {
            registry_.destroy(summon);
        }
    }
}

void SummonSystem::check_loyalty_expiration() {
    std::vector<entt::entity> expired_summons;

    auto summon_view = registry_.view<SummonComponent>();
    for (auto summon : summon_view) {
        const auto& summon_comp = summon_view.get<SummonComponent>(summon);

        // 检查忠诚度是否过期（如果设置了过期时间）
        if (summon_comp.loyalty_expire_time_ms > 0 &&
            current_time_ms_ >= summon_comp.loyalty_expire_time_ms) {
            expired_summons.push_back(summon);
        }
    }

    // 解散过期的召唤物
    for (auto summon : expired_summons) {
        const auto* summon_comp = registry_.try_get<SummonComponent>(summon);
        if (summon_comp && is_valid_entity(registry_, summon_comp->owner)) {
            dismiss_summon(summon_comp->owner, summon);
        }
    }
}

entt::entity SummonSystem::create_skeleton(entt::entity owner, int skill_level,
                                           const mir2::common::Position& pos) {
    entt::entity summon = registry_.create();

    auto& state = registry_.emplace<CharacterStateComponent>(summon);
    if (const auto* owner_state = registry_.try_get<CharacterStateComponent>(owner)) {
        state.map_id = owner_state->map_id;
        state.direction = owner_state->direction;
    } else {
        state.map_id = 1;
        state.direction = mir2::common::Direction::DOWN;
    }
    state.position = pos;

    auto& attributes = registry_.emplace<CharacterAttributesComponent>(summon);
    const int level = std::max(0, skill_level);
    attributes.level = std::max(1, level + 1);
    attributes.max_hp = 40 + level * 20;
    attributes.hp = attributes.max_hp;
    attributes.attack = 6 + level * 3;
    attributes.defense = 2 + level * 2;
    attributes.magic_attack = 0;
    attributes.magic_defense = 0;
    attributes.speed = 4 + level;
    attributes.mp = 0;
    attributes.max_mp = 0;
    attributes.gold = 0;

    registry_.emplace<CombatComponent>(summon);

    return summon;
}

entt::entity SummonSystem::create_elf_monster(entt::entity owner, int skill_level,
                                              const mir2::common::Position& pos) {
    entt::entity summon = registry_.create();

    auto& state = registry_.emplace<CharacterStateComponent>(summon);
    if (const auto* owner_state = registry_.try_get<CharacterStateComponent>(owner)) {
        state.map_id = owner_state->map_id;
        state.direction = owner_state->direction;
    } else {
        state.map_id = 1;
        state.direction = mir2::common::Direction::DOWN;
    }
    state.position = pos;

    // 神兽属性比骷髅更强
    auto& attributes = registry_.emplace<CharacterAttributesComponent>(summon);
    const int level = std::max(0, skill_level);
    attributes.level = std::max(1, level + 2);  // 比骷髅高1级
    attributes.max_hp = 60 + level * 30;        // 更高的HP
    attributes.hp = attributes.max_hp;
    attributes.attack = 8 + level * 4;          // 更高的攻击
    attributes.defense = 3 + level * 3;         // 更高的防御
    attributes.magic_attack = 4 + level * 2;    // 有魔法攻击
    attributes.magic_defense = 2 + level * 2;   // 有魔法防御
    attributes.speed = 5 + level;
    attributes.mp = 20 + level * 10;
    attributes.max_mp = attributes.mp;
    attributes.gold = 0;

    registry_.emplace<CombatComponent>(summon);

    return summon;
}

}  // namespace mir2::ecs
