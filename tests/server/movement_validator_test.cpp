/**
 * @file movement_validator_test.cpp
 * @brief MovementValidator 单元测试
 */

#include "handlers/movement/movement_validator.h"

#include <gtest/gtest.h>

#include <chrono>
#include <vector>

#include "game/map/map_instance.h"

namespace {

std::vector<uint8_t> BuildWalkability(int width,
                                      int height,
                                      const std::vector<mir2::common::Position>& blocked) {
  std::vector<uint8_t> walkability(
      static_cast<size_t>(width) * static_cast<size_t>(height), 1);
  for (const auto& pos : blocked) {
    if (pos.x < 0 || pos.y < 0 || pos.x >= width || pos.y >= height) {
      continue;
    }
    const size_t index = static_cast<size_t>(pos.y * width + pos.x);
    walkability[index] = 0;
  }
  return walkability;
}

mir2::game::map::MapTileData BuildTileData(
    int width,
    int height,
    const std::vector<mir2::common::Position>& no_fly) {
  mir2::game::map::MapTileData data;
  data.width = width;
  data.height = height;
  data.tiles.assign(static_cast<size_t>(width) * static_cast<size_t>(height),
                    mir2::game::map::TileInfo{});
  data.walkable.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 1);

  for (const auto& pos : no_fly) {
    if (pos.x < 0 || pos.y < 0 || pos.x >= width || pos.y >= height) {
      continue;
    }
    const size_t index = static_cast<size_t>(pos.y * width + pos.x);
    data.tiles[index].fr_img = 0x8000;
  }

  return data;
}

legend2::handlers::MovementValidator MakeValidator(
    const mir2::game::map::MapInstance& map,
    int max_steps = 20,
    float speed_tolerance = 1.2f) {
  legend2::handlers::MovementValidator::Config config;
  config.max_steps = max_steps;
  config.speed_tolerance = speed_tolerance;
  return legend2::handlers::MovementValidator(map, config);
}

}  // namespace

TEST(MovementValidatorTest, HappyPathStraightLine) {
  mir2::game::map::MapInstance map(
      1, 10, 10, mir2::game::map::AOIManager::kDefaultGridSize,
      BuildWalkability(10, 10, {}));
  auto validator = MakeValidator(map);

  const auto code = validator.Validate({1, 1}, {6, 1}, 5, 0, 0);
  EXPECT_EQ(code, mir2::common::ErrorCode::kOk);
}

TEST(MovementValidatorTest, HappyPathDiagonal) {
  mir2::game::map::MapInstance map(
      2, 10, 10, mir2::game::map::AOIManager::kDefaultGridSize,
      BuildWalkability(10, 10, {}));
  auto validator = MakeValidator(map);

  const auto code = validator.Validate({2, 2}, {5, 5}, 5, 0, 0);
  EXPECT_EQ(code, mir2::common::ErrorCode::kOk);
}

TEST(MovementValidatorTest, SamePositionIsOk) {
  mir2::game::map::MapInstance map(
      3, 5, 5, mir2::game::map::AOIManager::kDefaultGridSize,
      BuildWalkability(5, 5, {}));
  auto validator = MakeValidator(map);

  const auto code = validator.Validate({2, 2}, {2, 2}, 0, 1000, 1000);
  EXPECT_EQ(code, mir2::common::ErrorCode::kOk);
}

TEST(MovementValidatorTest, TargetOutOfRange) {
  mir2::game::map::MapInstance map(
      4, 10, 10, mir2::game::map::AOIManager::kDefaultGridSize,
      BuildWalkability(10, 10, {}));
  auto validator = MakeValidator(map, 3);

  const auto code = validator.Validate({1, 1}, {5, 1}, 5, 0, 0);
  EXPECT_EQ(code, mir2::common::ErrorCode::kTargetOutOfRange);
}

TEST(MovementValidatorTest, PathBlockedReturnsError) {
  mir2::game::map::MapInstance map(
      5, 10, 10, mir2::game::map::AOIManager::kDefaultGridSize,
      BuildWalkability(10, 10, {{3, 1}}));
  auto validator = MakeValidator(map);

  const auto code = validator.Validate({1, 1}, {6, 1}, 5, 0, 0);
  EXPECT_EQ(code, mir2::common::ErrorCode::kPathBlocked);
}

TEST(MovementValidatorTest, DiagonalWallBlocksMovement) {
  mir2::game::map::MapInstance map(
      6, 3, 3, mir2::game::map::AOIManager::kDefaultGridSize,
      BuildWalkability(3, 3, {{0, 1}}));
  auto validator = MakeValidator(map);

  const auto code = validator.Validate({0, 0}, {1, 1}, 5, 0, 0);
  EXPECT_EQ(code, mir2::common::ErrorCode::kInvalidPath);
}

TEST(MovementValidatorTest, SpeedViolationReturnsError) {
  mir2::game::map::MapInstance map(
      7, 20, 20, mir2::game::map::AOIManager::kDefaultGridSize,
      BuildWalkability(20, 20, {}));
  auto validator = MakeValidator(map, 50);

  const auto code = validator.Validate({1, 1}, {11, 1}, 3, 1000, 1500);
  EXPECT_EQ(code, mir2::common::ErrorCode::kSpeedViolation);
}

TEST(MovementValidatorTest, InvalidTimeDeltaIsSpeedViolation) {
  mir2::game::map::MapInstance map(
      8, 10, 10, mir2::game::map::AOIManager::kDefaultGridSize,
      BuildWalkability(10, 10, {}));
  auto validator = MakeValidator(map);

  const auto code = validator.Validate({1, 1}, {2, 1}, 5, 2000, 2000);
  EXPECT_EQ(code, mir2::common::ErrorCode::kSpeedViolation);
}

TEST(MovementValidatorTest, TracePathMatchesBresenham) {
  const auto path = legend2::handlers::MovementValidator::TracePath({0, 0}, {3, 2});
  std::vector<mir2::common::Position> expected = {{0, 0}, {1, 1}, {2, 1}, {3, 2}};
  EXPECT_EQ(path, expected);
}

TEST(MovementValidatorTest, PerformanceLongDistance) {
  mir2::game::map::MapInstance map(
      9, 200, 200, mir2::game::map::AOIManager::kDefaultGridSize,
      BuildWalkability(200, 200, {}));
  auto validator = MakeValidator(map, 300);

  const mir2::common::Position start{0, 0};
  const mir2::common::Position end{150, 75};
  constexpr int kIterations = 100;

  const auto begin = std::chrono::steady_clock::now();
  for (int i = 0; i < kIterations; ++i) {
    const auto code = validator.Validate(start, end, 10, 0, 0);
    ASSERT_EQ(code, mir2::common::ErrorCode::kOk);
  }
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - begin)
          .count();

  EXPECT_LT(elapsed_ms, 50);
}

TEST(MovementValidatorTest, ValidateFlyUsesTileDataFlag) {
  auto tile_data = BuildTileData(2, 2, {{1, 1}});
  mir2::game::map::MapInstance map(
      10, 2, 2, mir2::game::map::AOIManager::kDefaultGridSize,
      {}, std::move(tile_data));

  EXPECT_TRUE(legend2::handlers::MovementValidator::ValidateFly(&map, 0, 0));
  EXPECT_FALSE(legend2::handlers::MovementValidator::ValidateFly(&map, 1, 1));
}

TEST(MovementValidatorTest, ValidateFlyFallsBackToWalkable) {
  mir2::game::map::MapInstance map(
      11, 3, 3, mir2::game::map::AOIManager::kDefaultGridSize,
      BuildWalkability(3, 3, {{1, 1}}));

  EXPECT_TRUE(legend2::handlers::MovementValidator::ValidateFly(&map, 0, 0));
  EXPECT_FALSE(legend2::handlers::MovementValidator::ValidateFly(&map, 1, 1));
}

TEST(MovementValidatorTest, ValidateFlyRejectsOutOfBounds) {
  mir2::game::map::MapInstance map(
      12, 2, 2, mir2::game::map::AOIManager::kDefaultGridSize,
      BuildWalkability(2, 2, {}));

  EXPECT_FALSE(legend2::handlers::MovementValidator::ValidateFly(&map, -1, 0));
  EXPECT_FALSE(legend2::handlers::MovementValidator::ValidateFly(&map, 0, 2));
}
