// =============================================================================
// Legend2 可行走性接口 (Walkability Provider Interface)
// =============================================================================

#ifndef LEGEND2_CLIENT_GAME_MAP_I_WALKABILITY_PROVIDER_H
#define LEGEND2_CLIENT_GAME_MAP_I_WALKABILITY_PROVIDER_H

#include "common/types.h"

#include <vector>

namespace mir2::game::map {

/// 可行走性接口
/// 用于客户端点击移动前的可达性验证
///
/// 约定:
/// - 实现必须保证只返回合法地图范围内的位置。
/// - `get_neighbors` 返回可直接行走的相邻格（通常为8方向）。
/// - 若允许对角线移动，应避免“穿墙拐角”，即当相邻直线格不可走时，
///   不应返回该对角线位置。
/// - `get_map_size` 在未加载地图时应返回 {0, 0} 或报告错误（由实现决定）。
class IWalkabilityProvider {
public:
    virtual ~IWalkabilityProvider() = default;

    /// 检查坐标是否可行走
    virtual bool is_walkable(int x, int y) const = 0;

    /// 检查坐标是否合法
    virtual bool is_valid_position(int x, int y) const = 0;

    /// 获取地图尺寸
    virtual mir2::common::Size get_map_size() const = 0;

    /// 获取可行走的相邻位置
    virtual std::vector<mir2::common::Position> get_neighbors(
        const mir2::common::Position& pos) const = 0;
};

} // namespace mir2::game::map

#endif // LEGEND2_CLIENT_GAME_MAP_I_WALKABILITY_PROVIDER_H
