/**
 * @file entity_manager.h
 * @brief 客户端场景实体管理器
 *
 * 管理场景实体（玩家/怪物/NPC/地面物品），并提供简单的网格空间索引
 * 用于范围查询和按位置查找。
 */

#ifndef LEGEND2_CLIENT_GAME_ENTITY_MANAGER_H
#define LEGEND2_CLIENT_GAME_ENTITY_MANAGER_H

#include "common/types.h"
#include "client/core/position_interpolator.h"
#include "render/camera.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mir2::game {

using mir2::common::Position;

/**
 * @brief 场景实体类型
 */
enum class EntityType : uint8_t {
    Player = 0,
    Monster = 1,
    NPC = 2,
    GroundItem = 3
};

/**
 * @brief 场景实体基础属性
 */
struct EntityStats {
    int hp = 0;
    int max_hp = 0;
    int mp = 0;
    int max_mp = 0;
    uint16_t level = 0;
};

/**
 * @brief 场景实体数据
 */
struct Entity {
    uint64_t id = 0;                 ///< 实体ID
    EntityType type = EntityType::Player; ///< 实体类型
    Position position{};             ///< 位置（瓦片坐标）
    uint8_t direction = 0;           ///< 朝向
    EntityStats stats{};             ///< 基础属性
    uint8_t monster_state = 0;       ///< 怪物AI状态（仅对Monster有效）
    uint32_t monster_template_id = 0; ///< 怪物模板ID（仅对Monster有效）
    uint64_t target_id = 0;          ///< 当前目标ID（仅对Monster有效）
    legend2::EntityInterpolator interpolator;  ///< 网络位置插值器
};

/**
 * @brief 客户端场景实体管理器
 */
class EntityManager {
public:
    /**
     * @brief 构造函数
     * @param cell_size 空间索引单元大小（瓦片数，最小为1）
     */
    explicit EntityManager(int cell_size = 1);

    /// 添加实体，已存在时返回false
    bool add_entity(const Entity& entity);

    /// 删除实体
    bool remove_entity(uint64_t id);

    /// 更新实体（不存在则新增）
    bool update_entity(const Entity& entity);

    /// 更新实体位置与朝向
    bool update_entity_position(uint64_t id, const Position& position, uint8_t direction);

    /// 更新实体属性
    bool update_entity_stats(uint64_t id, int hp, int max_hp, int mp, int max_mp, uint16_t level);

    /// 获取实体
    Entity* get_entity(uint64_t id);
    const Entity* get_entity(uint64_t id) const;

    /// 检查实体是否存在
    bool contains(uint64_t id) const;

    /// 清空所有实体
    void clear();

    /// 获取实体数量
    size_t size() const { return entities_.size(); }

    /// 范围查询（方形区域，半径以瓦片计）
    std::vector<Entity*> query_range(const Position& center, int radius);
    std::vector<const Entity*> query_range(const Position& center, int radius) const;

    /// 查询指定位置的实体
    std::vector<Entity*> query_at(const Position& position);
    std::vector<const Entity*> query_at(const Position& position) const;

    /// 视口查询（带 padding 的矩形裁剪 + 排序）
    std::vector<Entity*> get_entities_in_view(const mir2::render::Camera& camera, int padding);
    std::vector<const Entity*> get_entities_in_view(const mir2::render::Camera& camera, int padding) const;

private:
    struct GridCoord {
        int x = 0;
        int y = 0;

        bool operator==(const GridCoord& other) const {
            return x == other.x && y == other.y;
        }
    };

    struct GridCoordHash {
        size_t operator()(const GridCoord& coord) const;
    };

    template <typename Container>
    void query_range_impl(const Position& center, int radius, Container& result) const;

    GridCoord cell_for_position(const Position& position) const;
    void index_entity(uint64_t id, const Position& position);
    void unindex_entity(uint64_t id, const Position& position);
    void move_entity(uint64_t id, const Position& old_position, const Position& new_position);

    int cell_size_ = 1;
    std::unordered_map<uint64_t, Entity> entities_;
    std::unordered_map<GridCoord, std::unordered_set<uint64_t>, GridCoordHash> grid_;
};

} // namespace mir2::game

#endif // LEGEND2_CLIENT_GAME_ENTITY_MANAGER_H
