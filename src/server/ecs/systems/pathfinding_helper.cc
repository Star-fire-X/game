/**
 * @file pathfinding_helper.cc
 * @brief 寻路辅助实现
 */

#include "ecs/systems/pathfinding_helper.h"
#include <cmath>
#include <queue>
#include <unordered_map>
#include <algorithm>

namespace mir2::ecs {

mir2::common::Position PathfindingHelper::GotoTargetXY(
    int32_t current_x, int32_t current_y,
    int32_t target_x, int32_t target_y) {
    
    int32_t dx = (target_x > current_x) ? 1 : (target_x < current_x) ? -1 : 0;
    int32_t dy = (target_y > current_y) ? 1 : (target_y < current_y) ? -1 : 0;
    
    return {current_x + dx, current_y + dy};
}

int32_t PathfindingHelper::ManhattanDistance(
    int32_t x1, int32_t y1,
    int32_t x2, int32_t y2) {
    return std::abs(x1 - x2) + std::abs(y1 - y2);
}

int32_t PathfindingHelper::ChebyshevDistance(
    int32_t x1, int32_t y1,
    int32_t x2, int32_t y2) {
    return std::max(std::abs(x1 - x2), std::abs(y1 - y2));
}

std::vector<mir2::common::Position> PathfindingHelper::FindPath(
    int32_t start_x, int32_t start_y,
    int32_t end_x, int32_t end_y,
    int32_t max_steps) {
    return FindPathStraightLine(start_x, start_y, end_x, end_y, max_steps);
}

std::vector<mir2::common::Position> PathfindingHelper::FindPathStraightLine(
    int32_t start_x, int32_t start_y,
    int32_t end_x, int32_t end_y,
    int32_t max_steps) {
    // 简化实现：直线路径（无障碍物检查）
    std::vector<mir2::common::Position> path;
    int32_t x = start_x, y = start_y;

    for (int32_t i = 0; i < max_steps; ++i) {
        if (x == end_x && y == end_y) break;

        auto next = GotoTargetXY(x, y, end_x, end_y);
        path.push_back(next);
        x = next.x;
        y = next.y;
    }

    return path;
}

namespace {

struct AStarNode {
    int32_t x, y;
    int32_t g_cost;  // 从起点到当前节点的代价
    int32_t f_cost;  // g_cost + h_cost（启发式估计）
    int32_t parent_x, parent_y;

    bool operator>(const AStarNode& other) const {
        return f_cost > other.f_cost;
    }
};

inline int64_t PackCoord(int32_t x, int32_t y) {
    return (static_cast<int64_t>(x) << 32) | static_cast<uint32_t>(y);
}

// 8方向移动偏移
constexpr int32_t kDirX[] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int32_t kDirY[] = {-1, -1, 0, 1, 1, 1, 0, -1};
constexpr int32_t kDirCost[] = {10, 14, 10, 14, 10, 14, 10, 14};  // 直线10，对角14

}  // namespace

std::vector<mir2::common::Position> PathfindingHelper::FindPath(
    int32_t start_x, int32_t start_y,
    int32_t end_x, int32_t end_y,
    const WalkableChecker& walkable_checker,
    int32_t max_steps) {

    std::vector<mir2::common::Position> path;

    // 起点或终点不可行走
    if (!walkable_checker(start_x, start_y) || !walkable_checker(end_x, end_y)) {
        return path;
    }

    // 已在目标位置
    if (start_x == end_x && start_y == end_y) {
        return path;
    }

    // 开放列表（优先队列）
    std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> open_list;

    // 已访问节点（坐标 -> g_cost）
    std::unordered_map<int64_t, int32_t> closed_set;

    // 父节点记录（坐标 -> 父坐标）
    std::unordered_map<int64_t, int64_t> came_from;

    // 初始化起点
    int32_t h_start = ChebyshevDistance(start_x, start_y, end_x, end_y) * 10;
    open_list.push({start_x, start_y, 0, h_start, start_x, start_y});

    int32_t nodes_explored = 0;

    while (!open_list.empty() && nodes_explored < max_steps * 10) {
        AStarNode current = open_list.top();
        open_list.pop();

        int64_t current_key = PackCoord(current.x, current.y);

        // 跳过已处理的节点
        auto it = closed_set.find(current_key);
        if (it != closed_set.end() && it->second <= current.g_cost) {
            continue;
        }
        closed_set[current_key] = current.g_cost;
        came_from[current_key] = PackCoord(current.parent_x, current.parent_y);

        ++nodes_explored;

        // 到达目标
        if (current.x == end_x && current.y == end_y) {
            // 回溯路径
            std::vector<mir2::common::Position> reverse_path;
            int64_t key = current_key;
            int64_t start_key = PackCoord(start_x, start_y);

            while (key != start_key) {
                int32_t px = static_cast<int32_t>(key >> 32);
                int32_t py = static_cast<int32_t>(key & 0xFFFFFFFF);
                reverse_path.push_back({px, py});
                key = came_from[key];
            }

            // 反转路径
            path.reserve(reverse_path.size());
            for (auto rit = reverse_path.rbegin(); rit != reverse_path.rend(); ++rit) {
                path.push_back(*rit);
                if (static_cast<int32_t>(path.size()) >= max_steps) break;
            }
            return path;
        }

        // 探索8个方向的邻居
        for (int dir = 0; dir < 8; ++dir) {
            int32_t nx = current.x + kDirX[dir];
            int32_t ny = current.y + kDirY[dir];

            if (!walkable_checker(nx, ny)) {
                continue;
            }

            // 对角移动需要检查相邻格子
            if (dir % 2 == 1) {
                int prev_dir = (dir + 7) % 8;
                int next_dir = (dir + 1) % 8;
                if (!walkable_checker(current.x + kDirX[prev_dir], current.y + kDirY[prev_dir]) ||
                    !walkable_checker(current.x + kDirX[next_dir], current.y + kDirY[next_dir])) {
                    continue;
                }
            }

            int32_t new_g = current.g_cost + kDirCost[dir];
            int64_t neighbor_key = PackCoord(nx, ny);

            auto closed_it = closed_set.find(neighbor_key);
            if (closed_it != closed_set.end() && closed_it->second <= new_g) {
                continue;
            }

            int32_t h = ChebyshevDistance(nx, ny, end_x, end_y) * 10;
            open_list.push({nx, ny, new_g, new_g + h, current.x, current.y});
        }
    }

    return path;  // 无法到达
}

}  // namespace mir2::ecs
