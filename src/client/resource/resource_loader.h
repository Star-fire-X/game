/**
 * @file resource_loader.h
 * @brief Legend2 客户端资源加载器
 * 
 * 本文件包含WIL/WIX文件解析和资源管理功能，包括：
 * - WIL/WIX图像档案解析
 * - 地图文件解析
 * - LRU缓存
 * - 资源管理器
 */

#ifndef LEGEND2_RESOURCE_LOADER_H
#define LEGEND2_RESOURCE_LOADER_H

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <unordered_map>
#include <fstream>
#include <mutex>
#include <list>
#include <nlohmann/json.hpp>

#include "client/resource/i_resource_provider.h"

namespace mir2::client {

// =============================================================================
// WIX索引文件结构 (WIX Index File Structures)
// =============================================================================

/// WIX文件头（WIL档案的索引文件）
/// Pascal string[40] = 1字节长度 + 40字节数据 = 41字节
constexpr size_t WIX_TITLE_SIZE = 41;  ///< Pascal短字符串大小

/**
 * @brief WIX文件头结构
 */
struct WixHeader {
    char title[WIX_TITLE_SIZE];  ///< Pascal string[40]: "WEMADE Entertainment inc."
    int32_t index_count;         ///< 档案中的图像数量
    int32_t ver_flag;            ///< 版本标志（0或65536为新版本）
    
    /// 检查头部是否有效
    bool is_valid() const {
        // 检查WEMADE签名（跳过第一个字节，那是长度）
        return (title[1] == 'W' && title[2] == 'E' && title[3] == 'M' && 
                title[4] == 'A' && title[5] == 'D' && title[6] == 'E');
    }
};

// =============================================================================
// WIL图像文件结构 (WIL Image File Structures)
// =============================================================================

/// WIL文件头（图像档案）
/// Pascal string[40] = 1字节长度 + 40字节数据 = 41字节
constexpr size_t WIL_TITLE_SIZE = 41;  ///< Pascal短字符串大小

/**
 * @brief WIL文件头结构
 */
struct WilHeader {
    char title[WIL_TITLE_SIZE];  ///< Pascal string[40]: "WEMADE Entertainment inc."
    int32_t image_count;         ///< 图像数量
    int32_t color_count;         ///< 颜色数：256=8位，65536=16位，16777216=24位
    int32_t palette_size;        ///< 调色板数据大小
    int32_t ver_flag;            ///< 版本标志（0或65536为新版本）
    
    /// 检查头部是否有效
    bool is_valid() const {
        return (title[1] == 'W' && title[2] == 'E' && title[3] == 'M' && 
                title[4] == 'A' && title[5] == 'D' && title[6] == 'E');
    }
    
    /// 获取位深度
    int get_bit_depth() const {
        switch (color_count) {
            case 256: return 8;
            case 65536: return 16;
            case 16777216: return 24;
            default: return 32;
        }
    }
    
    /// 是否为新版本格式
    bool is_new_version() const {
        return (ver_flag == 0) || (color_count == 65536);
    }
};

/**
 * @brief WIL文件中的单个图像头
 */
struct WilImageHeader {
    int16_t width;          ///< 图像宽度（像素）
    int16_t height;         ///< 图像高度（像素）
    int16_t offset_x;       ///< 精灵偏移X（用于定位）
    int16_t offset_y;       ///< 精灵偏移Y（用于定位）
};

// =============================================================================
// 精灵数据 (Sprite Data)
// =============================================================================

/**
 * @brief 从WIL档案提取的精灵数据
 */
struct Sprite {
    int width = 0;           ///< 宽度
    int height = 0;          ///< 高度
    int offset_x = 0;        ///< 精灵锚点偏移X
    int offset_y = 0;        ///< 精灵锚点偏移Y
    std::vector<uint32_t> pixels;  ///< RGBA格式像素数据
    
    /// 检查精灵是否有效
    bool is_valid() const {
        return width > 0 && height > 0 && 
               pixels.size() == static_cast<size_t>(width * height);
    }
    
    bool operator==(const Sprite& other) const {
        return width == other.width && height == other.height &&
               offset_x == other.offset_x && offset_y == other.offset_y &&
               pixels == other.pixels;
    }
    
    /// Serialize sprite to JSON
    std::string serialize() const;
    
    /// Deserialize sprite from JSON
    static std::optional<Sprite> deserialize(const std::string& data);
};

// JSON serialization for Sprite
void to_json(nlohmann::json& j, const Sprite& s);
void from_json(const nlohmann::json& j, Sprite& s);

// =============================================================================
// WIL/WIX Archive Parser
// =============================================================================

/// Parser for WIL/WIX format image archives
class WilArchive {
public:
    WilArchive() = default;
    ~WilArchive() = default;
    
    // Non-copyable but movable
    WilArchive(const WilArchive&) = delete;
    WilArchive& operator=(const WilArchive&) = delete;
    WilArchive(WilArchive&&) = default;
    WilArchive& operator=(WilArchive&&) = default;
    
    /// Load a WIL/WIX archive pair
    /// @param wil_path Path to the .wil file
    /// @return true if loaded successfully
    bool load(const std::string& wil_path);
    
    /// Check if archive is loaded
    bool is_loaded() const { return loaded_; }
    
    /// Get the number of images in the archive
    int get_image_count() const { return image_count_; }
    
    /// Get a sprite by index
    /// @param index Image index (0-based)
    /// @return Sprite data if successful
    std::optional<Sprite> get_sprite(int index);
    
    /// Get the archive name (filename without extension)
    const std::string& get_name() const { return name_; }
    
    /// Get the WIL header
    const WilHeader& get_wil_header() const { return wil_header_; }
    
    /// Get the palette (256 colors, RGBA format)
    const std::vector<uint32_t>& get_palette() const { return palette_; }
    
private:
    bool loaded_ = false;
    bool is_ilib_ = false;
    std::string name_;
    std::string wil_path_;
    std::string wix_path_;
    
    WilHeader wil_header_{};
    int image_count_ = 0;
    int bit_depth_ = 8;  // Bit depth: 8, 16, 24, or 32
    size_t wil_file_size_ = 0;
    
    // Palette (256 colors in RGBA format)
    std::vector<uint32_t> palette_;
    
    // Image offsets from WIX file
    std::vector<int32_t> image_offsets_;
    std::vector<int32_t> image_sizes_;
    
    // File stream for reading image data on demand
    mutable std::ifstream wil_file_;
    
    /// Load WIX index file
    bool load_wix(const std::string& wix_path);
    
    /// Load WIL header and palette
    bool load_wil_header(const std::string& wil_path);

    /// Build per-image data sizes using index offsets
    void build_image_sizes();

    /// Calculate expected data size for an image
    int calculate_data_size(int width, int height) const;

    /// Get payload size (excluding header) for an image index
    int get_image_payload_size(int index, int header_bytes) const;
};


// =============================================================================
// 地图文件结构 (Map File Structures)
// =============================================================================

/**
 * @brief 地图瓦片数据
 */
struct MapTile {
    uint16_t background = 0;    ///< 背景层瓦片索引
    uint16_t middle = 0;        ///< 中间层瓦片索引
    uint16_t object = 0;        ///< 物体层瓦片索引
    uint8_t door_index = 0;     ///< 门索引（用于传送门/门）
    uint8_t door_offset = 0;    ///< 门偏移
    uint8_t anim_frame = 0;     ///< 动画帧数
    uint8_t anim_tick = 0;      ///< 动画速度
    uint8_t area = 0;           ///< 区域标识符
    uint8_t light = 0;          ///< 光照强度（0..4）
    
    /**
     * @brief 检查瓦片是否可行走
     * 
     * 在传奇2地图中：
     * - 背景/前景高位(0x8000)表示阻挡
     * - 门存在且未打开时不可通行
     */
    bool is_walkable() const {
        if ((background & 0x8000) != 0) {
            return false;
        }
        if ((object & 0x8000) != 0) {
            return false;
        }
        const bool has_door = (door_index & 0x80) != 0 && (door_index & 0x7F) != 0;
        if (has_door && (door_offset & 0x80) == 0) {
            return false;
        }
        return true;
    }
    
    /// 检查瓦片是否有传送门/门
    bool has_portal() const {
        return (door_index & 0x80) != 0 && (door_index & 0x7F) != 0;
    }
    
    bool operator==(const MapTile& other) const {
        return background == other.background && middle == other.middle &&
               object == other.object && door_index == other.door_index &&
               door_offset == other.door_offset && anim_frame == other.anim_frame &&
               anim_tick == other.anim_tick && area == other.area &&
               light == other.light;
    }
};

/**
 * @brief 从.map文件加载的地图数据
 */
struct MapData {
    int width = 0;              ///< 地图宽度（瓦片数）
    int height = 0;             ///< 地图高度（瓦片数）
    std::vector<MapTile> tiles; ///< 瓦片数据（行优先顺序）
    
    /// 获取指定位置的瓦片
    const MapTile* get_tile(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) {
            return nullptr;
        }
        return &tiles[y * width + x];
    }
    
    /// 获取指定位置的可修改瓦片
    MapTile* get_tile_mut(int x, int y) {
        if (x < 0 || x >= width || y < 0 || y >= height) {
            return nullptr;
        }
        return &tiles[y * width + x];
    }
    
    /// 检查位置是否可行走
    bool is_walkable(int x, int y) const {
        const MapTile* tile = get_tile(x, y);
        return tile && tile->is_walkable();
    }
    
    /// 构建可行走性矩阵
    std::vector<std::vector<bool>> build_walkability_matrix() const;
    
    /// 检查地图数据是否有效
    bool is_valid() const {
        return width > 0 && height > 0 && 
               tiles.size() == static_cast<size_t>(width * height);
    }
    
    bool operator==(const MapData& other) const {
        return width == other.width && height == other.height &&
               tiles == other.tiles;
    }
    
    /// Serialize map data to JSON
    std::string serialize() const;
    
    /// Deserialize map data from JSON
    static std::optional<MapData> deserialize(const std::string& data);
};

// JSON serialization for MapTile
void to_json(nlohmann::json& j, const MapTile& t);
void from_json(const nlohmann::json& j, MapTile& t);

// JSON serialization for MapData
void to_json(nlohmann::json& j, const MapData& m);
void from_json(const nlohmann::json& j, MapData& m);

// =============================================================================
// Map File Parser
// =============================================================================

/// Parser for Legend2 .map format files (supports multiple header sizes/tile strides)
class MapLoader {
public:
    MapLoader() = default;
    
    /// Load a map file
    /// @param map_path Path to the .map file
    /// @return MapData if successful
    std::optional<MapData> load(const std::string& map_path);
    
private:
    /// Read map header
    bool read_header(std::ifstream& file, int16_t& width, int16_t& height);
    
    /// Read tile data
    bool read_tiles(std::ifstream& file, MapData& map, int tile_stride);
};

// =============================================================================
// 资源缓存 (Resource Cache - LRU)
// =============================================================================

/**
 * @brief LRU缓存，支持O(1)的get/put操作
 * 
 * 使用双向链表维护访问顺序，使用哈希表实现快速查找。
 * @tparam Key 键类型
 * @tparam Value 值类型
 */
template<typename Key, typename Value>
class LRUCache {
public:
    explicit LRUCache(size_t capacity) : capacity_(capacity) {
        if (capacity_ == 0) {
            capacity_ = 1;  // 最小容量为1
        }
    }
    
    /**
     * @brief 从缓存获取值（将项移到最近使用位置）
     * @param key 要查找的键
     * @return 值（如果找到），否则返回std::nullopt
     */
    std::optional<Value> get(const Key& key) {
        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) {
            return std::nullopt;
        }
        // 移到最前面（最近使用）- O(1)操作
        access_order_.splice(access_order_.begin(), access_order_, it->second.list_iter);
        return it->second.value;
    }
    
    /**
     * @brief 将值放入缓存（如果达到容量则淘汰LRU项）
     * @param key 要存储的键
     * @param value 要存储的值
     */
    void put(const Key& key, const Value& value) {
        auto it = cache_map_.find(key);
        if (it != cache_map_.end()) {
            // 更新现有项 - O(1)
            it->second.value = value;
            access_order_.splice(access_order_.begin(), access_order_, it->second.list_iter);
        } else {
            // 插入新项
            if (cache_map_.size() >= capacity_) {
                // 淘汰最少使用的（链表尾部）- O(1)
                const Key& lru_key = access_order_.back();
                cache_map_.erase(lru_key);
                access_order_.pop_back();
            }
            // 添加到链表头部 - O(1)
            access_order_.push_front(key);
            cache_map_[key] = CacheEntry{value, access_order_.begin()};
        }
    }
    
    /// 检查键是否存在于缓存中（不影响LRU顺序）
    bool contains(const Key& key) const {
        return cache_map_.find(key) != cache_map_.end();
    }
    
    /// 从缓存中移除指定键
    bool remove(const Key& key) {
        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) {
            return false;
        }
        access_order_.erase(it->second.list_iter);
        cache_map_.erase(it);
        return true;
    }
    
    /// 清空缓存
    void clear() {
        cache_map_.clear();
        access_order_.clear();
    }
    
    /// 获取当前缓存大小
    size_t size() const { return cache_map_.size(); }
    
    /// 获取缓存容量
    size_t capacity() const { return capacity_; }
    
    /// 检查缓存是否为空
    bool empty() const { return cache_map_.empty(); }
    
    /// 检查缓存是否已满
    bool full() const { return cache_map_.size() >= capacity_; }
    
    /// 设置新容量（如果新容量更小可能会淘汰项）
    void set_capacity(size_t new_capacity) {
        if (new_capacity == 0) {
            new_capacity = 1;
        }
        capacity_ = new_capacity;
        while (cache_map_.size() > capacity_) {
            const Key& lru_key = access_order_.back();
            cache_map_.erase(lru_key);
            access_order_.pop_back();
        }
    }
    
    /// 缓存统计信息（用于调试/监控）
    struct CacheStats {
        size_t current_size;   ///< 当前大小
        size_t capacity;       ///< 容量
        double fill_ratio;     ///< 填充率
    };
    
    CacheStats get_stats() const {
        return CacheStats{
            cache_map_.size(),
            capacity_,
            capacity_ > 0 ? static_cast<double>(cache_map_.size()) / capacity_ : 0.0
        };
    }
    
private:
    struct CacheEntry {
        Value value;
        typename std::list<Key>::iterator list_iter;
    };
    
    size_t capacity_;
    std::unordered_map<Key, CacheEntry> cache_map_;
    std::list<Key> access_order_;  ///< 头部=最近使用，尾部=最少使用
};

// =============================================================================
// Resource Manager
// =============================================================================

/// Manages loading and caching of game resources
class ResourceManager : public IResourceProvider {
public:
    explicit ResourceManager(size_t sprite_cache_size = 1000,
                            size_t map_cache_size = 10);
    ~ResourceManager() = default;
    
    // Non-copyable
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    
    /// Load a WIL archive
    /// @param wil_path Path to the .wil file
    /// @return true if loaded successfully
    bool load_wil_archive(const std::string& wil_path);

    /// Load an archive (IResourceProvider)
    bool load_archive(const std::string& path) override;
    
    /// Get a sprite from a loaded archive (uses cache)
    /// @param archive_name Archive name (e.g., "Hum", "Mon1")
    /// @param index Sprite index
    /// @return Sprite data if found
    std::optional<Sprite> get_sprite(const std::string& archive_name, int index) override;
    
    /// Load a map file (uses cache)
    /// @param map_path Path to the .map file
    /// @return MapData if successful
    std::optional<MapData> load_map(const std::string& map_path) override;
    
    /// Clear all caches (sprite and map)
    void clear_cache();
    
    /// Clear only sprite cache
    void clear_sprite_cache() { sprite_cache_.clear(); }
    
    /// Clear only map cache
    void clear_map_cache() { map_cache_.clear(); }
    
    /// Remove a specific sprite from cache
    /// @param archive_name Archive name
    /// @param index Sprite index
    /// @return true if sprite was in cache and removed
    bool remove_sprite_from_cache(const std::string& archive_name, int index);
    
    /// Remove a specific map from cache
    /// @param map_path Path to the map file
    /// @return true if map was in cache and removed
    bool remove_map_from_cache(const std::string& map_path);
    
    /// Check if a sprite is in cache
    /// @param archive_name Archive name
    /// @param index Sprite index
    /// @return true if sprite is cached
    bool is_sprite_cached(const std::string& archive_name, int index) const;
    
    /// Check if a map is in cache
    /// @param map_path Path to the map file
    /// @return true if map is cached
    bool is_map_cached(const std::string& map_path) const;
    
    /// Get sprite cache size
    size_t get_sprite_cache_size() const { return sprite_cache_.size(); }
    
    /// Get sprite cache capacity
    size_t get_sprite_cache_capacity() const { return sprite_cache_.capacity(); }
    
    /// Get map cache size
    size_t get_map_cache_size() const { return map_cache_.size(); }
    
    /// Get map cache capacity
    size_t get_map_cache_capacity() const { return map_cache_.capacity(); }
    
    /// Set sprite cache capacity (may evict items if reducing)
    void set_sprite_cache_capacity(size_t capacity) { sprite_cache_.set_capacity(capacity); }
    
    /// Set map cache capacity (may evict items if reducing)
    void set_map_cache_capacity(size_t capacity) { map_cache_.set_capacity(capacity); }
    
    /// Check if an archive is loaded
    bool is_archive_loaded(const std::string& archive_name) const override;
    
    /// Get total cache memory usage estimate (in bytes)
    size_t get_estimated_cache_memory() const;
    
private:
    // Loaded WIL archives
    std::unordered_map<std::string, std::unique_ptr<WilArchive>> archives_;
    
    // Sprite cache (key: "archive_name:index")
    mutable LRUCache<std::string, Sprite> sprite_cache_;
    
    // Map cache (key: map file path)
    mutable LRUCache<std::string, MapData> map_cache_;
    
    // Map loader
    MapLoader map_loader_;
    
    /// Generate cache key for sprite
    static std::string make_sprite_key(const std::string& archive, int index);

    mutable std::mutex mutex_;
};

} // namespace mir2::client

#endif // LEGEND2_RESOURCE_LOADER_H
