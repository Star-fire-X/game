/**
 * @file portal_manager.h
 * @brief Legend2 传送门管理器（职责分离）
 *
 * PortalManager 专门负责：
 * - 传送门注册和查询
 * - 传送门有效性验证
 * - 传送门转换逻辑
 */

#ifndef LEGEND2_CLIENT_GAME_MAP_PORTAL_MANAGER_H
#define LEGEND2_CLIENT_GAME_MAP_PORTAL_MANAGER_H

#include "common/types.h"
#include <optional>
#include <vector>

namespace mir2::game::map {

using mir2::common::Position;

/**
 * @brief 传送门数据结构
 */
struct Portal {
    Position source_pos;        ///< 当前地图上的源位置
    uint32_t target_map_id;     ///< 目标地图ID
    Position target_pos;        ///< 目标地图上的位置

    bool operator==(const Portal& other) const noexcept {
        return source_pos == other.source_pos &&
               target_map_id == other.target_map_id &&
               target_pos == other.target_pos;
    }

    /**
     * @brief 验证传送门配置是否有效
     */
    bool is_valid() const noexcept {
        return target_map_id != 0;  // target_map_id 必须有效
    }
};

/**
 * @brief 传送门管理器
 *
 * 职责：
 * - 管理当前地图上的传送门列表
 * - 注册/注销传送门
 * - 验证传送门配置
 * - 查询传送门信息
 *
 * 注意：PortalManager 仅管理传送门数据，不处理地图加载。
 * 传送门转换中的地图加载应由 MapSystem 协调。
 */
class PortalManager {
public:
    /**
     * @brief 构造函数
     */
    PortalManager() = default;

    ~PortalManager() = default;

    // 禁止复制
    PortalManager(const PortalManager&) = delete;
    PortalManager& operator=(const PortalManager&) = delete;

    /**
     * @brief 注册传送门
     * @param portal 传送门配置
     * @return true 注册成功，false 配置无效
     *
     * 如果位置已有传送门则更新，否则添加新传送门。
     */
    bool register_portal(const Portal& portal);

    bool RegisterPortal(const Portal& portal) {
        return register_portal(portal);
    }

    /**
     * @brief 注销传送门
     * @param pos 传送门源位置
     * @return true 注销成功，false 位置不存在传送门
     */
    bool unregister_portal(const Position& pos);

    bool UnregisterPortal(const Position& pos) {
        return unregister_portal(pos);
    }

    /**
     * @brief 获取指定位置的传送门
     * @param pos 位置坐标
     * @return 传送门数据，未找到返回 nullopt
     */
    std::optional<Portal> get_portal_at(const Position& pos) const;

    std::optional<Portal> GetPortalAt(const Position& pos) const {
        return get_portal_at(pos);
    }

    /**
     * @brief 检查位置是否有传送门
     * @param pos 位置坐标
     * @return true 有传送门
     */
    bool has_portal(const Position& pos) const;

    bool HasPortal(const Position& pos) const {
        return has_portal(pos);
    }

    /**
     * @brief 获取所有传送门
     * @return 传送门列表的常量引用
     */
    const std::vector<Portal>& get_portals() const noexcept { return portals_; }

    const std::vector<Portal>& GetPortals() const noexcept {
        return get_portals();
    }

    /**
     * @brief 清除所有传送门
     *
     * 在加载新地图时调用。
     */
    void clear();

    void Clear() {
        clear();
    }

    /**
     * @brief 获取当前管理的传送门数量
     */
    size_t get_portal_count() const noexcept { return portals_.size(); }

    size_t GetPortalCount() const noexcept {
        return get_portal_count();
    }

private:
    std::vector<Portal> portals_;
};

} // namespace mir2::game::map

#endif // LEGEND2_CLIENT_GAME_MAP_PORTAL_MANAGER_H
