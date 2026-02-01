// =============================================================================
// Legend2 摄像机 (Camera)
//
// 功能说明:
//   - 2D游戏视口管理
//   - 世界坐标与屏幕坐标转换
//   - 可见区域计算
//   - 平滑跟随和缩放
// =============================================================================

#ifndef LEGEND2_RENDER_CAMERA_H
#define LEGEND2_RENDER_CAMERA_H

#include "common/types.h"
#include <cmath>

namespace mir2::render {

// 引入公共类型定义
using namespace mir2::common;

/// 2D摄像机/视口
/// 管理游戏世界的可视区域和坐标转换
struct Camera {
    float x = 0.0f;         // 摄像机X位置(世界坐标)
    float y = 0.0f;         // 摄像机Y位置(世界坐标)
    int viewport_width = 800;   // 视口宽度(像素)
    int viewport_height = 600;  // 视口高度(像素)
    float zoom = 1.0f;      // 缩放级别(1.0=正常)

    /// 将世界坐标转换为屏幕坐标
    /// @param world_pos 世界坐标位置
    /// @return 屏幕坐标位置
    Position world_to_screen(const Position& world_pos) const {
        const float world_x = (static_cast<float>(world_pos.x) + 0.5f) *
                              static_cast<float>(constants::TILE_WIDTH);
        const float world_y = (static_cast<float>(world_pos.y) + 0.5f) *
                              static_cast<float>(constants::TILE_HEIGHT);
        const float screen_x = (world_x - x) * zoom + static_cast<float>(viewport_width) * 0.5f;
        const float screen_y = (world_y - y) * zoom + static_cast<float>(viewport_height) * 0.5f;
        return {
            static_cast<int>(std::lround(screen_x)),
            static_cast<int>(std::lround(screen_y))
        };
    }

    /// 将屏幕坐标转换为世界坐标
    /// @param screen_pos 屏幕坐标位置
    /// @return 世界坐标位置(瓦片坐标)
    Position screen_to_world(const Position& screen_pos) const {
        const float world_x = (static_cast<float>(screen_pos.x) - static_cast<float>(viewport_width) * 0.5f) /
                                  zoom +
                              x;
        const float world_y = (static_cast<float>(screen_pos.y) - static_cast<float>(viewport_height) * 0.5f) /
                                  zoom +
                              y;
        const float tile_x = world_x / static_cast<float>(constants::TILE_WIDTH) - 0.5f;
        const float tile_y = world_y / static_cast<float>(constants::TILE_HEIGHT) - 0.5f;
        return {
            static_cast<int>(std::lround(tile_x)),
            static_cast<int>(std::lround(tile_y))
        };
    }

    /// 获取可见的瓦片范围
    /// @return 可见瓦片的边界矩形
    Rect get_visible_tile_bounds() const {
        int half_width = static_cast<int>(viewport_width / (2 * zoom * constants::TILE_WIDTH)) + 2;
        int half_height = static_cast<int>(viewport_height / (2 * zoom * constants::TILE_HEIGHT)) + 2;
        int center_x = static_cast<int>(x / constants::TILE_WIDTH);
        int center_y = static_cast<int>(y / constants::TILE_HEIGHT);
        return {
            center_x - half_width,
            center_y - half_height,
            half_width * 2 + 1,
            half_height * 2 + 1
        };
    }

    /// 将摄像机中心对准指定世界位置
    /// @param world_pos 要对准的世界坐标
    void center_on(const Position& world_pos) {
        x = static_cast<float>(world_pos.x * constants::TILE_WIDTH + constants::TILE_WIDTH / 2);
        y = static_cast<float>(world_pos.y * constants::TILE_HEIGHT + constants::TILE_HEIGHT / 2);
    }

    /// 平滑移动摄像机到目标位置
    /// @param target_x 目标X坐标
    /// @param target_y 目标Y坐标
    /// @param speed 移动速度(插值系数)
    void move_towards(float target_x, float target_y, float speed) {
        x += (target_x - x) * speed;
        y += (target_y - y) * speed;
    }
};

} // namespace mir2::render

#endif // LEGEND2_RENDER_CAMERA_H
