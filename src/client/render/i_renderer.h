// =============================================================================
// Legend2 渲染器接口 (Renderer Interface)
// =============================================================================

#ifndef LEGEND2_CLIENT_RENDER_I_RENDERER_H
#define LEGEND2_CLIENT_RENDER_I_RENDERER_H

#include <SDL.h>
#include <memory>
#include <string>

#include "common/types.h"

namespace mir2::client {
struct Sprite;
} // namespace mir2::client

namespace mir2::render {

class Texture;

// 引入公共类型定义
using namespace mir2::common;

/// 渲染器接口
/// 定义渲染系统的基本功能
class IRenderer {
public:
    virtual ~IRenderer() = default;

    /// 初始化渲染器
    /// @param width 窗口宽度
    /// @param height 窗口高度
    /// @param title 窗口标题
    /// @return 初始化成功返回true
    virtual bool initialize(int width, int height, const std::string& title) = 0;

    /// 关闭渲染器
    virtual void shutdown() = 0;

    /// 检查渲染器是否已初始化
    virtual bool is_initialized() const = 0;

    /// 开始新的一帧
    virtual void begin_frame() = 0;

    /// 结束当前帧并呈现
    virtual void end_frame() = 0;

    /// 用指定颜色清除屏幕
    virtual void clear(const Color& color = Color::black()) = 0;

    /// 获取窗口尺寸
    virtual Size get_window_size() const = 0;

    /// 设置逻辑渲染尺寸(用于分辨率缩放)
    virtual void set_logical_size(int width, int height) = 0;

    /// 从精灵数据创建纹理
    virtual std::shared_ptr<Texture> create_texture_from_sprite(const mir2::client::Sprite& sprite, bool flip_vertical) = 0;

    /// 在屏幕位置绘制纹理
    virtual void draw_texture(const Texture& texture, int x, int y) = 0;

    /// 使用源矩形和目标矩形绘制纹理
    virtual void draw_texture(const Texture& texture, const Rect& src, const Rect& dst) = 0;

    /// 绘制带偏移的精灵纹理
    virtual void draw_sprite(const Texture& texture, int x, int y, int offset_x, int offset_y) = 0;

    /// 绘制填充矩形
    virtual void draw_rect(const Rect& rect, const Color& color) = 0;

    /// 绘制矩形边框
    virtual void draw_rect_outline(const Rect& rect, const Color& color) = 0;

    /// 绘制线条
    virtual void draw_line(int x1, int y1, int x2, int y2, const Color& color) = 0;

    /// 绘制点
    virtual void draw_point(int x, int y, const Color& color) = 0;

    /// 设置绘制颜色
    virtual void set_draw_color(const Color& color) = 0;

    /// 设置混合模式
    virtual void set_blend_mode(SDL_BlendMode mode) = 0;
};

} // namespace mir2::render

#endif // LEGEND2_CLIENT_RENDER_I_RENDERER_H
