/**
 * @file monster_manager.h
 * @brief Client-side monster manager.
 */

#ifndef LEGEND2_CLIENT_GAME_MONSTER_MONSTER_MANAGER_H
#define LEGEND2_CLIENT_GAME_MONSTER_MONSTER_MANAGER_H

#include "client/game/entity_manager.h"
#include "client/game/monster/monster_data.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace mir2::render {
struct Camera;
struct Actor;
} // namespace mir2::render

namespace mir2::game::monster {

/**
 * @brief Client-side monster manager.
 */
class MonsterManager {
public:
    enum class MonsterEventType : uint8_t {
        Added = 0,
        Removed,
        Moved,
        StatsUpdated,
        Damaged,
        Death,
        Attack
    };

    using EventCallback = std::function<void(MonsterEventType event, const ClientMonster& monster)>;

    explicit MonsterManager(mir2::game::EntityManager& entity_manager);

    bool add_monster(const ClientMonster& monster);
    bool remove_monster(uint64_t id);
    ClientMonster* get_monster(uint64_t id);
    const ClientMonster* get_monster(uint64_t id) const;
    void clear();

    bool update_position(uint64_t id, const mir2::common::Position& position, uint8_t direction);
    bool update_stats(uint64_t id, int hp, int max_hp, uint16_t level);
    bool apply_damage(uint64_t id, int damage);
    bool handle_death(uint64_t id, uint64_t killer_id);
    bool set_attacking(uint64_t id, bool attacking);

    void update(float delta_ms);

    std::vector<ClientMonster*> get_visible_monsters(const mir2::render::Camera& camera, int padding);
    std::vector<const ClientMonster*> get_visible_monsters(const mir2::render::Camera& camera, int padding) const;

    std::vector<ClientMonster*> query_range(const mir2::common::Position& center, int radius);
    std::vector<const ClientMonster*> query_range(const mir2::common::Position& center, int radius) const;

    mir2::render::Actor convert_to_actor(const ClientMonster& monster) const;

    int add_listener(EventCallback callback);
    bool remove_listener(int listener_id);

    void set_corpse_duration_ms(uint64_t duration_ms) { corpse_duration_ms_ = duration_ms; }
    uint64_t corpse_duration_ms() const { return corpse_duration_ms_; }

private:
    void notify(MonsterEventType event, const ClientMonster& monster);
    void update_anim_state(ClientMonster& monster, uint64_t now_ms);
    mir2::game::Entity build_entity_snapshot(const ClientMonster& monster) const;

    mir2::game::EntityManager& entity_manager_;
    std::unordered_map<uint64_t, ClientMonster> monsters_;
    uint64_t corpse_duration_ms_ = 10000;

    int next_listener_id_ = 1;
    std::unordered_map<int, EventCallback> listeners_;
};

} // namespace mir2::game::monster

#endif // LEGEND2_CLIENT_GAME_MONSTER_MONSTER_MANAGER_H
