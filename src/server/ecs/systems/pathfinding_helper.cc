/**
 * @file pathfinding_helper.cc
 * @brief 寻路辅助实现
 */

#include "ecs/systems/pathfinding_helper.h"
#include <cmath>

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

std::vector<mir2::common::Position> PathfindingHelper::FindPath(
    int32_t start_x, int32_t start_y,
    int32_t end_x, int32_t end_y,
    int32_t max_steps) {
    
    // 简化实现：直线路径
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

}  // namespace mir2::ecs
