// =============================================================================
// Legend2 纹理缓存接口 (Texture Cache Interface)
// =============================================================================

#ifndef LEGEND2_CLIENT_RENDER_I_TEXTURE_CACHE_H
#define LEGEND2_CLIENT_RENDER_I_TEXTURE_CACHE_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace mir2::client {
struct Sprite;
} // namespace mir2::client

namespace mir2::render {

class Texture;

/// 纹理缓存键
/// 使用整数键避免字符串拼接与哈希开销
struct TextureKey {
    uint16_t archive_id = 0;   // 档案 ID (1-65535)
    uint32_t frame_index = 0;  // 帧索引
    bool flip_v = false;       // 垂直翻转标志

    bool operator==(const TextureKey& other) const noexcept {
        return archive_id == other.archive_id &&
               frame_index == other.frame_index &&
               flip_v == other.flip_v;
    }
};

/// TextureKey 哈希函数
struct TextureKeyHash {
    size_t operator()(const TextureKey& key) const noexcept {
        const uint64_t combined = (static_cast<uint64_t>(key.archive_id) << 33) |
                                  (static_cast<uint64_t>(key.frame_index) << 1) |
                                  (key.flip_v ? 1ULL : 0ULL);
        return std::hash<uint64_t>{}(combined);
    }
};

/// 纹理缓存接口
/// 用于解耦纹理缓存策略与渲染实现
class ITextureCache {
public:
    virtual ~ITextureCache() = default;

    /// 获取或创建纹理 (整数 key)
    virtual std::shared_ptr<Texture> get_or_create(const TextureKey& key,
                                                   const mir2::client::Sprite& sprite) = 0;

    /// 获取已缓存纹理（不存在返回nullptr，整数 key）
    virtual std::shared_ptr<Texture> get(const TextureKey& key) const = 0;

    /// 从缓存移除指定键（整数 key）
    virtual void evict(const TextureKey& key) = 0;

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
