/**
 * @file pathfinding_helper.h
 * @brief 寻路辅助系统
 */

#ifndef MIR2_ECS_SYSTEMS_PATHFINDING_HELPER_H
#define MIR2_ECS_SYSTEMS_PATHFINDING_HELPER_H

#include <vector>
#include <cstdint>
#include "common/types.h"

namespace mir2::ecs {

/**
 * @brief 寻路辅助类
 */
class PathfindingHelper {
public:
    /**
     * @brief 简单寻路：朝目标移动一步
     * @return 下一步位置
     */
    static mir2::common::Position GotoTargetXY(
        int32_t current_x, int32_t current_y,
        int32_t target_x, int32_t target_y);

    /**
     * @brief A*寻路算法
     * @return 路径点序列
     */
    static std::vector<mir2::common::Position> FindPath(
        int32_t start_x, int32_t start_y,
        int32_t end_x, int32_t end_y,
        int32_t max_steps = 80);

    /**
     * @brief 计算曼哈顿距离
     */
    static int32_t ManhattanDistance(
        int32_t x1, int32_t y1,
        int32_t x2, int32_t y2);
};

}  // namespace mir2::ecs

#endif  // MIR2_ECS_SYSTEMS_PATHFINDING_HELPER_H
