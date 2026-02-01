// =============================================================================
// Legend2 渲染器实现 (Renderer Implementation)
// 
// 功能说明:
//   - 基于SDL2的渲染系统实现
//   - 管理窗口创建和渲染上下文
//   - 提供纹理创建、缓存和绘制功能
//   - SDL_ttf字体库初始化与管理
// =============================================================================

#include "render/renderer.h"
#include <iostream>
#include <algorithm>
#include <vector>

#ifdef HAS_SDL2_TTF
#include <SDL_ttf.h>
#endif

namespace mir2::render {

// =============================================================================
// SDLTextureCache 实现
// =============================================================================

SDLTextureCache::SDLTextureCache(size_t max_bytes)
    : max_bytes_(max_bytes) {}

SDLTextureCache::SDLTextureCache(TextureFactory factory, size_t max_bytes)
    : factory_(std::move(factory))
    , max_bytes_(max_bytes) {}

void SDLTextureCache::set_factory(TextureFactory factory) {
    factory_ = std::move(factory);
}

std::string SDLTextureCache::make_oriented_key(const std::string& key, bool flip_v) const {
    return key + (flip_v ? ":vflip" : ":noflip");
}

void SDLTextureCache::touch(const std::string& key, Entry& entry) const {
    lru_.splice(lru_.begin(), lru_, entry.iter);
    entry.iter = lru_.begin();
}

void SDLTextureCache::evict_key(const std::string& key) {
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return;
    }
    current_bytes_ = (current_bytes_ > it->second.bytes) ? (current_bytes_ - it->second.bytes) : 0;
    lru_.erase(it->second.iter);
    cache_.erase(it);
}

void SDLTextureCache::enforce_limits() {
    if (max_bytes_ == 0) {
        return;
    }
    while (!lru_.empty() && current_bytes_ > max_bytes_) {
        const std::string& lru_key = lru_.back();
        evict_key(lru_key);
    }
}

std::shared_ptr<Texture> SDLTextureCache::get(const std::string& key, bool flip_v) const {
    const std::string oriented_key = make_oriented_key(key, flip_v);
    auto it = cache_.find(oriented_key);
    if (it == cache_.end()) {
        return nullptr;
    }
    touch(oriented_key, it->second);
    return it->second.texture;
}

std::shared_ptr<Texture> SDLTextureCache::get_or_create(const std::string& key,
                                                        const Sprite& sprite,
                                                        bool flip_v) {
    const std::string oriented_key = make_oriented_key(key, flip_v);
    auto it = cache_.find(oriented_key);
    if (it != cache_.end()) {
        touch(oriented_key, it->second);
        return it->second.texture;
    }

    if (!factory_) {
        return nullptr;
    }

    auto texture = factory_(sprite, flip_v);
    if (!texture) {
        return nullptr;
    }

    Entry entry;
    entry.texture = texture;
    entry.bytes = static_cast<size_t>(texture->width()) * static_cast<size_t>(texture->height()) * sizeof(uint32_t);
    lru_.push_front(oriented_key);
    entry.iter = lru_.begin();
    cache_[oriented_key] = entry;
    current_bytes_ += entry.bytes;
    enforce_limits();
    return texture;
}

void SDLTextureCache::evict(const std::string& key) {
    evict_key(make_oriented_key(key, true));
    evict_key(make_oriented_key(key, false));
}

void SDLTextureCache::clear() {
    cache_.clear();
    lru_.clear();
    current_bytes_ = 0;
}

size_t SDLTextureCache::size() const {
    return cache_.size();
}

// =============================================================================
// SDLRenderer 实现 - SDL2渲染器
// =============================================================================

SDLRenderer::SDLRenderer()
    : texture_cache_(kDefaultTextureCacheBytes) {
    texture_cache_.set_factory([this](const Sprite& sprite, bool flip_vertical) {
        return create_texture_from_sprite(sprite, flip_vertical);
    });
}

SDLRenderer::~SDLRenderer() {
    shutdown();  // 释放资源并清理
}

/// 初始化渲染器
/// 创建SDL窗口和渲染上下文
/// @param width 窗口宽度
/// @param height 窗口高度
/// @param title 窗口标题
/// @return 初始化成功返回true
bool SDLRenderer::initialize(int width, int height, const std::string& title) {
    // 检查是否已初始化
    if (window_ != nullptr) {
        std::cerr << "Renderer already initialized" << std::endl;
        return false;
    }
    
    // 如果尚未初始化SDL视频子系统则初始化
    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            std::cerr << "SDL video initialization failed: " << SDL_GetError() << std::endl;
            return false;
        }
    }
    
    // 创建窗口
    window_.reset(SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    ));
    
    if (!window_) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }
    
    SDL_SetWindowSize(window_.get(), width, height);
    int actual_width = 0;
    int actual_height = 0;
    SDL_GetWindowSize(window_.get(), &actual_width, &actual_height);
    std::cout << "  Window size: " << actual_width << "x" << actual_height << std::endl;
    
    // 创建渲染器(启用硬件加速和垂直同步)
    renderer_.reset(SDL_CreateRenderer(
        window_.get(),
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    ));
    
    if (!renderer_) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        window_.reset();
        return false;
    }
    
    // 设置默认混合模式
    SDL_SetRenderDrawBlendMode(renderer_.get(), SDL_BLENDMODE_BLEND);
    
#ifdef HAS_SDL2_TTF
    // 初始化SDL_ttf字体库
    if (TTF_WasInit() == 0) {
        if (TTF_Init() < 0) {
            std::cerr << "SDL_ttf initialization failed: " << TTF_GetError() << std::endl;
            // TTF失败不应该阻止渲染器初始化，但字体渲染会降级
        } else {
            std::cout << "SDL_ttf initialized successfully" << std::endl;
        }
    }
#endif
    
    // 初始化计时
    last_frame_time_ = SDL_GetTicks();
    fps_last_time_ = last_frame_time_;
    
    return true;
}

/// 关闭渲染器
/// 释放所有资源
void SDLRenderer::shutdown() {
    // 首先清除纹理缓存
    texture_cache_.clear();
    
    renderer_.reset();
    window_.reset();
    
#ifdef HAS_SDL2_TTF
    // 关闭SDL_ttf字体库
    if (TTF_WasInit()) {
        TTF_Quit();
    }
#endif
}

/// 开始新的一帧
/// 计算帧间隔时间和FPS
void SDLRenderer::begin_frame() {
    // 计算帧间隔时间
    uint32_t current_time = SDL_GetTicks();
    delta_time_ = (current_time - last_frame_time_) / 1000.0f;
    last_frame_time_ = current_time;
    
    // 计算FPS(每秒更新一次)
    fps_frame_count_++;
    if (current_time - fps_last_time_ >= 1000) {
        fps_ = static_cast<float>(fps_frame_count_) * 1000.0f / (current_time - fps_last_time_);
        fps_frame_count_ = 0;
        fps_last_time_ = current_time;
    }
    
    frame_count_++;
}

/// 结束当前帧
/// 将渲染内容呈现到屏幕
void SDLRenderer::end_frame() {
    SDL_RenderPresent(renderer_.get());
}

/// 用指定颜色清除屏幕
void SDLRenderer::clear(const Color& color) {
    SDL_SetRenderDrawColor(renderer_.get(), color.r, color.g, color.b, color.a);
    SDL_RenderClear(renderer_.get());
}

/// 获取窗口尺寸
Size SDLRenderer::get_window_size() const {
    int w = 0, h = 0;
    if (window_) {
        SDL_GetWindowSize(window_.get(), &w, &h);
    }
    return {w, h};
}

/// 设置逻辑渲染尺寸
void SDLRenderer::set_logical_size(int width, int height) {
    if (renderer_) {
        SDL_RenderSetLogicalSize(renderer_.get(), width, height);
    }
}

// =============================================================================
// 纹理管理 (Texture Management)
// =============================================================================

/// 从精灵数据创建纹理
std::shared_ptr<Texture> SDLRenderer::create_texture_from_sprite(const Sprite& sprite, bool flip_vertical) {
    if (!sprite.is_valid() || !renderer_) {
        return nullptr;
    }

    if (!flip_vertical) {
        return create_texture(sprite.pixels.data(), sprite.width, sprite.height);
    }

    // WIL 像素数据为自底向上的行顺序，这里翻转为自顶向下
    std::vector<uint32_t> flipped(sprite.pixels.size());
    for (int y = 0; y < sprite.height; ++y) {
        int src_y = sprite.height - 1 - y;
        auto src_it = sprite.pixels.begin() + src_y * sprite.width;
        auto dst_it = flipped.begin() + y * sprite.width;
        std::copy(src_it, src_it + sprite.width, dst_it);
    }

    return create_texture(flipped.data(), sprite.width, sprite.height);
}

/// 从RGBA像素数据创建纹理
std::shared_ptr<Texture> SDLRenderer::create_texture(const uint32_t* pixels, int width, int height) {
    if (!pixels || width <= 0 || height <= 0 || !renderer_) {
        return nullptr;
    }
    
    // 从像素数据创建SDL表面
    // 像素格式为RGBA(0xRRGGBBAA或0xAABBGGRR,取决于字节序)
    core::SDLSurfacePtr surface(SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<uint32_t*>(pixels),
        width,
        height,
        32,
        width * 4,
        SDL_PIXELFORMAT_RGBA32
    ));
    
    if (!surface) {
        std::cerr << "Failed to create surface: " << SDL_GetError() << std::endl;
        return nullptr;
    }
    
    // 从表面创建纹理
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_.get(), surface.get());
    
    if (!texture) {
        std::cerr << "Failed to create texture: " << SDL_GetError() << std::endl;
        return nullptr;
    }
    
    // 为纹理启用Alpha混合
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    
    // 设置纹理缩放模式为线性插值，使图片缩放时更平滑清晰
    // SDL_ScaleModeBest 会选择最佳质量的缩放算法(通常是线性插值)
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
#endif
    
    return std::make_shared<Texture>(texture, width, height);
}

/// 获取或创建缓存的精灵纹理
std::shared_ptr<Texture> SDLRenderer::get_sprite_texture(const std::string& cache_key, const Sprite& sprite, bool flip_vertical) {
    return texture_cache_.get_or_create(cache_key, sprite, flip_vertical);
}

/// 清除纹理缓存
void SDLRenderer::clear_texture_cache() {
    texture_cache_.clear();
}

/// 从缓存中移除纹理
void SDLRenderer::remove_from_cache(const std::string& cache_key) {
    texture_cache_.evict(cache_key);
}

// =============================================================================
// 绘制函数 (Drawing Functions)
// =============================================================================

/// 在屏幕位置绘制纹理
void SDLRenderer::draw_texture(const Texture& texture, int x, int y) {
    if (!texture.valid() || !renderer_) return;
    
    SDL_Rect dst = {x, y, texture.width(), texture.height()};
    SDL_RenderCopy(renderer_.get(), texture.get(), nullptr, &dst);
}

/// 使用源矩形和目标矩形绘制纹理
void SDLRenderer::draw_texture(const Texture& texture, const Rect& src, const Rect& dst) {
    if (!texture.valid() || !renderer_) return;
    
    SDL_Rect src_rect = {src.x, src.y, src.width, src.height};
    SDL_Rect dst_rect = {dst.x, dst.y, dst.width, dst.height};
    SDL_RenderCopy(renderer_.get(), texture.get(), &src_rect, &dst_rect);
}

/// 绘制带偏移的精灵纹理
/// 精灵通常以底部中心或其他点为锚点
void SDLRenderer::draw_sprite(const Texture& texture, int x, int y, int offset_x, int offset_y) {
    if (!texture.valid() || !renderer_) return;
    
    // 应用精灵偏移
    SDL_Rect dst = {
        x - offset_x,
        y - offset_y,
        texture.width(),
        texture.height()
    };
    SDL_RenderCopy(renderer_.get(), texture.get(), nullptr, &dst);
}

/// 绘制填充矩形
void SDLRenderer::draw_rect(const Rect& rect, const Color& color) {
    if (!renderer_) return;
    
    SDL_SetRenderDrawColor(renderer_.get(), color.r, color.g, color.b, color.a);
    SDL_Rect sdl_rect = {rect.x, rect.y, rect.width, rect.height};
    SDL_RenderFillRect(renderer_.get(), &sdl_rect);
}

/// 绘制矩形边框
void SDLRenderer::draw_rect_outline(const Rect& rect, const Color& color) {
    if (!renderer_) return;
    
    SDL_SetRenderDrawColor(renderer_.get(), color.r, color.g, color.b, color.a);
    SDL_Rect sdl_rect = {rect.x, rect.y, rect.width, rect.height};
    SDL_RenderDrawRect(renderer_.get(), &sdl_rect);
}

/// 绘制线条
void SDLRenderer::draw_line(int x1, int y1, int x2, int y2, const Color& color) {
    if (!renderer_) return;
    
    SDL_SetRenderDrawColor(renderer_.get(), color.r, color.g, color.b, color.a);
    SDL_RenderDrawLine(renderer_.get(), x1, y1, x2, y2);
}

/// 绘制点
void SDLRenderer::draw_point(int x, int y, const Color& color) {
    if (!renderer_) return;
    
    SDL_SetRenderDrawColor(renderer_.get(), color.r, color.g, color.b, color.a);
    SDL_RenderDrawPoint(renderer_.get(), x, y);
}

/// 设置绘制颜色
void SDLRenderer::set_draw_color(const Color& color) {
    if (renderer_) {
        SDL_SetRenderDrawColor(renderer_.get(), color.r, color.g, color.b, color.a);
    }
}

/// 设置混合模式
void SDLRenderer::set_blend_mode(SDL_BlendMode mode) {
    if (renderer_) {
        SDL_SetRenderDrawBlendMode(renderer_.get(), mode);
    }
}

} // namespace mir2::render
