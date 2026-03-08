/**
 * @file map_attributes.h
 * @brief 地图属性定义
 */

#ifndef MIR2_GAME_MAP_MAP_ATTRIBUTES_H_
#define MIR2_GAME_MAP_MAP_ATTRIBUTES_H_

#include <cstdint>
#include <string>
#include <vector>

namespace mir2::game::map {

/**
 * @brief 安全区定义
 */
struct SafeZone {
  int32_t x = 0;
  int32_t y = 0;
  int32_t radius = 0;

  /**
   * @brief 判断坐标是否在安全区内
   */
  bool Contains(int32_t pos_x, int32_t pos_y) const {
    const int64_t dx = static_cast<int64_t>(pos_x) - x;
    const int64_t dy = static_cast<int64_t>(pos_y) - y;
    const int64_t r = radius > 0 ? radius : 0;
    return dx * dx + dy * dy <= r * r;
  }
};

/**
 * @brief 进入地图的任务要求
 */
struct QuestRequirement {
  int32_t quest_id = 0;
  int32_t quest_value = 0;
};

/**
 * @brief 地图属性
 */
struct MapAttributes {
  bool is_safe_zone = false;
  bool is_pk_zone = false;
  bool no_teleport = false;
  bool no_drug = false;
  bool is_dark_map = false;
  bool no_recall = false;
  bool no_random_move = false;
  bool fight_zone = false;
  bool fight3_zone = false;
  int32_t min_level = 1;
  int32_t max_level = 255;
  int32_t mine_map = 0;
  int32_t dark_level = 0;
  float exp_rate = 1.0f;
  float drop_rate = 1.0f;
  std::vector<QuestRequirement> quest_requirements;
  std::string home_map;
  int32_t home_x = 0;
  int32_t home_y = 0;
  std::string pk_village_map;
  int32_t pk_village_x = 0;
  int32_t pk_village_y = 0;
  std::vector<SafeZone> safe_zones;
};

}  // namespace mir2::game::map

#endif  // MIR2_GAME_MAP_MAP_ATTRIBUTES_H_
