/**
 * @file entity_manager.cc
 * @brief 客户端场景实体管理器实现
 */

#include "entity_manager.h"

#include <algorithm>
#include <chrono>
#include <type_traits>

namespace mir2::game {

namespace {
uint32_t NowMs() {
    using clock = std::chrono::steady_clock;
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
}
} // namespace

EntityManager::EntityManager(int cell_size)
    : cell_size_(std::max(1, cell_size)) {}

size_t EntityManager::GridCoordHash::operator()(const GridCoord& coord) const {
    const size_t h1 = std::hash<int>{}(coord.x);
    const size_t h2 = std::hash<int>{}(coord.y);
    return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
}

EntityManager::GridCoord EntityManager::cell_for_position(const Position& position) const {
    if (cell_size_ <= 1) {
        return {position.x, position.y};
    }
    return {position.x / cell_size_, position.y / cell_size_};
}

void EntityManager::index_entity(uint64_t id, const Position& position) {
    if (position.x < 0 || position.y < 0) {
        return;
    }
    const GridCoord cell = cell_for_position(position);
    grid_[cell].insert(id);
}

void EntityManager::unindex_entity(uint64_t id, const Position& position) {
    if (position.x < 0 || position.y < 0) {
        return;
    }
    const GridCoord cell = cell_for_position(position);
    auto it = grid_.find(cell);
    if (it == grid_.end()) {
        return;
    }
    it->second.erase(id);
    if (it->second.empty()) {
        grid_.erase(it);
    }
}

void EntityManager::move_entity(uint64_t id, const Position& old_position, const Position& new_position) {
    const GridCoord old_cell = cell_for_position(old_position);
    const GridCoord new_cell = cell_for_position(new_position);
    if (old_cell == new_cell) {
        return;
    }
    unindex_entity(id, old_position);
    index_entity(id, new_position);
}

bool EntityManager::add_entity(const Entity& entity) {
    auto [it, inserted] = entities_.emplace(entity.id, entity);
    if (!inserted) {
        return false;
    }
    it->second.interpolator.set_initial(entity.position);
    index_entity(entity.id, entity.position);
    return true;
}

bool EntityManager::remove_entity(uint64_t id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) {
        return false;
    }
    unindex_entity(id, it->second.position);
    entities_.erase(it);
    return true;
}

bool EntityManager::update_entity(const Entity& entity) {
    auto it = entities_.find(entity.id);
    if (it == entities_.end()) {
        auto [new_it, inserted] = entities_.emplace(entity.id, entity);
        if (inserted) {
            new_it->second.interpolator.set_initial(entity.position);
            index_entity(entity.id, entity.position);
        }
        return true;
    }

    const Position old_position = it->second.position;
    it->second = entity;
    it->second.interpolator.receive_state(entity.position, NowMs());
    move_entity(entity.id, old_position, entity.position);
    return true;
}

bool EntityManager::update_entity_position(uint64_t id, const Position& position, uint8_t direction) {
    auto it = entities_.find(id);
    if (it == entities_.end()) {
        return false;
    }

    const Position old_position = it->second.position;
    it->second.position = position;
    it->second.direction = direction;
    it->second.interpolator.receive_state(position, NowMs());
    move_entity(id, old_position, position);
    return true;
}

bool EntityManager::update_entity_stats(uint64_t id, int hp, int max_hp, int mp, int max_mp, uint16_t level) {
    auto it = entities_.find(id);
    if (it == entities_.end()) {
        return false;
    }

    it->second.stats.hp = hp;
    it->second.stats.max_hp = max_hp;
    it->second.stats.mp = mp;
    it->second.stats.max_mp = max_mp;
    it->second.stats.level = level;
    return true;
}

Entity* EntityManager::get_entity(uint64_t id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) {
        return nullptr;
    }
    return &it->second;
}

const Entity* EntityManager::get_entity(uint64_t id) const {
    auto it = entities_.find(id);
    if (it == entities_.end()) {
        return nullptr;
    }
    return &it->second;
}

bool EntityManager::contains(uint64_t id) const {
    return entities_.find(id) != entities_.end();
}

void EntityManager::clear() {
    entities_.clear();
    grid_.clear();
}

template <typename Container>
void EntityManager::query_range_impl(const Position& center, int radius, Container& result) const {
    if (entities_.empty() || radius < 0) {
        return;
    }

    std::unordered_map<uint64_t, const Entity*> entity_cache;

    const int min_x = center.x - radius;
    const int max_x = center.x + radius;
    const int min_y = center.y - radius;
    const int max_y = center.y + radius;

    const GridCoord min_cell = cell_for_position({min_x, min_y});
    const GridCoord max_cell = cell_for_position({max_x, max_y});

    for (int cy = min_cell.y; cy <= max_cell.y; ++cy) {
        for (int cx = min_cell.x; cx <= max_cell.x; ++cx) {
            GridCoord key{cx, cy};
            auto cell_it = grid_.find(key);
            if (cell_it == grid_.end()) {
                continue;
            }
            for (uint64_t id : cell_it->second) {
                const Entity* entity = nullptr;
                auto cache_it = entity_cache.find(id);
                if (cache_it == entity_cache.end()) {
                    auto entity_it = entities_.find(id);
                    if (entity_it == entities_.end()) {
                        continue;
                    }
                    entity = &entity_it->second;
                    entity_cache.emplace(id, entity);
                } else {
                    entity = cache_it->second;
                }
                auto& entity_ref = *entity;
                if (entity_ref.position.x < min_x || entity_ref.position.x > max_x ||
                    entity_ref.position.y < min_y || entity_ref.position.y > max_y) {
                    continue;
                }
                using Pointer = typename Container::value_type;
                using Pointee = std::remove_pointer_t<Pointer>;
                // query_range_impl is const; casting is safe for non-const callers.
                result.push_back(const_cast<Pointee*>(&entity_ref));
            }
        }
    }
}

std::vector<const Entity*> EntityManager::query_range(const Position& center, int radius) const {
    std::vector<const Entity*> result;
    query_range_impl(center, radius, result);

    return result;
}

std::vector<Entity*> EntityManager::query_range(const Position& center, int radius) {
    std::vector<Entity*> result;
    query_range_impl(center, radius, result);

    return result;
}

std::vector<const Entity*> EntityManager::query_at(const Position& position) const {
    std::vector<const Entity*> result;
    const GridCoord cell = cell_for_position(position);
    auto it = grid_.find(cell);
    if (it == grid_.end()) {
        return result;
    }

    for (uint64_t id : it->second) {
        auto entity_it = entities_.find(id);
        if (entity_it == entities_.end()) {
            continue;
        }
        const Entity& entity = entity_it->second;
        if (entity.position == position) {
            result.push_back(&entity);
        }
    }

    return result;
}

std::vector<Entity*> EntityManager::get_entities_in_view(const mir2::render::Camera& camera, int padding) {
    std::vector<Entity*> result;
    if (entities_.empty()) {
        return result;
    }

    mir2::common::Rect bounds = camera.get_visible_tile_bounds();
    const int min_x = bounds.x - padding;
    const int min_y = bounds.y - padding;
    const int max_x = bounds.x + bounds.width + padding;
    const int max_y = bounds.y + bounds.height + padding;

    const GridCoord min_cell = cell_for_position({min_x, min_y});
    const GridCoord max_cell = cell_for_position({max_x, max_y});

    for (int cy = min_cell.y; cy <= max_cell.y; ++cy) {
        for (int cx = min_cell.x; cx <= max_cell.x; ++cx) {
            GridCoord key{cx, cy};
            auto cell_it = grid_.find(key);
            if (cell_it == grid_.end()) {
                continue;
            }
            for (uint64_t id : cell_it->second) {
                auto entity_it = entities_.find(id);
                if (entity_it == entities_.end()) {
                    continue;
                }
                Entity& entity = entity_it->second;
                if (entity.position.x < min_x || entity.position.x > max_x ||
                    entity.position.y < min_y || entity.position.y > max_y) {
                    continue;
                }
                result.push_back(&entity);
            }
        }
    }

    std::stable_sort(result.begin(), result.end(), [](const Entity* a, const Entity* b) {
        if (a->position.y == b->position.y) {
            if (a->position.x == b->position.x) {
                return a->id < b->id;
            }
            return a->position.x < b->position.x;
        }
        return a->position.y < b->position.y;
    });

    return result;
}

std::vector<const Entity*> EntityManager::get_entities_in_view(const mir2::render::Camera& camera, int padding) const {
    std::vector<const Entity*> result;
    if (entities_.empty()) {
        return result;
    }

    mir2::common::Rect bounds = camera.get_visible_tile_bounds();
    const int min_x = bounds.x - padding;
    const int min_y = bounds.y - padding;
    const int max_x = bounds.x + bounds.width + padding;
    const int max_y = bounds.y + bounds.height + padding;

    const GridCoord min_cell = cell_for_position({min_x, min_y});
    const GridCoord max_cell = cell_for_position({max_x, max_y});

    for (int cy = min_cell.y; cy <= max_cell.y; ++cy) {
        for (int cx = min_cell.x; cx <= max_cell.x; ++cx) {
            GridCoord key{cx, cy};
            auto cell_it = grid_.find(key);
            if (cell_it == grid_.end()) {
                continue;
            }
            for (uint64_t id : cell_it->second) {
                auto entity_it = entities_.find(id);
                if (entity_it == entities_.end()) {
                    continue;
                }
                const Entity& entity = entity_it->second;
                if (entity.position.x < min_x || entity.position.x > max_x ||
                    entity.position.y < min_y || entity.position.y > max_y) {
                    continue;
                }
                result.push_back(&entity);
            }
        }
    }

    std::stable_sort(result.begin(), result.end(), [](const Entity* a, const Entity* b) {
        if (a->position.y == b->position.y) {
            if (a->position.x == b->position.x) {
                return a->id < b->id;
            }
            return a->position.x < b->position.x;
        }
        return a->position.y < b->position.y;
    });

    return result;
}

std::vector<Entity*> EntityManager::query_at(const Position& position) {
    std::vector<Entity*> result;
    const GridCoord cell = cell_for_position(position);
    auto it = grid_.find(cell);
    if (it == grid_.end()) {
        return result;
    }

    for (uint64_t id : it->second) {
        auto entity_it = entities_.find(id);
        if (entity_it == entities_.end()) {
            continue;
        }
        Entity& entity = entity_it->second;
        if (entity.position == position) {
            result.push_back(&entity);
        }
    }

    return result;
}

} // namespace mir2::game
