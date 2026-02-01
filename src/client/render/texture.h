// =============================================================================
// Legend2 纹理封装 (Texture)
//
// 功能说明:
//   - SDL_Texture 的 RAII 封装
//   - 自动内存管理
//   - 移动语义支持
// =============================================================================

#ifndef LEGEND2_RENDER_TEXTURE_H
#define LEGEND2_RENDER_TEXTURE_H

#include <SDL.h>
#include "core/sdl_resource.h"

namespace mir2::render {

/// SDL纹理的RAII封装
/// 自动管理SDL_Texture生命周期，仅支持移动语义
class Texture {
public:
    Texture() = default;
    Texture(SDL_Texture* tex, int w, int h) : texture_(tex), width_(w), height_(h) {}
    ~Texture() = default;

    // 仅支持移动语义
    Texture(Texture&& other) noexcept = default;
    Texture& operator=(Texture&& other) noexcept = default;

    // 禁止拷贝
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    SDL_Texture* get() const { return texture_.get(); }  // 获取原始SDL纹理指针
    int width() const { return width_; }           // 获取纹理宽度
    int height() const { return height_; }         // 获取纹理高度
    bool valid() const { return texture_ != nullptr; }  // 检查纹理是否有效

private:
    core::SDLTexturePtr texture_;  // SDL纹理指针
    int width_ = 0;                   // 纹理宽度
    int height_ = 0;                  // 纹理高度
};

} // namespace mir2::render

#endif // LEGEND2_RENDER_TEXTURE_H
