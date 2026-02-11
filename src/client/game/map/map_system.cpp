/**
 * @file map_system.cpp
 * @brief Legend2 客户端地图系统实现
 *
 * 本文件实现地图管理系统的核心功能，包括：
 * - 地图文件加载和解析
 * - 可行走性矩阵构建
 * - 寻路委托（PathFinder）
 * - 传送门检测和地图切换
 */

#include "map_system.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace mir2::game::map {

using mir2::client::MapTile;

namespace {
template <typename T>
bool is_ok(const std::variant<T, ErrorCode>& result) {
  return std::holds_alternative<T>(result);
}

template <typename T>
const T* get_ok(const std::variant<T, ErrorCode>& result) {
  return std::get_if<T>(&result);
}

template <typename T>
ErrorCode get_error(const std::variant<T, ErrorCode>& result) {
  if (const auto* code = std::get_if<ErrorCode>(&result)) {
    return *code;
  }
  return ErrorCode::SUCCESS;
}

template <typename T>
Result<T> make_ok(T value) {
  return Result<T>(std::move(value));
}

inline Result<void> make_ok() { return Result<void>(std::monostate{}); }

template <typename T>
Result<T> make_error(ErrorCode code) {
  return Result<T>(code);
}

std::string NormalizeMapDirectory(std::string directory) {
  if (directory.empty()) {
    return "Map/";
  }
  if (directory.back() != '/' && directory.back() != '\\') {
    directory.push_back('/');
  }
  return directory;
}

std::vector<std::string> BuildMapDirCandidates(const std::string& map_directory) {
  std::vector<std::string> candidates;
  candidates.reserve(5);
  candidates.push_back(NormalizeMapDirectory(map_directory));

  constexpr std::array<const char*, 4> kFallbackDirs = {
      "Map/", "../Map/", "../../Map/", "./Map/"};
  for (const char* fallback : kFallbackDirs) {
    if (candidates.back() != fallback &&
        std::find(candidates.begin(), candidates.end(), fallback) ==
            candidates.end()) {
      candidates.emplace_back(fallback);
    }
  }
  return candidates;
}
}  // namespace

// =============================================================================
// Portal JSON 序列化
// =============================================================================

/**
 * @brief 将 Portal 序列化为 JSON
 * @param j 输出的 JSON 对象
 * @param p 传送门对象
 */
void to_json(nlohmann::json& j, const Portal& p) {
    j = nlohmann::json{
        {"source_pos", p.source_pos},
        {"target_map_id", p.target_map_id},
        {"target_pos", p.target_pos}
    };
}

/**
 * @brief 从 JSON 反序列化 Portal
 * @param j 输入的 JSON 对象
 * @param p 输出的传送门对象
 */
void from_json(const nlohmann::json& j, Portal& p) {
    j.at("source_pos").get_to(p.source_pos);
    j.at("target_map_id").get_to(p.target_map_id);
    j.at("target_pos").get_to(p.target_pos);
}

// =============================================================================
// MapSystem 类实现
// =============================================================================

/**
 * @brief 构造函数 - 使用默认地图目录
 * @param resource_manager 资源管理器引用
 */
MapSystem::MapSystem(ResourceManager& resource_manager)
    : resource_manager_(resource_manager),
      map_directory_(NormalizeMapDirectory("Map/")),
      portal_manager_(std::make_unique<PortalManager>()) {}

/**
 * @brief 构造函数 - 指定地图目录
 * @param resource_manager 资源管理器引用
 * @param map_directory 地图文件目录路径
 */
MapSystem::MapSystem(ResourceManager& resource_manager, const std::string& map_directory)
    : resource_manager_(resource_manager),
      map_directory_(NormalizeMapDirectory(map_directory)),
      portal_manager_(std::make_unique<PortalManager>()) {}

/**
 * @brief 设置地图文件目录
 * @param directory 目录路径
 *
 * 自动确保路径以分隔符结尾。
 */
void MapSystem::set_map_directory(const std::string& directory) {
  map_directory_ = NormalizeMapDirectory(directory);
}

/**
 * @brief 加载地图（Result 版本）
 * @param map_id 地图 ID
 * @return Result<void> 成功或错误码
 *
 * 加载流程：
 * 1. 尝试多个可能的地图目录路径
 * 2. 使用资源管理器加载地图数据
 * 3. 构建可行走性矩阵
 * 4. 自动检测传送门位置
 */
Result<void> MapSystem::load_map_result(uint32_t map_id) {
  const std::vector<std::string> map_dirs = BuildMapDirCandidates(map_directory_);

  std::string map_path;
  for (const auto& dir : map_dirs) {
    std::string test_path = dir + std::to_string(map_id) + ".map";
    std::ifstream test(test_path);
    if (test.good()) {
      map_path = test_path;
      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Found map at: %s",
                  map_path.c_str());
      break;
    }
  }

  if (map_path.empty()) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not find map %u",
                static_cast<unsigned int>(map_id));
    return make_error<void>(ErrorCode::INVALID_ACTION);
  }

  auto map_data = resource_manager_.load_map(map_path);
  if (!map_data) {
    return make_error<void>(ErrorCode::INTERNAL_ERROR);
  }

  map_data_ = std::make_unique<MapData>(*map_data);
  current_map_id_ = map_id;

  build_walkability_matrix();

  path_finder_ = std::make_unique<PathFinder>(*this);
  path_finder_->set_max_path_length(max_path_length_);

  if (!portal_manager_) {
    portal_manager_ = std::make_unique<PortalManager>();
  } else {
    portal_manager_->clear();
  }

  for (int y = 0; y < map_data_->height; ++y) {
    for (int x = 0; x < map_data_->width; ++x) {
      const MapTile* tile = map_data_->get_tile(x, y);
      if (tile && tile->has_portal()) {
        Portal portal;
        portal.source_pos = {x, y};
        portal.target_map_id = 0;
        portal.target_pos = {0, 0};
        portal_manager_->register_portal(portal);
      }
    }
  }

  return make_ok();
}

/**
 * @brief 加载地图
 * @param map_id 地图 ID
 * @return true 加载成功
 */
bool MapSystem::load_map(uint32_t map_id) {
  return is_ok(load_map_result(map_id));
}

// -----------------------------------------------------------------------------
// 位置验证方法
// -----------------------------------------------------------------------------

/**
 * @brief 检查位置是否在地图范围内
 * @param pos 位置坐标
 * @return true 如果位置有效
 */
bool MapSystem::is_valid_position(const Position& pos) const {
    if (!map_data_) {
        return false;
    }
    return pos.x >= 0 && pos.x < map_data_->width &&
           pos.y >= 0 && pos.y < map_data_->height;
}

/**
 * @brief 检查位置是否可行走
 * @param pos 位置坐标
 * @return true 如果可以行走
 *
 * 同时检查位置有效性和可行走性矩阵。
 */
bool MapSystem::is_walkable(const Position& pos) const {
    if (!is_valid_position(pos)) {
        return false;
    }
    if (walkability_matrix_.empty() || walkability_width_ <= 0 ||
        walkability_height_ <= 0) {
        return false;
    }
    if (pos.x < 0 || pos.y < 0 || pos.x >= walkability_width_ ||
        pos.y >= walkability_height_) {
        return false;
    }
    const size_t index =
        static_cast<size_t>(pos.y) * static_cast<size_t>(walkability_width_) +
        static_cast<size_t>(pos.x);
    if (index >= walkability_matrix_.size()) {
        return false;
    }
    return walkability_matrix_[index] != 0;
}

bool MapSystem::is_valid_position(int x, int y) const {
    return is_valid_position(Position{x, y});
}

bool MapSystem::is_walkable(int x, int y) const {
    return is_walkable(Position{x, y});
}

/**
 * @brief 获取地图尺寸（Result 版本）
 * @return Result<Size> 地图宽高或错误码
 */
Result<Size> MapSystem::get_map_size_result() const {
    if (!map_data_) {
        return make_error<Size>(ErrorCode::INVALID_ACTION);
    }
    return make_ok<Size>(Size{map_data_->width, map_data_->height});
}

/**
 * @brief 获取地图尺寸
 * @return Size 地图宽高
 */
Size MapSystem::get_map_size() const {
    auto result = get_map_size_result();
    if (const auto* size = get_ok(result)) {
        return *size;
    }
    return {0, 0};
}

/**
 * @brief 获取可行走性矩阵
 * @return 二维布尔矩阵的常量引用
 */
const std::vector<uint8_t>& MapSystem::get_walkability_matrix() const noexcept {
    return walkability_matrix_;
}

// -----------------------------------------------------------------------------
// 传送门管理
// -----------------------------------------------------------------------------

/**
 * @brief 获取指定位置的传送门（Result 版本）
 * @param pos 位置坐标
 * @return Result<Portal> 存在返回传送门，否则返回错误码
 */
Result<Portal> MapSystem::get_portal_at_result(const Position& pos) const {
    if (!is_valid_position(pos)) {
        return make_error<Portal>(ErrorCode::INVALID_POSITION);
    }

    if (!portal_manager_) {
        return make_error<Portal>(ErrorCode::INTERNAL_ERROR);
    }

    auto portal = portal_manager_->get_portal_at(pos);
    if (!portal.has_value()) {
        return make_error<Portal>(ErrorCode::INVALID_ACTION);
    }
    return make_ok(*portal);
}

/**
 * @brief 获取指定位置的传送门
 * @param pos 位置坐标
 * @return std::optional<Portal> 存在返回传送门，否则返回 nullopt
 */
std::optional<Portal> MapSystem::get_portal_at(const Position& pos) const {
    auto result = get_portal_at_result(pos);
    if (const auto* portal = get_ok(result)) {
        return *portal;
    }
    return std::nullopt;
}

/**
 * @brief 检查位置是否有传送门
 * @param pos 位置坐标
 * @return true 如果有传送门
 */
bool MapSystem::has_portal(const Position& pos) const {
    if (!portal_manager_) {
        return false;
    }
    return portal_manager_->has_portal(pos);
}

/**
 * @brief 注册传送门
 * @param portal 传送门配置
 *
 * 如果位置已有传送门则更新，否则添加新传送门。
 */
void MapSystem::register_portal(const Portal& portal) {
    if (!portal_manager_) {
        portal_manager_ = std::make_unique<PortalManager>();
    }
    portal_manager_->register_portal(portal);
}

/**
 * @brief 清除所有传送门
 */
void MapSystem::clear_portals() {
    if (portal_manager_) {
        portal_manager_->clear();
    }
}

// -----------------------------------------------------------------------------
// 内部辅助方法
// -----------------------------------------------------------------------------

/**
 * @brief 构建可行走性矩阵
 *
 * 从地图数据生成二维布尔矩阵，用于快速查询可行走性。
 */
void MapSystem::build_walkability_matrix() {
    if (!map_data_) {
        walkability_matrix_.clear();
        walkability_width_ = 0;
        walkability_height_ = 0;
        return;
    }

    walkability_width_ = map_data_->width;
    walkability_height_ = map_data_->height;
    if (walkability_width_ <= 0 || walkability_height_ <= 0) {
        walkability_matrix_.clear();
        walkability_width_ = 0;
        walkability_height_ = 0;
        return;
    }

    const size_t tile_count = static_cast<size_t>(walkability_width_) *
                              static_cast<size_t>(walkability_height_);
    walkability_matrix_.assign(tile_count, 0);
    for (int y = 0; y < walkability_height_; ++y) {
        for (int x = 0; x < walkability_width_; ++x) {
            const size_t index =
                static_cast<size_t>(y) * static_cast<size_t>(walkability_width_) +
                static_cast<size_t>(x);
            walkability_matrix_[index] =
                static_cast<uint8_t>(map_data_->is_walkable(x, y) ? 1 : 0);
        }
    }
}

/**
 * @brief 获取相邻可行走位置
 * @param pos 当前位置
 * @return 相邻可行走位置列表
 *
 * 支持 8 方向移动，对角线移动需要两个相邻格子都可行走。
 */
std::vector<Position> MapSystem::get_neighbors(const Position& pos) const {
    std::vector<Position> neighbors;
    neighbors.reserve(8);

    // 8-directional movement
    static const int dx[] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int dy[] = {-1, -1, 0, 1, 1, 1, 0, -1};

    for (int i = 0; i < 8; ++i) {
        Position neighbor = {pos.x + dx[i], pos.y + dy[i]};
        if (is_walkable(neighbor)) {
            // For diagonal movement, check that both adjacent cardinal tiles are walkable
            // to prevent cutting corners through walls
            if (dx[i] != 0 && dy[i] != 0) {
                Position adj1 = {pos.x + dx[i], pos.y};
                Position adj2 = {pos.x, pos.y + dy[i]};
                if (!is_walkable(adj1) || !is_walkable(adj2)) {
                    continue;  // Can't cut corners
                }
            }
            neighbors.push_back(neighbor);
        }
    }

    return neighbors;
}

// -----------------------------------------------------------------------------
// PathFinder 寻路委托
// -----------------------------------------------------------------------------

/**
 * @brief 寻找从起点到终点的路径
 * @param start 起点位置
 * @param end 终点位置
 * @return 路径位置列表（包含起点和终点），无路径返回空列表
 */
Result<std::vector<Position>> MapSystem::find_path_result(const Position& start,
                                                          const Position& end) const {
    // Validate start and end positions
    if (!is_valid_position(start) || !is_valid_position(end)) {
        return make_error<std::vector<Position>>(ErrorCode::INVALID_POSITION);
    }
    if (!is_walkable(start) || !is_walkable(end)) {
        return make_error<std::vector<Position>>(ErrorCode::POSITION_NOT_WALKABLE);
    }

    // If start equals end, return single-element path
    if (start == end) {
        return make_ok<std::vector<Position>>(std::vector<Position>{start});
    }

    if (!path_finder_) {
        return make_error<std::vector<Position>>(ErrorCode::INTERNAL_ERROR);
    }

    auto path = path_finder_->find_path(start, end);
    if (path.empty()) {
        return make_error<std::vector<Position>>(ErrorCode::kInvalidPath);
    }
    return make_ok(std::move(path));
}

std::vector<Position> MapSystem::find_path(const Position& start,
                                           const Position& end) const {
    auto result = find_path_result(start, end);
    if (const auto* path = get_ok(result)) {
        return *path;
    }
    return {};
}

/**
 * @brief 获取地图文件路径
 * @param map_id 地图 ID
 * @return 完整的地图文件路径
 */
std::string MapSystem::get_map_file_path(uint32_t map_id) const {
    return map_directory_ + std::to_string(map_id) + ".map";
}

// -----------------------------------------------------------------------------
// 传送门传送功能
// -----------------------------------------------------------------------------

/**
 * @brief 检查传送门传送条件（Result 版本）
 * @param pos 当前位置
 * @return Result<PortalTransition> 检查结果
 *
 * 验证位置有效性、传送门存在性和目标配置。
 */
Result<PortalTransition> MapSystem::check_portal_transition_result(const Position& pos) const {
    if (!is_valid_position(pos)) {
        return make_error<PortalTransition>(ErrorCode::INVALID_POSITION);
    }

    auto portal_result = get_portal_at_result(pos);
    const auto* portal = get_ok(portal_result);
    if (!portal) {
        return make_error<PortalTransition>(get_error(portal_result));
    }

    if (portal->target_map_id == 0) {
        return make_error<PortalTransition>(ErrorCode::INVALID_ACTION);
    }

    return make_ok<PortalTransition>(PortalTransition{portal->target_map_id, portal->target_pos});
}

/**
 * @brief 检查传送门传送条件
 * @param pos 当前位置
 * @return PortalTransitionResult 检查结果
 */
PortalTransitionResult MapSystem::check_portal_transition(const Position& pos) const {
    auto result = check_portal_transition_result(pos);
    if (const auto* transition = get_ok(result)) {
        return PortalTransitionResult::ok(transition->target_map_id, transition->target_pos);
    }
    return PortalTransitionResult::fail(get_error(result));
}

/**
 * @brief 执行传送门传送（Result 版本）
 * @param pos 当前位置
 * @return Result<PortalTransition> 传送结果
 *
 * 执行流程：
 * 1. 检查传送条件
 * 2. 加载目标地图
 * 3. 验证目标位置可行走
 */
Result<PortalTransition> MapSystem::execute_portal_transition_result(const Position& pos) {
    auto transition_result = check_portal_transition_result(pos);
    const auto* transition = get_ok(transition_result);
    if (!transition) {
        return make_error<PortalTransition>(get_error(transition_result));
    }

    auto load_result = load_map_result(transition->target_map_id);
    if (!is_ok(load_result)) {
        return make_error<PortalTransition>(get_error(load_result));
    }

    if (!is_valid_position(transition->target_pos)) {
        return make_error<PortalTransition>(ErrorCode::INVALID_POSITION);
    }

    if (!is_walkable(transition->target_pos)) {
        return make_error<PortalTransition>(ErrorCode::POSITION_NOT_WALKABLE);
    }

    return make_ok(*transition);
}

/**
 * @brief 执行传送门传送
 * @param pos 当前位置
 * @return PortalTransitionResult 传送结果
 */
PortalTransitionResult MapSystem::execute_portal_transition(const Position& pos) {
    auto result = execute_portal_transition_result(pos);
    if (const auto* transition = get_ok(result)) {
        return PortalTransitionResult::ok(transition->target_map_id, transition->target_pos);
    }
    return PortalTransitionResult::fail(get_error(result));
}

} // namespace mir2::game::map
