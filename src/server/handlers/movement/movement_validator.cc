/**
 * @file movement_validator.cc
 * @brief 服务器端移动验证器实现
 */

#include "handlers/movement/movement_validator.h"

#include <algorithm>
#include <cmath>

namespace legend2::handlers {

bool MovementValidator::ValidateWalk(mir2::game::map::MapInstance* map,
                                     int32_t x,
                                     int32_t y) {
  if (!map) {
    return false;
  }
  if (!map->IsValidPosition(x, y)) {
    return false;
  }
  return map->IsWalkable(x, y);
}

bool MovementValidator::ValidateFly(mir2::game::map::MapInstance* map,
                                    int32_t x,
                                    int32_t y) {
  if (!map) {
    return false;
  }
  if (!map->IsValidPosition(x, y)) {
    return false;
  }

  const auto* tile = map->GetTileInfo(x, y);
  if (tile) {
    return (tile->fr_img & 0x8000) == 0;
  }

  return map->IsWalkable(x, y);
}

MovementValidator::MovementValidator(const mir2::game::map::MapInstance& map_instance,
                                     Config config)
    : map_instance_(map_instance), config_(config) {}

mir2::common::ErrorCode MovementValidator::Validate(const mir2::common::Position& from,
                                                    const mir2::common::Position& to,
                                                    int speed,
                                                    int64_t last_move_time_ms,
                                                    int64_t current_time_ms) const {
  if (from == to) {
    return mir2::common::ErrorCode::kOk;
  }

  const int dx = std::abs(to.x - from.x);
  const int dy = std::abs(to.y - from.y);
  const int steps = std::max(dx, dy);
  if (config_.max_steps > 0 && steps > config_.max_steps) {
    return mir2::common::ErrorCode::kTargetOutOfRange;
  }

  if (last_move_time_ms > 0 && (dx > 0 || dy > 0)) {
    if (speed <= 0) {
      return mir2::common::ErrorCode::kSpeedViolation;
    }
    const int64_t elapsed_ms = current_time_ms - last_move_time_ms;
    if (elapsed_ms <= 0) {
      return mir2::common::ErrorCode::kSpeedViolation;
    }
    const double distance = std::hypot(static_cast<double>(to.x - from.x),
                                       static_cast<double>(to.y - from.y));
    const double elapsed_seconds = static_cast<double>(elapsed_ms) / 1000.0;
    const double actual_speed = distance / elapsed_seconds;
    const double max_speed = static_cast<double>(speed) * config_.speed_tolerance;
    if (actual_speed > max_speed) {
      return mir2::common::ErrorCode::kSpeedViolation;
    }
  }

  const auto path = TracePath(from, to);
  mir2::common::Position prev = path.front();
  for (size_t i = 0; i < path.size(); ++i) {
    const auto& pos = path[i];
    if (!map_instance_.IsWalkable(pos.x, pos.y)) {
      return mir2::common::ErrorCode::kPathBlocked;
    }
    if (i > 0 && IsDiagonalBlocked(prev, pos)) {
      return mir2::common::ErrorCode::kInvalidPath;
    }
    prev = pos;
  }

  return mir2::common::ErrorCode::kOk;
}

std::vector<mir2::common::Position> MovementValidator::TracePath(
    const mir2::common::Position& from,
    const mir2::common::Position& to) {
  std::vector<mir2::common::Position> path;
  int x0 = from.x;
  int y0 = from.y;
  const int x1 = to.x;
  const int y1 = to.y;

  const int dx = std::abs(x1 - x0);
  const int sx = (x0 < x1) ? 1 : -1;
  const int dy = -std::abs(y1 - y0);
  const int sy = (y0 < y1) ? 1 : -1;
  int err = dx + dy;

  while (true) {
    path.push_back({x0, y0});
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }

  return path;
}

bool MovementValidator::IsDiagonalBlocked(const mir2::common::Position& from,
                                          const mir2::common::Position& to) const {
  const int dx = to.x - from.x;
  const int dy = to.y - from.y;
  if (std::abs(dx) != 1 || std::abs(dy) != 1) {
    return false;
  }
  if (!map_instance_.IsWalkable(from.x, to.y)) {
    return true;
  }
  if (!map_instance_.IsWalkable(to.x, from.y)) {
    return true;
  }
  return false;
}

}  // namespace legend2::handlers
