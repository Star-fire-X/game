/**
 * @file portal_manager.cpp
 * @brief PortalManager 类的实现
 */

#include "portal_manager.h"
#include <algorithm>

namespace mir2::game::map {

bool PortalManager::register_portal(const Portal& portal) {
    // 验证传送门配置
    if (!portal.is_valid()) {
        return false;
    }

    // 检查是否已存在
    for (auto& existing : portals_) {
        if (existing.source_pos == portal.source_pos) {
            // 更新现有传送门
            existing = portal;
            return true;
        }
    }

    // 添加新传送门
    portals_.push_back(portal);
    return true;
}

bool PortalManager::unregister_portal(const Position& pos) {
    auto it = std::find_if(portals_.begin(), portals_.end(),
                          [&pos](const Portal& p) { return p.source_pos == pos; });

    if (it == portals_.end()) {
        return false;
    }

    portals_.erase(it);
    return true;
}

std::optional<Portal> PortalManager::get_portal_at(const Position& pos) const {
    for (const auto& portal : portals_) {
        if (portal.source_pos == pos) {
            return portal;
        }
    }
    return std::nullopt;
}

bool PortalManager::has_portal(const Position& pos) const {
    return get_portal_at(pos).has_value();
}

void PortalManager::clear() {
    portals_.clear();
}

} // namespace mir2::game::map
