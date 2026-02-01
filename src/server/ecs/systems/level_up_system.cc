#include "ecs/systems/level_up_system.h"

#include "ecs/components/summon_component.h"
#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/attribute_events.h"

namespace mir2::ecs {

LevelUpSystem::LevelUpSystem()
    : System(SystemPriority::kLevelUp) {}

void LevelUpSystem::Update(entt::registry& registry, float /*delta_time*/) {
    auto view = registry.view<CharacterIdentityComponent, CharacterAttributesComponent>();
    for (auto entity : view) {
        CheckLevelUp(registry, entity);
    }
}

void LevelUpSystem::ApplyLevelUpStats(mir2::common::CharacterClass char_class,
                                      CharacterAttributesComponent& attributes) {
    switch (char_class) {
        case mir2::common::CharacterClass::WARRIOR:
            // 战士：重视HP、物理攻击和防御
            attributes.max_hp += 20;
            attributes.max_mp += 5;
            attributes.attack += 3;
            attributes.defense += 2;
            attributes.magic_attack += 1;
            attributes.magic_defense += 1;
            break;

        case mir2::common::CharacterClass::MAGE:
            // 法师：重视MP和魔法属性
            attributes.max_hp += 8;
            attributes.max_mp += 15;
            attributes.attack += 1;
            attributes.defense += 1;
            attributes.magic_attack += 4;
            attributes.magic_defense += 2;
            break;

        case mir2::common::CharacterClass::TAOIST:
            // 道士：平衡型成长
            attributes.max_hp += 12;
            attributes.max_mp += 10;
            attributes.attack += 2;
            attributes.defense += 1;
            attributes.magic_attack += 2;
            attributes.magic_defense += 2;
            break;
    }
}

bool LevelUpSystem::GainExperience(entt::registry& registry, entt::entity entity, int exp_amount,
                                   EventBus* event_bus) {
    if (exp_amount < 0) {
        return false;
    }

    auto* attributes = registry.try_get<CharacterAttributesComponent>(entity);
    if (!attributes) {
        return false;
    }

    const int old_level = attributes->level;
    attributes->experience += exp_amount;
    dirty_tracker::mark_attributes_dirty(registry, entity);

    // 发布经验获得事件
    if (event_bus && exp_amount > 0) {
        events::ExperienceGainedEvent event;
        event.entity = entity;
        event.amount = exp_amount;
        event.total_experience = attributes->experience;
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

    bool leveled_up = false;
    int exp_needed = attributes->GetExpForNextLevel();
    while (attributes->experience >= exp_needed) {
        attributes->experience -= exp_needed;
        const int old_level = attributes->level;
        attributes->level += 1;
        ApplyLevelUpStats(identity->char_class, *attributes);

        // 升级后恢复满 HP/MP
        attributes->hp = attributes->max_hp;
        attributes->mp = attributes->max_mp;

        // 发布升级事件
        if (event_bus) {
            events::LevelUpEvent event;
            event.entity = entity;
            event.old_level = old_level;
            event.new_level = attributes->level;
            event_bus->Publish(event);
        }

        exp_needed = attributes->GetExpForNextLevel();
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

    // 召唤兽获得主人经验的一定比例（例如30%）
    const int summon_exp = static_cast<int>(exp_amount * 0.3);

    for (entt::entity summon_entity : summoner->summons) {
        if (!registry.valid(summon_entity)) {
            continue;
        }

        auto* summon_comp = registry.try_get<SummonComponent>(summon_entity);
        if (summon_comp) {
            summon_comp->summon_experience += summon_exp;
        }

        // 召唤兽也可以升级
        GainExperience(registry, summon_entity, summon_exp, event_bus);
    }
}

void LevelUpSystem::DistributeExpToParty(entt::registry& registry, entt::entity killer,
                                         int exp_amount, EventBus* event_bus) {
    // TODO: 实现组队经验分配
    // 当前版本：直接给击杀者全部经验
    GainExperience(registry, killer, exp_amount, event_bus);

    // 同时分配给召唤兽
    DistributeExpToSummons(registry, killer, exp_amount, event_bus);
}

void LevelUpSystem::DistributeExpByDamage(entt::registry& registry,
                                          const std::unordered_map<entt::entity, int>& damage_contributors,
                                          int total_exp, EventBus* event_bus) {
    if (total_exp <= 0 || damage_contributors.empty()) {
        return;
    }

    // 计算总伤害
    int total_damage = 0;
    for (const auto& [entity, damage] : damage_contributors) {
        total_damage += damage;
    }

    if (total_damage <= 0) {
        return;
    }

    // 按伤害比例分配经验
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
