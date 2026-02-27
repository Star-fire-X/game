#include "ecs/systems/level_up_system.h"

#include "ecs/components/summon_component.h"
#include "ecs/components/party_component.h"
#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/attribute_events.h"
#include "ecs/events/combat_events.h"
#include "ecs/components/monster_component.h"
#include "ecs/systems/growth_formulas.h"
#include "common/types/growth_constants.h"

#include <algorithm>
#include <cmath>

namespace mir2::ecs {

LevelUpSystem::LevelUpSystem()
    : System(SystemPriority::kLevelUp) {}

LevelUpSystem::LevelUpSystem(entt::registry& registry, EventBus& event_bus)
    : System(SystemPriority::kLevelUp),
      registry_(&registry),
      event_bus_(&event_bus) {
    death_subscription_ = event_bus_->SubscribeScoped<events::EntityDeathEvent>(
        [this](events::EntityDeathEvent& event) {
            OnEntityDeath(event);
        });
}

void LevelUpSystem::Update(entt::registry& registry, float /*delta_time*/) {
    auto view = registry.view<CharacterIdentityComponent, CharacterAttributesComponent>();
    for (auto entity : view) {
        CheckLevelUp(registry, entity);
    }
}

void LevelUpSystem::OnEntityDeath(events::EntityDeathEvent& event) {
    if (!registry_) {
        return;
    }

    if (event.entity == entt::null || !registry_->valid(event.entity)) {
        return;
    }

    const auto* monster_identity = registry_->try_get<MonsterIdentityComponent>(event.entity);
    if (!monster_identity || monster_identity->monster_template_id == 0) {
        return;
    }

    const auto* attributes = registry_->try_get<CharacterAttributesComponent>(event.entity);
    if (!attributes) {
        return;
    }

    const int base_exp = attributes->level * 10;
    if (base_exp <= 0) {
        return;
    }

    if (event.killer == entt::null || !registry_->valid(event.killer)) {
        return;
    }

    // 应用等级差惩罚
    const auto* killer_attrs = registry_->try_get<CharacterAttributesComponent>(event.killer);
    int adjusted_exp = base_exp;
    if (killer_attrs) {
        adjusted_exp = GrowthFormulas::CalcGetExp(
            killer_attrs->level, attributes->level, base_exp);
    }

    DistributeExpToParty(*registry_, event.killer, adjusted_exp, event_bus_);
}

void LevelUpSystem::RecalcLevelAbilities(entt::registry& registry, entt::entity entity) {
    auto* attributes = registry.try_get<CharacterAttributesComponent>(entity);
    auto* identity = registry.try_get<CharacterIdentityComponent>(entity);
    if (!attributes || !identity) {
        return;
    }

    NakedAbility ability = GrowthFormulas::CalcLevelAbility(
        identity->char_class, attributes->level);

    // 设置绝对值（裸装基础属性）
    attributes->max_hp = ability.max_hp;
    attributes->max_mp = ability.max_mp;
    attributes->attack = ability.dc_hi;
    attributes->defense = ability.ac_hi;
    attributes->magic_attack = ability.mc_hi;
    attributes->magic_defense = ability.mac_hi;
    attributes->sc = ability.sc_hi;
    attributes->max_weight = ability.max_weight;
    attributes->max_wear_weight = ability.max_wear_weight;
    attributes->max_hand_weight = ability.max_hand_weight;
}

bool LevelUpSystem::GainExperience(entt::registry& registry, entt::entity entity, int64_t exp_amount,
                                   EventBus* event_bus) {
    if (exp_amount < 0) {
        return false;
    }

    auto* attributes = registry.try_get<CharacterAttributesComponent>(entity);
    if (!attributes) {
        return false;
    }

    // 满级不再获得经验
    if (attributes->level >= common::MAX_LEVEL) {
        return false;
    }

    const int old_level = attributes->level;
    attributes->experience += exp_amount;
    dirty_tracker::mark_attributes_dirty(registry, entity);

    // 发布经验获得事件
    if (event_bus && exp_amount > 0) {
        events::ExperienceGainedEvent event;
        event.entity = entity;
        event.amount = static_cast<int>(std::min(exp_amount, static_cast<int64_t>(INT32_MAX)));
        event.total_experience = static_cast<int>(std::min(attributes->experience, static_cast<int64_t>(INT32_MAX)));
        event_bus->Publish(event);
    }

    CheckLevelUp(registry, entity, event_bus);

    return attributes->level > old_level;
}

void LevelUpSystem::CheckLevelUp(entt::registry& registry, entt::entity entity,
                                 EventBus* event_bus) {
    auto* attributes = registry.try_get<CharacterAttributesComponent>(entity);
    if (!attributes) {
        return;
    }

    auto* identity = registry.try_get<CharacterIdentityComponent>(entity);
    if (!identity) {
        return;
    }

    // 满级检查
    if (attributes->level >= common::MAX_LEVEL) {
        return;
    }

    bool leveled_up = false;
    int64_t exp_needed = GrowthFormulas::GetExpForLevel(attributes->level);
    while (attributes->experience >= exp_needed && attributes->level < common::MAX_LEVEL) {
        attributes->experience -= exp_needed;
        const int old_level = attributes->level;
        attributes->level += 1;

        // 使用公式重算裸装属性（从增量模型改为绝对值模型）
        RecalcLevelAbilities(registry, entity);

        // 升级后恢复 HP/MP（+2000，不超过最大值）
        attributes->hp = std::min(attributes->max_hp, attributes->hp + 2000);
        attributes->mp = std::min(attributes->max_mp, attributes->mp + 2000);

        // 升级时 body_luck +100
        attributes->body_luck += 100;

        // 发布升级事件
        if (event_bus) {
            events::LevelUpEvent event;
            event.entity = entity;
            event.old_level = old_level;
            event.new_level = attributes->level;
            event_bus->Publish(event);
        }

        exp_needed = GrowthFormulas::GetExpForLevel(attributes->level);
        leveled_up = true;
    }

    if (leveled_up) {
        dirty_tracker::mark_attributes_dirty(registry, entity);
    }
}

void LevelUpSystem::DistributeExpToSummons(entt::registry& registry, entt::entity owner,
                                           int exp_amount, EventBus* event_bus) {
    if (exp_amount <= 0) {
        return;
    }

    auto* summoner = registry.try_get<SummonerComponent>(owner);
    if (!summoner || summoner->summons.empty()) {
        return;
    }

    // 召唤兽获得主人经验的30%
    const int summon_exp = static_cast<int>(exp_amount * 0.3);

    for (entt::entity summon_entity : summoner->summons) {
        if (!registry.valid(summon_entity)) {
            continue;
        }

        auto* summon_comp = registry.try_get<SummonComponent>(summon_entity);
        if (summon_comp) {
            summon_comp->summon_experience += summon_exp;
        }

        GainExperience(registry, summon_entity, summon_exp, event_bus);
    }
}

void LevelUpSystem::DistributeExpToParty(entt::registry& registry, entt::entity killer,
                                         int exp_amount, EventBus* event_bus) {
    if (exp_amount <= 0) {
        return;
    }

    // 检查击杀者是否在队伍中
    auto* member_comp = registry.try_get<PartyMemberComponent>(killer);
    if (!member_comp || member_comp->party_id == 0) {
        // 无组队，直接给击杀者全部经验
        GainExperience(registry, killer, exp_amount, event_bus);
        DistributeExpToSummons(registry, killer, exp_amount, event_bus);
        return;
    }

    // 查找队伍实体
    const auto* killer_state = registry.try_get<CharacterStateComponent>(killer);
    if (!killer_state) {
        GainExperience(registry, killer, exp_amount, event_bus);
        DistributeExpToSummons(registry, killer, exp_amount, event_bus);
        return;
    }

    // 收集有效队员（同地图且距离在 PARTY_SHARE_RANGE 内）
    struct EligibleMember {
        entt::entity entity;
        int level;
    };
    std::vector<EligibleMember> eligible;

    // 遍历所有拥有相同 party_id 的 PartyMemberComponent
    auto party_view = registry.view<PartyMemberComponent, CharacterAttributesComponent, CharacterStateComponent>();
    for (auto [e, pm, attrs, state] : party_view.each()) {
        if (pm.party_id != member_comp->party_id) continue;
        if (!registry.valid(e)) continue;

        // 同地图检查
        if (state.map_id != killer_state->map_id) continue;

        // 距离检查
        int dx = state.position.x - killer_state->position.x;
        int dy = state.position.y - killer_state->position.y;
        int distance = std::abs(dx) + std::abs(dy);  // 曼哈顿距离
        if (distance > common::PARTY_SHARE_RANGE) continue;

        eligible.push_back({e, attrs.level});
    }

    if (eligible.empty()) {
        // 不应发生，但保险起见
        GainExperience(registry, killer, exp_amount, event_bus);
        DistributeExpToSummons(registry, killer, exp_amount, event_bus);
        return;
    }

    // 应用组队经验加成
    double bonus = GrowthFormulas::GetPartyExpBonus(static_cast<int>(eligible.size()));
    int64_t total_exp = static_cast<int64_t>(exp_amount * bonus);

    // 计算等级总和
    int level_sum = 0;
    for (const auto& m : eligible) {
        level_sum += m.level;
    }
    if (level_sum <= 0) level_sum = 1;

    // 按等级比例分配
    for (const auto& m : eligible) {
        int64_t member_exp = total_exp * m.level / level_sum;
        if (member_exp < 1) member_exp = 1;
        GainExperience(registry, m.entity, member_exp, event_bus);
        DistributeExpToSummons(registry, m.entity, static_cast<int>(member_exp), event_bus);
    }
}

void LevelUpSystem::DistributeExpByDamage(entt::registry& registry,
                                          const std::unordered_map<entt::entity, int>& damage_contributors,
                                          int total_exp, EventBus* event_bus) {
    if (total_exp <= 0 || damage_contributors.empty()) {
        return;
    }

    int total_damage = 0;
    for (const auto& [entity, damage] : damage_contributors) {
        total_damage += damage;
    }

    if (total_damage <= 0) {
        return;
    }

    for (const auto& [entity, damage] : damage_contributors) {
        if (!registry.valid(entity) || damage <= 0) {
            continue;
        }

        const int exp_share = static_cast<int>(
            static_cast<int64_t>(total_exp) * damage / total_damage);

        if (exp_share > 0) {
            GainExperience(registry, entity, exp_share, event_bus);
            DistributeExpToSummons(registry, entity, exp_share, event_bus);
        }
    }
}

}  // namespace mir2::ecs
