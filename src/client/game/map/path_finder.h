/**
 * @file path_finder.h
 * @brief Legend2 A*寻路器（职责分离）
 *
 * PathFinder 专门负责：
 * - A* 算法实现
 * - 启发式函数
 * - 邻居节点生成
 */

#ifndef LEGEND2_CLIENT_GAME_MAP_PATH_FINDER_H
#define LEGEND2_CLIENT_GAME_MAP_PATH_FINDER_H

#include "common/types.h"
#include "client/game/map/i_walkability_provider.h"
#include <memory>
#include <unordered_map>
#include <vector>

namespace mir2::game::map {

using mir2::common::Position;

/**
 * @brief A*路径查找算法的实现
 *
 * 职责：
 * - 执行A*寻路算法
 * - 管理启发式函数
 * - 管理搜索参数限制
 *
 * 依赖：IWalkabilityProvider（用于查询可行走性）
 */
class PathFinder {
public:
    /**
     * @brief 构造函数
     * @param walkability_provider 可行走性提供者（用于查询地图）
     */
    explicit PathFinder(const IWalkabilityProvider& walkability_provider);

    ~PathFinder() = default;

    // 禁止复制
    PathFinder(const PathFinder&) = delete;
    PathFinder& operator=(const PathFinder&) = delete;

    /**
     * @brief 查找从起点到终点的路径
     * @param start 起点位置
     * @param end 终点位置
     * @return 路径位置列表（包含起点和终点），无路径返回空列表
     *
     * 使用 A* 算法实现：
     * - 启发式函数：Manhattan 距离（可采纳）
     * - 移动代价：直线 10，对角线 14
     * - 最大搜索节点数限制防止死循环
     */
    std::vector<Position> find_path(const Position& start, const Position& end) const;

    // PascalCase compatibility wrappers.
    std::vector<Position> FindPath(const Position& start, const Position& end) const {
        return find_path(start, end);
    }

    /**
     * @brief 设置最大搜索节点数限制
     * @param max_length 最大节点数（0=无限制）
     */
    void set_max_path_length(int max_length) noexcept { max_path_length_ = max_length; }

    void SetMaxPathLength(int max_length) noexcept {
        set_max_path_length(max_length);
    }

    /**
     * @brief 获取最大搜索节点数限制
     */
    int get_max_path_length() const noexcept { return max_path_length_; }

    int GetMaxPathLength() const noexcept {
        return get_max_path_length();
    }

private:
    const IWalkabilityProvider& walkability_provider_;
    int max_path_length_ = 1000;  // 最大节点数限制

    /**
     * @brief 计算曼哈顿距离启发式
     * @param a 起点
     * @param b 终点
     * @return 启发式代价值
     *
     * 启发式函数必须是可采纳的 (admissible)：
     * h(n) <= 实际代价(n到目标)
     *
     * 使用曼哈顿距离乘以10（最小移动代价）
     */
    static int manhattan_distance(const Position& a, const Position& b);

    /**
     * @brief 获取相邻可行走位置
     * @param pos 当前位置
     * @return 相邻可行走位置列表
     *
     * 支持 8 方向移动，对角线移动需要两个相邻格子都可行走。
     */
    std::vector<Position> get_neighbors(const Position& pos) const;

    /**
     * @brief 重建路径
     * @param came_from 父节点映射
     * @param start 起点
     * @param end 终点
     * @return 从起点到终点的路径
     */
    std::vector<Position> reconstruct_path(
        const std::unordered_map<uint64_t, Position>& came_from,
        const Position& start,
        const Position& end) const;

    /**
     * @brief 位置转换为一维哈希键
     * @param pos 位置坐标
     * @return 一维哈希键值（uint64_t防止溢出）
     */
    uint64_t pos_to_hash_key(const Position& pos) const;
};

} // namespace mir2::game::map

#endif // LEGEND2_CLIENT_GAME_MAP_PATH_FINDER_H
