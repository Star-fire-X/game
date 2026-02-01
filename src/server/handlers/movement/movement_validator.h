/**
 * @file movement_validator.h
 * @brief 服务器端移动验证器
 */

#ifndef LEGEND2_SERVER_HANDLERS_MOVEMENT_VALIDATOR_H
#define LEGEND2_SERVER_HANDLERS_MOVEMENT_VALIDATOR_H

#include <cstdint>
#include <vector>

#include "server/common/error_codes.h"
#include "common/types/types.h"
#include "game/map/map_instance.h"

namespace legend2::handlers {

/**
 * @brief 服务器端移动验证器
 */
class MovementValidator {
 public:
  /**
   * @brief 检查指定坐标是否可行走
   */
  static bool ValidateWalk(mir2::game::map::MapInstance* map,
                           int32_t x,
                           int32_t y);

  /**
   * @brief 检查指定坐标是否可飞行
   */
  static bool ValidateFly(mir2::game::map::MapInstance* map,
                          int32_t x,
                          int32_t y);

  /**
   * @brief 配置项
   */
  struct Config {
    int max_steps;
    float speed_tolerance;

    Config(int max_steps = 10, float speed_tolerance = 1.2f)
        : max_steps(max_steps), speed_tolerance(speed_tolerance) {}
  };

  /**
   * @brief 构造函数
   *
   * @param map_instance 地图实例
   * @param config 配置项
   */
  MovementValidator(const mir2::game::map::MapInstance& map_instance,
                    Config config = Config());

  /**
   * @brief 验证移动请求
   *
   * @param from 起点坐标
   * @param to 终点坐标
   * @param speed 角色速度
   * @param last_move_time_ms 上次移动时间戳
   * @param current_time_ms 当前时间戳
   * @return 验证结果错误码
   */
  mir2::common::ErrorCode Validate(const mir2::common::Position& from,
                                   const mir2::common::Position& to,
                                   int speed,
                                   int64_t last_move_time_ms,
                                   int64_t current_time_ms) const;

  /**
   * @brief 生成 Bresenham 路径
   */
  static std::vector<mir2::common::Position> TracePath(const mir2::common::Position& from,
                                                  const mir2::common::Position& to);

 private:
  bool IsDiagonalBlocked(const mir2::common::Position& from,
                         const mir2::common::Position& to) const;

  const mir2::game::map::MapInstance& map_instance_;
  Config config_;
};

}  // namespace legend2::handlers

#endif  // LEGEND2_SERVER_HANDLERS_MOVEMENT_VALIDATOR_H
