/**
 * @file path_finder.cpp
 * @brief PathFinder 类的实现
 */

#include "path_finder.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace mir2::game::map {

/**
 * @brief A* 寻路节点的内部定义
 */
struct PathNode {
    Position pos;       ///< 节点位置
    int g_cost;         ///< 从起点到此节点的代价
    int h_cost;         ///< 从此节点到目标的启发式代价

    int f_cost() const { return g_cost + h_cost; }

    bool operator>(const PathNode& other) const {
        return f_cost() > other.f_cost();
    }
};

// =============================================================================
// PathFinder 实现
// =============================================================================

PathFinder::PathFinder(const IWalkabilityProvider& walkability_provider)
    : walkability_provider_(walkability_provider) {
}

std::vector<Position> PathFinder::find_path(const Position& start,
                                           const Position& end) const {
    // 验证起点和终点可行走
    if (!walkability_provider_.is_walkable(start.x, start.y) ||
        !walkability_provider_.is_walkable(end.x, end.y)) {
        return {};
    }

    // 如果起点等于终点，返回单元素路径
    if (start == end) {
        return {start};
    }

    // A* 算法实现
    auto cmp = [](const PathNode& a, const PathNode& b) {
        return a.f_cost() > b.f_cost();
    };
    std::priority_queue<PathNode, std::vector<PathNode>, decltype(cmp)> open_set(cmp);

    // 已关闭的节点集合
    std::unordered_set<uint64_t> closed_set;

    // open_set 中节点的最佳 g_cost（用于去重/过期检测）
    std::unordered_map<uint64_t, int> open_set_g;

    // 用于路径重建的父节点映射
    std::unordered_map<uint64_t, Position> came_from;

    // 用于追踪 g_cost 的映射
    std::unordered_map<uint64_t, int> g_costs;

    // 初始化起点
    PathNode start_node;
    start_node.pos = start;
    start_node.g_cost = 0;
    start_node.h_cost = manhattan_distance(start, end);

    uint64_t start_key = pos_to_hash_key(start);
    open_set.push(start_node);
    g_costs[start_key] = 0;
    open_set_g[start_key] = 0;

    int nodes_explored = 0;

    while (!open_set.empty() && (max_path_length_ == 0 || nodes_explored < max_path_length_)) {
        PathNode current = open_set.top();
        open_set.pop();

        uint64_t current_key = pos_to_hash_key(current.pos);

        // 跳过过期/重复节点
        auto open_it = open_set_g.find(current_key);
        if (open_it == open_set_g.end() || current.g_cost != open_it->second) {
            continue;
        }
        open_set_g.erase(open_it);

        // 跳过已关闭的节点
        if (closed_set.count(current_key)) {
            continue;
        }

        // 检查是否到达目标
        if (current.pos == end) {
            return reconstruct_path(came_from, start, end);
        }

        // 加入关闭集合
        closed_set.insert(current_key);
        nodes_explored++;

        // 探索邻居
        for (const Position& neighbor : get_neighbors(current.pos)) {
            uint64_t neighbor_key = pos_to_hash_key(neighbor);

            // 跳过已关闭的邻居
            if (closed_set.count(neighbor_key)) {
                continue;
            }

            // 计算移动代价（直线=10, 对角线=14）
            int dx = std::abs(neighbor.x - current.pos.x);
            int dy = std::abs(neighbor.y - current.pos.y);
            int move_cost = (dx + dy == 2) ? 14 : 10;

            int tentative_g = current.g_cost + move_cost;

            // 检查这是否是更好的路径
            auto it = g_costs.find(neighbor_key);
            if (it != g_costs.end() && tentative_g >= it->second) {
                continue;  // 不是更好的路径
            }

            // open_set 中已有更好或相等的节点
            auto open_it_neighbor = open_set_g.find(neighbor_key);
            if (open_it_neighbor != open_set_g.end() && tentative_g >= open_it_neighbor->second) {
                continue;
            }

            // 这是目前最好的路径
            came_from[neighbor_key] = current.pos;
            g_costs[neighbor_key] = tentative_g;

            PathNode neighbor_node;
            neighbor_node.pos = neighbor;
            neighbor_node.g_cost = tentative_g;
            neighbor_node.h_cost = manhattan_distance(neighbor, end);

            open_set.push(neighbor_node);
            open_set_g[neighbor_key] = tentative_g;
        }
    }

    // 找不到路径
    return {};
}

int PathFinder::manhattan_distance(const Position& a, const Position& b) {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

std::vector<Position> PathFinder::get_neighbors(const Position& pos) const {
    std::vector<Position> neighbors;
    neighbors.reserve(8);

    // 8方向移动
    static const int dx[] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int dy[] = {-1, -1, 0, 1, 1, 1, 0, -1};

    for (int i = 0; i < 8; ++i) {
        Position neighbor = {pos.x + dx[i], pos.y + dy[i]};

        if (!walkability_provider_.is_walkable(neighbor.x, neighbor.y)) {
            continue;
        }

        // 对角线移动检查：两个相邻格子都必须可行走
        if (dx[i] != 0 && dy[i] != 0) {
            Position adj1 = {pos.x + dx[i], pos.y};
            Position adj2 = {pos.x, pos.y + dy[i]};
            if (!walkability_provider_.is_walkable(adj1.x, adj1.y) ||
                !walkability_provider_.is_walkable(adj2.x, adj2.y)) {
                continue;  // 无法切角
            }
        }

        neighbors.push_back(neighbor);
    }

    return neighbors;
}

std::vector<Position> PathFinder::reconstruct_path(
    const std::unordered_map<uint64_t, Position>& came_from,
    const Position& start,
    const Position& end) const {

    std::vector<Position> path;
    Position current = end;

    while (!(current == start)) {
        path.push_back(current);
        uint64_t key = pos_to_hash_key(current);
        auto it = came_from.find(key);
        if (it == came_from.end()) {
            // 不应该发生，但作为防守措施
            break;
        }
        current = it->second;
    }

    path.push_back(start);

    // 反转得到从起点到终点的路径
    std::reverse(path.begin(), path.end());

    return path;
}

uint64_t PathFinder::pos_to_hash_key(const Position& pos) const {
    // 使用 uint64_t 防止溢出
    // 最多支持 65536×65536 的地图（足够大）
    return (static_cast<uint64_t>(pos.y) << 16) | static_cast<uint64_t>(pos.x);
}

} // namespace mir2::game::map
