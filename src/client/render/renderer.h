// =============================================================================
// Legend2 渲染器 (Renderer)
// 
// 功能说明:
//   - 基于SDL2的渲染系统
//   - 提供纹理管理和缓存
//   - 支持精灵、矩形、线条等基本图形绘制
//   - 实现摄像机视口管理
//
// 主要组件:
//   - Camera: 摄像机,管理视口和坐标转换
//   - Texture: SDL纹理的RAII封装
//   - IRenderer: 渲染器接口
//   - SDLRenderer: SDL2渲染器实现
// =============================================================================

#ifndef LEGEND2_RENDERER_H
#define LEGEND2_RENDERER_H

#include <SDL.h>
#include <string>
#include <memory>
#include <unordered_map>
#include <optional>
#include <list>
#include <functional>
#include <cstddef>
#include "common/types.h"
#include "client/resource/resource_loader.h"
#include "core/sdl_resource.h"
#include "render/camera.h"
#include "render/i_renderer.h"
#include "render/i_texture_cache.h"
#include "render/texture.h"

namespace mir2::render {

// 引入公共类型定义
using namespace mir2::common;
using mir2::client::Sprite;

// Camera 类已移至 render/camera.h
// Texture 类已移至 render/texture.h

// =============================================================================
// SDL2 纹理缓存实现 (SDL2 Texture Cache)
// =============================================================================

constexpr size_t kDefaultTextureCacheBytes = 256 * 1024 * 1024;  // 256MB

/// SDL2纹理缓存（LRU）
class SDLTextureCache : public ITextureCache {
public:
    using TextureFactory = std::function<std::shared_ptr<Texture>(const Sprite& sprite, bool flip_vertical)>;

    explicit SDLTextureCache(size_t max_bytes = kDefaultTextureCacheBytes);
    SDLTextureCache(TextureFactory factory, size_t max_bytes = kDefaultTextureCacheBytes);

    void set_factory(TextureFactory factory);

    std::shared_ptr<Texture> get_or_create(const std::string& key,
                                           const Sprite& sprite,
                                           bool flip_v) override;
    std::shared_ptr<Texture> get(const std::string& key, bool flip_v) const override;
    void evict(const std::string& key) override;
    void clear() override;
    size_t size() const override;

    size_t max_bytes() const { return max_bytes_; }
    size_t current_bytes() const { return current_bytes_; }

private:
    struct Entry {
        std::shared_ptr<Texture> texture;
        size_t bytes = 0;
        std::list<std::string>::iterator iter;
    };

    std::string make_oriented_key(const std::string& key, bool flip_v) const;
    void touch(const std::string& key, Entry& entry) const;
    void enforce_limits();
    void evict_key(const std::string& key);

    TextureFactory factory_;
    size_t max_bytes_ = kDefaultTextureCacheBytes;
    size_t current_bytes_ = 0;
    mutable std::unordered_map<std::string, Entry> cache_;
    mutable std::list<std::string> lru_;  // front = most recent
};

// =============================================================================
// SDL2 渲染器实现 (SDL2 Renderer Implementation)
// =============================================================================

/// 基于SDL2的渲染器实现
/// 提供完整的2D渲染功能
class SDLRenderer : public IRenderer {
public:
    SDLRenderer();
    ~SDLRenderer() override;
    
    // IRenderer 接口实现
    bool initialize(int width, int height, const std::string& title) override;
    void shutdown() override;
    bool is_initialized() const override { return window_ != nullptr && renderer_ != nullptr; }
    void begin_frame() override;
    void end_frame() override;
    void clear(const Color& color = Color::black()) override;
    Size get_window_size() const override;
    void set_logical_size(int width, int height) override;
    
    // --- 纹理管理 (Texture Management) ---
    
    /// 从精灵数据创建纹理
    /// @param sprite 精灵数据
    /// @param flip_vertical 是否进行垂直翻转（WIL为自底向上，需要翻转）
    /// @return 纹理指针
    std::shared_ptr<Texture> create_texture_from_sprite(const Sprite& sprite, bool flip_vertical) override;
    
    /// 从RGBA像素数据创建纹理
    /// @param pixels 像素数据
    /// @param width 宽度
    /// @param height 高度
    /// @return 纹理指针
    std::shared_ptr<Texture> create_texture(const uint32_t* pixels, int width, int height);
    
    /// 获取或创建缓存的精灵纹理
    /// @param cache_key 缓存键
    /// @param sprite 精灵数据
    /// @param flip_vertical 是否进行垂直翻转（WIL为自底向上，需要翻转）
    /// @return 纹理指针
    std::shared_ptr<Texture> get_sprite_texture(const std::string& cache_key, const Sprite& sprite, bool flip_vertical);
    
    /// 清除纹理缓存
    void clear_texture_cache();
    
    /// 从缓存中移除纹理
    void remove_from_cache(const std::string& cache_key);

    /// 获取纹理缓存接口
    ITextureCache& get_texture_cache() { return texture_cache_; }
    const ITextureCache& get_texture_cache() const { return texture_cache_; }
    
    // --- 绘制函数 (Drawing Functions) ---
    
    /// 在屏幕位置绘制纹理
    void draw_texture(const Texture& texture, int x, int y) override;
    
    /// 使用源矩形和目标矩形绘制纹理
    void draw_texture(const Texture& texture, const Rect& src, const Rect& dst) override;
    
    /// 绘制带偏移的精灵纹理
    void draw_sprite(const Texture& texture, int x, int y, int offset_x, int offset_y) override;
    
    /// 绘制填充矩形
    void draw_rect(const Rect& rect, const Color& color) override;
    
    /// 绘制矩形边框
    void draw_rect_outline(const Rect& rect, const Color& color) override;
    
    /// 绘制线条
    void draw_line(int x1, int y1, int x2, int y2, const Color& color) override;
    
    /// 绘制点
    void draw_point(int x, int y, const Color& color) override;
    
    /// 设置绘制颜色
    void set_draw_color(const Color& color) override;
    
    /// 设置混合模式
    void set_blend_mode(SDL_BlendMode mode) override;
    
    // --- 访问SDL对象 (Access to SDL objects) ---
    
    SDL_Window* get_window() const { return window_.get(); }
    SDL_Renderer* get_renderer() const { return renderer_.get(); }
    
    // --- 帧计时 (Frame timing) ---
    
    uint32_t get_frame_count() const { return frame_count_; }  // 获取帧计数
    float get_delta_time() const { return delta_time_; }       // 获取帧间隔时间
    float get_fps() const { return fps_; }                     // 获取当前FPS
    
private:
    core::SDLWindowPtr window_;         // SDL窗口
    core::SDLRendererPtr renderer_;     // SDL渲染器
    
    // 纹理缓存(LRU)
    SDLTextureCache texture_cache_;
    
    // 帧计时
    uint32_t frame_count_ = 0;      // 总帧数
    uint32_t last_frame_time_ = 0;  // 上一帧时间
    float delta_time_ = 0.0f;       // 帧间隔时间(秒)
    float fps_ = 0.0f;              // 当前FPS
    
    // FPS计算
    uint32_t fps_frame_count_ = 0;  // FPS计算用帧计数
    uint32_t fps_last_time_ = 0;    // FPS计算用时间戳
};

} // namespace mir2::render

#endif // LEGEND2_RENDERER_H
