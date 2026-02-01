/**
 * @file monster_manager.cc
 * @brief Client-side monster manager implementation.
 */

#include "monster_manager.h"

#include "render/actor_renderer.h"

#include "render/camera.h"

#include <algorithm>
#include <chrono>

namespace mir2::game::monster {

namespace {
uint64_t NowMs() {
    using clock = std::chrono::steady_clock;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
}

int clamp_int(int value, int min_value, int max_value) {
    return std::max(min_value, std::min(value, max_value));
}

mir2::game::ActorAction to_actor_action(ClientMonsterState state) {
    switch (state) {
        case ClientMonsterState::Idle:
            return mir2::game::ActorAction::STAND;
        case ClientMonsterState::Walk:
            return mir2::game::ActorAction::WALK;
        case ClientMonsterState::Attack:
            return mir2::game::ActorAction::HIT;
        case ClientMonsterState::Struck:
            return mir2::game::ActorAction::STRUCK;
        case ClientMonsterState::Death:
            return mir2::game::ActorAction::DIE;
        case ClientMonsterState::Dead:
            return mir2::game::ActorAction::DEATH;
        default:
            return mir2::game::ActorAction::STAND;
    }
}

const mir2::game::MonsterAction* resolve_monster_action(const ClientMonster& monster) {
    const auto* action = mir2::game::get_monster_action(monster.render_config.race,
                                                        monster.render_config.appearance);
    if (!action) {
        action = &mir2::game::MA14;
    }
    return action;
}

const mir2::game::ActionInfo* select_action_info(const mir2::game::MonsterAction& action,
                                                 ClientMonsterState state) {
    switch (state) {
        case ClientMonsterState::Walk:
            return &action.act_walk;
        case ClientMonsterState::Attack:
            return &action.act_attack;
        case ClientMonsterState::Struck:
            return &action.act_struck;
        case ClientMonsterState::Death:
            return &action.act_die;
        case ClientMonsterState::Dead:
            return &action.act_death;
        case ClientMonsterState::Idle:
        default:
            return &action.act_stand;
    }
}

bool is_loop_state(ClientMonsterState state) {
    return state == ClientMonsterState::Idle || state == ClientMonsterState::Walk;
}
} // namespace

MonsterManager::MonsterManager(mir2::game::EntityManager& entity_manager)
    : entity_manager_(entity_manager) {}

bool MonsterManager::add_monster(const ClientMonster& monster) {
    if (monster.id == 0) {
        return false;
    }

    auto [it, inserted] = monsters_.emplace(monster.id, monster);
    if (!inserted) {
        return false;
    }

    it->second.interpolator.set_initial(monster.position);
    it->second.anim_state.state = it->second.state;

    mir2::game::Entity entity = build_entity_snapshot(it->second);
    if (!entity_manager_.add_entity(entity)) {
        entity_manager_.update_entity(entity);
    }

    notify(MonsterEventType::Added, it->second);
    return true;
}

bool MonsterManager::remove_monster(uint64_t id) {
    auto it = monsters_.find(id);
    if (it == monsters_.end()) {
        return false;
    }

    ClientMonster snapshot = it->second;
    entity_manager_.remove_entity(id);
    monsters_.erase(it);
    notify(MonsterEventType::Removed, snapshot);
    return true;
}

ClientMonster* MonsterManager::get_monster(uint64_t id) {
    auto it = monsters_.find(id);
    if (it == monsters_.end()) {
        return nullptr;
    }
    return &it->second;
}

const ClientMonster* MonsterManager::get_monster(uint64_t id) const {
    auto it = monsters_.find(id);
    if (it == monsters_.end()) {
        return nullptr;
    }
    return &it->second;
}

void MonsterManager::clear() {
    for (const auto& entry : monsters_) {
        entity_manager_.remove_entity(entry.first);
    }
    monsters_.clear();
}

bool MonsterManager::update_position(uint64_t id, const mir2::common::Position& position, uint8_t direction) {
    auto it = monsters_.find(id);
    if (it == monsters_.end()) {
        return false;
    }

    ClientMonster& monster = it->second;
    monster.position = position;
    monster.direction = direction;
    monster.interpolator.receive_state(position, static_cast<uint32_t>(NowMs()));

    entity_manager_.update_entity_position(id, position, direction);
    notify(MonsterEventType::Moved, monster);
    return true;
}

bool MonsterManager::update_stats(uint64_t id, int hp, int max_hp, uint16_t level) {
    auto it = monsters_.find(id);
    if (it == monsters_.end()) {
        return false;
    }

    ClientMonster& monster = it->second;
    const int safe_max_hp = std::max(0, max_hp);
    const int safe_hp = clamp_int(hp, 0, safe_max_hp);

    monster.max_hp = safe_max_hp;
    monster.hp = safe_hp;
    monster.level = level;

    entity_manager_.update_entity_stats(id, safe_hp, safe_max_hp, 0, 0, level);
    notify(MonsterEventType::StatsUpdated, monster);

    if (monster.hp <= 0) {
        handle_death(id, 0);
    }

    return true;
}

bool MonsterManager::apply_damage(uint64_t id, int damage) {
    if (damage <= 0) {
        return false;
    }

    auto it = monsters_.find(id);
    if (it == monsters_.end()) {
        return false;
    }

    ClientMonster& monster = it->second;
    if (monster.is_dead()) {
        return false;
    }

    const int new_hp = clamp_int(monster.hp - damage, 0, std::max(0, monster.max_hp));
    if (new_hp == monster.hp) {
        return false;
    }

    monster.hp = new_hp;
    entity_manager_.update_entity_stats(id, monster.hp, monster.max_hp, 0, 0, monster.level);
    notify(MonsterEventType::Damaged, monster);

    if (monster.hp <= 0) {
        handle_death(id, 0);
    } else {
        monster.state = ClientMonsterState::Struck;
        monster.anim_state.state = monster.state;
        monster.anim_state.finished = false;
        monster.anim_state.last_frame_tick = 0;
    }

    return true;
}

bool MonsterManager::handle_death(uint64_t id, uint64_t killer_id) {
    auto it = monsters_.find(id);
    if (it == monsters_.end()) {
        return false;
    }

    ClientMonster& monster = it->second;
    if (monster.state == ClientMonsterState::Death || monster.state == ClientMonsterState::Dead) {
        return false;
    }

    monster.hp = 0;
    monster.state = ClientMonsterState::Death;
    monster.anim_state.state = monster.state;
    monster.anim_state.loop = false;
    monster.anim_state.finished = false;
    monster.anim_state.last_frame_tick = 0;
    monster.death_time = NowMs();

    entity_manager_.update_entity_stats(id, 0, monster.max_hp, 0, 0, monster.level);
    notify(MonsterEventType::Death, monster);

    (void)killer_id;
    return true;
}

bool MonsterManager::set_attacking(uint64_t id, bool attacking) {
    auto it = monsters_.find(id);
    if (it == monsters_.end()) {
        return false;
    }

    ClientMonster& monster = it->second;
    if (attacking) {
        monster.state = ClientMonsterState::Attack;
        monster.anim_state.state = monster.state;
        monster.anim_state.loop = false;
        monster.anim_state.finished = false;
        monster.anim_state.last_frame_tick = 0;
        notify(MonsterEventType::Attack, monster);
    } else if (monster.state == ClientMonsterState::Attack) {
        monster.state = ClientMonsterState::Idle;
        monster.anim_state.state = monster.state;
        monster.anim_state.loop = true;
        monster.anim_state.finished = false;
        monster.anim_state.last_frame_tick = 0;
    }

    return true;
}

void MonsterManager::update(float delta_ms) {
    const uint64_t now_ms = NowMs();
    std::vector<uint64_t> expired;

    for (auto& entry : monsters_) {
        ClientMonster& monster = entry.second;
        monster.interpolator.update(delta_ms);

        if (mir2::game::Entity* entity = entity_manager_.get_entity(monster.id)) {
            entity->interpolator.update(delta_ms);
        }

        update_anim_state(monster, now_ms);

        if (monster.state == ClientMonsterState::Death && monster.anim_state.finished) {
            monster.state = ClientMonsterState::Dead;
            monster.anim_state.state = monster.state;
            if (monster.death_time == 0) {
                monster.death_time = now_ms;
            }
        }

        if (monster.death_time > 0 && monster.is_dead()) {
            if (now_ms >= monster.death_time + corpse_duration_ms_) {
                expired.push_back(monster.id);
            }
        }
    }

    for (uint64_t id : expired) {
        remove_monster(id);
    }
}

std::vector<ClientMonster*> MonsterManager::get_visible_monsters(const mir2::render::Camera& camera, int padding) {
    std::vector<ClientMonster*> result;
    auto entities = entity_manager_.get_entities_in_view(camera, padding);
    result.reserve(entities.size());

    for (mir2::game::Entity* entity : entities) {
        if (!entity || entity->type != mir2::game::EntityType::Monster) {
            continue;
        }
        auto it = monsters_.find(entity->id);
        if (it == monsters_.end()) {
            continue;
        }
        result.push_back(&it->second);
    }

    return result;
}

std::vector<const ClientMonster*> MonsterManager::get_visible_monsters(const mir2::render::Camera& camera,
                                                                       int padding) const {
    std::vector<const ClientMonster*> result;
    auto entities = static_cast<const mir2::game::EntityManager&>(entity_manager_).get_entities_in_view(camera, padding);
    result.reserve(entities.size());

    for (const mir2::game::Entity* entity : entities) {
        if (!entity || entity->type != mir2::game::EntityType::Monster) {
            continue;
        }
        auto it = monsters_.find(entity->id);
        if (it == monsters_.end()) {
            continue;
        }
        result.push_back(&it->second);
    }

    return result;
}

std::vector<ClientMonster*> MonsterManager::query_range(const mir2::common::Position& center, int radius) {
    std::vector<ClientMonster*> result;
    auto entities = entity_manager_.query_range(center, radius);
    result.reserve(entities.size());

    for (mir2::game::Entity* entity : entities) {
        if (!entity || entity->type != mir2::game::EntityType::Monster) {
            continue;
        }
        auto it = monsters_.find(entity->id);
        if (it == monsters_.end()) {
            continue;
        }
        result.push_back(&it->second);
    }

    return result;
}

std::vector<const ClientMonster*> MonsterManager::query_range(const mir2::common::Position& center, int radius) const {
    std::vector<const ClientMonster*> result;
    auto entities = static_cast<const mir2::game::EntityManager&>(entity_manager_).query_range(center, radius);
    result.reserve(entities.size());

    for (const mir2::game::Entity* entity : entities) {
        if (!entity || entity->type != mir2::game::EntityType::Monster) {
            continue;
        }
        auto it = monsters_.find(entity->id);
        if (it == monsters_.end()) {
            continue;
        }
        result.push_back(&it->second);
    }

    return result;
}

int MonsterManager::add_listener(EventCallback callback) {
    if (!callback) {
        return 0;
    }

    const int id = next_listener_id_++;
    listeners_[id] = std::move(callback);
    return id;
}

bool MonsterManager::remove_listener(int listener_id) {
    return listeners_.erase(listener_id) > 0;
}

void MonsterManager::notify(MonsterEventType event, const ClientMonster& monster) {
    if (listeners_.empty()) {
        return;
    }

    std::vector<EventCallback> callbacks;
    callbacks.reserve(listeners_.size());
    for (const auto& entry : listeners_) {
        if (entry.second) {
            callbacks.push_back(entry.second);
        }
    }

    for (const auto& callback : callbacks) {
        callback(event, monster);
    }
}

void MonsterManager::update_anim_state(ClientMonster& monster, uint64_t now_ms) {
    MonsterAnimState& anim = monster.anim_state;
    const mir2::game::MonsterAction* action = resolve_monster_action(monster);
    const mir2::common::Position interp_pos = monster.interpolator.get_tile_position();
    const bool moving = (interp_pos != monster.position);

    ClientMonsterState desired_state = monster.state;
    if (desired_state == ClientMonsterState::Idle || desired_state == ClientMonsterState::Walk) {
        desired_state = moving ? ClientMonsterState::Walk : ClientMonsterState::Idle;
        monster.state = desired_state;
    }

    int dir = static_cast<int>(monster.direction);
    if (dir < 0 || dir >= mir2::game::MAX_DIRECTION) {
        dir = 0;
    }

    struct AnimConfig {
        int start_frame = 0;
        int end_frame = 0;
        int frame_time = 0;
        bool loop = false;
    };

    auto build_anim_config = [&](ClientMonsterState state) -> AnimConfig {
        const mir2::game::ActionInfo* info = select_action_info(*action, state);
        if (info->frame <= 0) {
            info = &action->act_stand;
        }

        AnimConfig config;
        config.start_frame = info->get_frame_index(dir, 0);
        config.end_frame = config.start_frame + info->frame - 1;
        config.frame_time = info->ftime;
        config.loop = is_loop_state(state);

        if (state == ClientMonsterState::Dead) {
            if (action->act_death.frame <= 0 && action->act_die.frame > 0) {
                config.start_frame = action->act_die.get_frame_index(dir, action->act_die.frame - 1);
                config.end_frame = config.start_frame;
                config.frame_time = action->act_die.ftime;
            }
            config.loop = false;
        }

        return config;
    };

    const AnimConfig config = build_anim_config(desired_state);

    const bool needs_reset = (anim.state != desired_state) ||
                             (anim.start_frame != config.start_frame) ||
                             (anim.end_frame != config.end_frame) ||
                             (anim.frame_time != config.frame_time) ||
                             (anim.loop != config.loop);

    if (needs_reset) {
        anim.state = desired_state;
        anim.start_frame = config.start_frame;
        anim.end_frame = config.end_frame;
        anim.current_frame = config.start_frame;
        anim.frame_time = config.frame_time;
        anim.loop = config.loop;
        anim.finished = (config.start_frame > config.end_frame || config.frame_time <= 0);
        anim.last_frame_tick = now_ms;
    }

    if (anim.finished || anim.frame_time <= 0) {
        return;
    }

    if (anim.last_frame_tick == 0) {
        anim.last_frame_tick = now_ms;
        return;
    }

    if (anim.start_frame > anim.end_frame) {
        anim.finished = true;
        return;
    }

    if (anim.current_frame < anim.start_frame || anim.current_frame > anim.end_frame) {
        anim.current_frame = anim.start_frame;
    }

    const uint64_t elapsed = now_ms - anim.last_frame_tick;
    const uint64_t frame_time_ms = static_cast<uint64_t>(anim.frame_time);
    if (elapsed < frame_time_ms) {
        return;
    }

    const int steps = static_cast<int>(elapsed / frame_time_ms);
    anim.last_frame_tick += static_cast<uint64_t>(steps) * frame_time_ms;

    for (int i = 0; i < steps; ++i) {
        if (anim.current_frame < anim.end_frame) {
            ++anim.current_frame;
            continue;
        }

        if (anim.loop) {
            anim.current_frame = anim.start_frame;
        } else {
            anim.current_frame = anim.end_frame;
            anim.finished = true;
            break;
        }
    }

    if (!anim.finished) {
        return;
    }

    if (monster.state == ClientMonsterState::Attack ||
        monster.state == ClientMonsterState::Struck) {
        ClientMonsterState next_state = moving ? ClientMonsterState::Walk : ClientMonsterState::Idle;
        monster.state = next_state;
        const AnimConfig next_config = build_anim_config(next_state);
        anim.state = next_state;
        anim.start_frame = next_config.start_frame;
        anim.end_frame = next_config.end_frame;
        anim.current_frame = next_config.start_frame;
        anim.frame_time = next_config.frame_time;
        anim.loop = next_config.loop;
        anim.finished = (next_config.start_frame > next_config.end_frame || next_config.frame_time <= 0);
        anim.last_frame_tick = now_ms;
    } else if (monster.state == ClientMonsterState::Death) {
        monster.state = ClientMonsterState::Dead;
        const AnimConfig dead_config = build_anim_config(monster.state);
        anim.state = monster.state;
        anim.start_frame = dead_config.start_frame;
        anim.end_frame = dead_config.end_frame;
        anim.current_frame = dead_config.start_frame;
        anim.frame_time = dead_config.frame_time;
        anim.loop = dead_config.loop;
        anim.finished = (dead_config.start_frame > dead_config.end_frame || dead_config.frame_time <= 0);
        anim.last_frame_tick = now_ms;
    }
}

mir2::game::Entity MonsterManager::build_entity_snapshot(const ClientMonster& monster) const {
    mir2::game::Entity entity;
    entity.id = monster.id;
    entity.type = mir2::game::EntityType::Monster;
    entity.position = monster.position;
    entity.direction = monster.direction;
    entity.stats.hp = monster.hp;
    entity.stats.max_hp = monster.max_hp;
    entity.stats.mp = 0;
    entity.stats.max_mp = 0;
    entity.stats.level = monster.level;
    entity.monster_state = static_cast<uint8_t>(monster.state);
    entity.monster_template_id = monster.template_id;
    return entity;
}

mir2::render::Actor MonsterManager::convert_to_actor(const ClientMonster& monster) const {
    mir2::render::Actor actor;
    const mir2::common::Position interp_pos = monster.interpolator.get_tile_position();
    const int safe_x = std::max(0, interp_pos.x);
    const int safe_y = std::max(0, interp_pos.y);

    actor.data.id = static_cast<int32_t>(monster.id);
    actor.data.map_x = static_cast<uint16_t>(safe_x);
    actor.data.map_y = static_cast<uint16_t>(safe_y);
    actor.data.direction = monster.direction;
    actor.data.race = monster.render_config.race;
    actor.data.appearance = static_cast<uint16_t>(monster.render_config.appearance);
    actor.data.name = monster.name;
    actor.data.is_dead = monster.is_dead();

    actor.anim.current_action = to_actor_action(monster.state);
    actor.anim.start_frame = monster.anim_state.start_frame;
    actor.anim.end_frame = monster.anim_state.end_frame;
    actor.anim.current_frame = monster.anim_state.current_frame;
    actor.anim.frame_time = monster.anim_state.frame_time;
    actor.anim.last_frame_tick = monster.anim_state.last_frame_tick;
    actor.anim.lock_end_frame = (monster.state == ClientMonsterState::Dead);

    return actor;
}

} // namespace mir2::game::monster
