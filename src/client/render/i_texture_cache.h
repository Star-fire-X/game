// =============================================================================
// Legend2 纹理缓存接口 (Texture Cache Interface)
// =============================================================================

#ifndef LEGEND2_CLIENT_RENDER_I_TEXTURE_CACHE_H
#define LEGEND2_CLIENT_RENDER_I_TEXTURE_CACHE_H

#include <cstddef>
#include <memory>
#include <string>

namespace mir2::client {
struct Sprite;
} // namespace mir2::client

namespace mir2::render {

class Texture;

/// 纹理缓存接口
/// 用于解耦纹理缓存策略与渲染实现
class ITextureCache {
public:
    virtual ~ITextureCache() = default;

    /// 获取或创建纹理
    virtual std::shared_ptr<Texture> get_or_create(const std::string& key,
                                                   const mir2::client::Sprite& sprite,
                                                   bool flip_v) = 0;

    /// 获取已缓存纹理（不存在返回nullptr）
    virtual std::shared_ptr<Texture> get(const std::string& key, bool flip_v) const = 0;

    /// 从缓存移除指定键
    virtual void evict(const std::string& key) = 0;

    /// 清空缓存
    virtual void clear() = 0;

    /// 当前缓存数量
    virtual size_t size() const = 0;
};

} // namespace mir2::render

#endif // LEGEND2_CLIENT_RENDER_I_TEXTURE_CACHE_H
