/**
 * @file map_system.h
 * @brief Legend2 客户端地图系统
 *
 * 本文件包含地图管理系统的定义，包括：
 * - 地图加载和管理
 * - 寻路（委托给 PathFinder）
 * - 传送门处理（委托给 PortalManager）
 */

#ifndef LEGEND2_CLIENT_MAP_SYSTEM_H
#define LEGEND2_CLIENT_MAP_SYSTEM_H

#include "common/types.h"
#include "client/resource/resource_loader.h"
#include "client/game/map/i_walkability_provider.h"
#include "client/game/map/path_finder.h"
#include "client/game/map/portal_manager.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#ifndef LEGEND2_CLIENT_MAPSYSTEM_ENABLE_LEGACY_PASCAL_API
#define LEGEND2_CLIENT_MAPSYSTEM_ENABLE_LEGACY_PASCAL_API 1
#endif

namespace mir2::game::map {

using mir2::common::ErrorCode;
using mir2::common::Position;
using mir2::common::Size;
using mir2::client::ResourceManager;
using mir2::client::MapData;

namespace detail {
template <typename T>
using ResultValue = std::conditional_t<std::is_void_v<T>, std::monostate, T>;
}  // namespace detail

/**
 * @brief 错误处理策略
 *
 * 所有可能失败的操作都提供两个版本：
 * 1. Result<T> 版本（_result 后缀）：返回 Result<T>，包含值或 ErrorCode
 * 2. 兼容版本：返回 bool/optional/空容器，用于简单场景
 *
 * 推荐使用 Result<T> 版本以获得详细的错误信息。
 *
 * Result<T> 用于统一错误处理:
 * - 成功时持有 T（或 void 对应的 std::monostate）
 * - 失败时持有 ErrorCode
 */
template <typename T>
using Result = std::variant<detail::ResultValue<T>, ErrorCode>;

// JSON serialization for Portal
void to_json(nlohmann::json& j, const Portal& p);
void from_json(const nlohmann::json& j, Portal& p);

// =============================================================================
// 传送门转换结果 (Portal Transition Result)
// =============================================================================

/**
 * @brief 传送门转换尝试的结果
 */
struct PortalTransitionResult {
    bool success = false;           ///< 转换是否成功
    uint32_t target_map_id = 0;     ///< 目标地图ID（成功时有效）
    Position target_pos;            ///< 目标位置（成功时有效）
    ErrorCode error_code = ErrorCode::SUCCESS;  ///< 错误码

    static PortalTransitionResult ok(uint32_t map_id, const Position& pos) {
        return {true, map_id, pos, ErrorCode::SUCCESS};
    }

    static PortalTransitionResult fail(ErrorCode code) {
        return {false, 0, {0, 0}, code};
    }
};

/**
 * @brief 传送门转换目标信息（Result 版）
 */
struct PortalTransition {
    uint32_t target_map_id = 0;
    Position target_pos;
};

// =============================================================================
// 地图系统实现 (Map System Implementation)
// =============================================================================

/**
 * @brief 地图系统的具体实现
 */
class MapSystem : public IWalkabilityProvider {
public:
    /**
     * @brief 构造函数
     * @param resource_manager 资源管理器引用（用于加载地图）
     */
    explicit MapSystem(ResourceManager& resource_manager);

    /**
     * @brief 带地图目录路径的构造函数
     * @param resource_manager 资源管理器引用
     * @param map_directory 地图文件所在目录
     */
    MapSystem(ResourceManager& resource_manager, const std::string& map_directory);

    ~MapSystem() = default;

    /// Result 版本: 加载地图
    Result<void> load_map_result(uint32_t map_id);

    /// 兼容接口: 成功返回 true
    bool load_map(uint32_t map_id);

    uint32_t get_current_map_id() const noexcept { return current_map_id_; }
    bool is_valid_position(const Position& pos) const;
    bool is_walkable(const Position& pos) const;
    bool is_valid_position(int x, int y) const override;
    bool is_walkable(int x, int y) const override;
    Size get_map_size() const override;
    std::vector<Position> get_neighbors(const Position& pos) const override;
    Result<Size> get_map_size_result() const;
    Result<std::vector<Position>> find_path_result(const Position& start,
                                                   const Position& end) const;
    /// 兼容接口: 失败返回空路径
    std::vector<Position> find_path(const Position& start,
                                    const Position& end) const;
    Result<Portal> get_portal_at_result(const Position& pos) const;
    /// 兼容接口: 未找到返回 nullopt
    std::optional<Portal> get_portal_at(const Position& pos) const;
    bool has_portal(const Position& pos) const;
    const std::vector<uint8_t>& get_walkability_matrix() const noexcept;

    /// Set the map directory path
    /// @param directory Path to map files directory
    void set_map_directory(const std::string& directory);

    /// Get the map directory path
    const std::string& get_map_directory() const noexcept { return map_directory_; }

    /// Register a portal on the current map
    /// @param portal Portal data to register
    void register_portal(const Portal& portal);

    /// Clear all registered portals
    void clear_portals();

    /// Get all portals on the current map
    const std::vector<Portal>& get_portals() const noexcept {
        return portal_manager_->get_portals();
    }

    /// Get the raw map data
    const MapData* get_map_data() const noexcept { return map_data_.get(); }

    /// Check if a map is currently loaded
    bool is_map_loaded() const noexcept { return map_data_ != nullptr; }

    /// Set maximum path length for pathfinding
    /// @param max_length Maximum number of nodes to explore
    void set_max_path_length(int max_length) {
        max_path_length_ = max_length;
        if (path_finder_) {
            path_finder_->set_max_path_length(max_length);
        }
    }

    /// Get maximum path length
    int get_max_path_length() const noexcept { return max_path_length_; }

    /// Check if a character at the given position can use a portal
    /// @param pos Current position to check for portal
    /// @return PortalTransitionResult with target map and position if portal exists
    Result<PortalTransition> check_portal_transition_result(const Position& pos) const;
    /// 兼容接口: 使用 PortalTransitionResult 表达错误
    PortalTransitionResult check_portal_transition(const Position& pos) const;

    /// Execute a portal transition - loads the target map
    /// @param pos Current position to check for portal
    /// @return PortalTransitionResult with success status
    Result<PortalTransition> execute_portal_transition_result(const Position& pos);
    /// 兼容接口: 使用 PortalTransitionResult 表达错误
    PortalTransitionResult execute_portal_transition(const Position& pos);

#if LEGEND2_CLIENT_MAPSYSTEM_ENABLE_LEGACY_PASCAL_API
    // TODO(remove after 2026-Q3): legacy PascalCase compatibility wrappers.
    Result<void> LoadMapResult(uint32_t map_id) { return load_map_result(map_id); }
    bool LoadMap(uint32_t map_id) { return load_map(map_id); }
    uint32_t GetCurrentMapId() const noexcept { return get_current_map_id(); }
    bool IsValidPosition(const Position& pos) const { return is_valid_position(pos); }
    bool IsWalkable(const Position& pos) const { return is_walkable(pos); }
    bool IsValidPosition(int x, int y) const { return is_valid_position(x, y); }
    bool IsWalkable(int x, int y) const { return is_walkable(x, y); }
    Size GetMapSize() const { return get_map_size(); }
    std::vector<Position> GetNeighbors(const Position& pos) const {
        return get_neighbors(pos);
    }
    Result<Size> GetMapSizeResult() const { return get_map_size_result(); }
    Result<std::vector<Position>> FindPathResult(const Position& start,
                                                 const Position& end) const {
        return find_path_result(start, end);
    }
    std::vector<Position> FindPath(const Position& start, const Position& end) const {
        return find_path(start, end);
    }
    Result<Portal> GetPortalAtResult(const Position& pos) const {
        return get_portal_at_result(pos);
    }
    std::optional<Portal> GetPortalAt(const Position& pos) const {
        return get_portal_at(pos);
    }
    bool HasPortal(const Position& pos) const { return has_portal(pos); }
    const std::vector<uint8_t>& GetWalkabilityMatrix() const noexcept {
        return get_walkability_matrix();
    }
    void SetMapDirectory(const std::string& directory) { set_map_directory(directory); }
    const std::string& GetMapDirectory() const noexcept { return get_map_directory(); }
    void RegisterPortal(const Portal& portal) { register_portal(portal); }
    void ClearPortals() { clear_portals(); }
    const std::vector<Portal>& GetPortals() const noexcept { return get_portals(); }
    const MapData* GetMapData() const noexcept { return get_map_data(); }
    bool IsMapLoaded() const noexcept { return is_map_loaded(); }
    void SetMaxPathLength(int max_length) { set_max_path_length(max_length); }
    int GetMaxPathLength() const noexcept { return get_max_path_length(); }
    Result<PortalTransition> CheckPortalTransitionResult(const Position& pos) const {
        return check_portal_transition_result(pos);
    }
    PortalTransitionResult CheckPortalTransition(const Position& pos) const {
        return check_portal_transition(pos);
    }
    Result<PortalTransition> ExecutePortalTransitionResult(const Position& pos) {
        return execute_portal_transition_result(pos);
    }
    PortalTransitionResult ExecutePortalTransition(const Position& pos) {
        return execute_portal_transition(pos);
    }
#endif

private:
    ResourceManager& resource_manager_;
    std::string map_directory_;
    uint32_t current_map_id_ = 0;
    std::unique_ptr<MapData> map_data_;
    std::vector<uint8_t> walkability_matrix_;
    int32_t walkability_width_ = 0;
    int32_t walkability_height_ = 0;
    int max_path_length_ = 1000;  // Maximum nodes to explore in pathfinding
    std::unique_ptr<PathFinder> path_finder_;
    std::unique_ptr<PortalManager> portal_manager_;

    /// Build walkability matrix from map data
    void build_walkability_matrix();

    /// Generate map file path from map ID
    std::string get_map_file_path(uint32_t map_id) const;
};

} // namespace mir2::game::map

#endif // LEGEND2_CLIENT_MAP_SYSTEM_H
