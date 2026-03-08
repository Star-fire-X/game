// =============================================================================
// Legend2 地图渲染器 (Map Renderer)
// 
// 功能说明:
//   - 基于瓦片的地图渲染
//   - 支持多层渲染(背景、中层、物件)
//   - 支持动画瓦片
//   - 提供调试可视化: 网格、可行走区域
//
// 主要组件:
//   - MapLayer: 地图渲染层枚举
//   - TileAnimation: 瓦片动画状态
//   - MapRenderer: 地图渲染器
// =============================================================================

#ifndef LEGEND2_MAP_RENDERER_H
#define LEGEND2_MAP_RENDERER_H

#include "render/renderer.h"
#include "render/i_texture_cache.h"
#include "common/types.h"
#include "client/resource/resource_loader.h"
#include "client/resource/i_resource_provider.h"
#include "client/game/map/map_system.h"
#include <atomic>
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace mir2::client {
class AsyncLoader;
} // namespace mir2::client

namespace mir2::game::map {

using mir2::common::Position;
using mir2::client::IResourceProvider;

// 跨模块类型引用
using mir2::render::SDLRenderer;
using mir2::render::Texture;
using mir2::render::Camera;
using mir2::render::ITextureCache;
using mir2::render::TextureKey;
using mir2::render::TextureKeyHash;
using mir2::render::ArchiveRegistry;

// =============================================================================
// 地图渲染层 (Map Render Layer)
// =============================================================================

/// 地图渲染层枚举
/// 定义地图的三层渲染层
enum class MapLayer {
    BACKGROUND = 0,     // 地面层(Tiles.wil)
    MIDDLE = 1,         // 中间层(SmTiles.wil)
    OBJECTS = 2,        // 对象层(Objects*.wil)
    COUNT = 3           // 层数量
};

// =============================================================================
// 瓦片动画状态 (Tile Animation State)
// =============================================================================

/// 动画瓦片的状态
/// 用于管理动画瓦片的帧切换
struct TileAnimation {
    int current_frame = 0;        // 当前帧
    int total_frames = 1;         // 总帧数
    float frame_time = 0.0f;      // 当前帧已播放时间
    float frame_duration = 0.2f;  // 每帧持续时间(秒)
    
    /// 更新动画
    /// @param delta_time 帧间隔时间
    void update(float delta_time) {
        if (total_frames <= 1) return;
        frame_time += delta_time;
        while (frame_time >= frame_duration) {
            frame_time -= frame_duration;
            current_frame = (current_frame + 1) % total_frames;
        }
    }
    
    /// 重置动画
    void reset() {
        current_frame = 0;
        frame_time = 0.0f;
    }
};

// =============================================================================
// 地图渲染器 (Map Renderer)
// =============================================================================

/// 地图渲染器
/// 使用分层瓦片渲染游戏地图
class MapRenderer {
public:
    /// 构造函数
    /// @param renderer SDL渲染器引用
    /// @param resource_provider 资源提供者引用(用于加载精灵)
    /// @param texture_cache 纹理缓存引用
    /// @param async_loader 异步加载器（可为nullptr）
    MapRenderer(SDLRenderer& renderer,
                IResourceProvider& resource_provider,
                ITextureCache& texture_cache,
                mir2::client::AsyncLoader* async_loader = nullptr);
    
    ~MapRenderer();
    
    // 禁止拷贝
    MapRenderer(const MapRenderer&) = delete;
    MapRenderer& operator=(const MapRenderer&) = delete;
    
    /// 设置要渲染的地图数据
    /// @param map_data 地图数据指针(必须保持有效)
    void set_map(const MapData* map_data);
    void SetMap(const MapData* map_data) { set_map(map_data); }
    
    /// 获取当前地图数据
    const MapData* get_map() const noexcept { return map_data_; }
    const MapData* GetMap() const noexcept { return get_map(); }
    
    /// 更新动画
    /// @param delta_time 帧间隔时间
    void update(float delta_time);
    void Update(float delta_time) { update(delta_time); }
    
    /// 渲染地图
    /// @param camera 用于视口裁剪的摄像机
    void render(const Camera& camera);
    void Render(const Camera& camera) { render(camera); }

    /// 预取可见瓦片资源
    void prefetch_visible_tiles(const Camera& camera);
    void PrefetchVisibleTiles(const Camera& camera) { prefetch_visible_tiles(camera); }
    
    /// 渲染指定层
    /// @param layer 要渲染的层
    /// @param camera 用于视口裁剪的摄像机
    void render_layer(MapLayer layer, const Camera& camera);
    void RenderLayer(MapLayer layer, const Camera& camera) { render_layer(layer, camera); }
    
    /// 设置指定层是否可见
    /// @param layer 要设置的层
    /// @param enabled 是否可见
    void set_layer_visible(MapLayer layer, bool enabled);
    void SetLayerVisible(MapLayer layer, bool enabled) {
        set_layer_visible(layer, enabled);
    }
    
    /// 检查指定层是否可见
    bool is_layer_visible(MapLayer layer) const;
    bool IsLayerVisible(MapLayer layer) const { return is_layer_visible(layer); }
    
    /// 设置瓦片资源文件名
    void set_tiles_archive(const std::string& name);
    void SetTilesArchive(const std::string& name) { set_tiles_archive(name); }
    void set_smtiles_archive(const std::string& name);
    void SetSmTilesArchive(const std::string& name) { set_smtiles_archive(name); }
    void set_objects_archive(const std::string& name);
    void SetObjectsArchive(const std::string& name) { set_objects_archive(name); }
    
    /// 加载所需的瓦片资源文件
    /// @return 所有资源加载成功返回true
    bool load_archives();
    bool LoadArchives() { return load_archives(); }
    
    /// 清除缓存的纹理
    void clear_cache();
    void ClearCache() { clear_cache(); }
    
    /// 渲染统计信息
    struct RenderStats {
        int tiles_rendered = 0;   // 已渲染的瓦片数
        int tiles_culled = 0;     // 剔除的瓦片数
        int cache_hits = 0;       // 缓存命中次数
        int cache_misses = 0;     // 缓存未命中次数
    };
    
    const RenderStats& get_stats() const noexcept { return stats_; }
    const RenderStats& GetStats() const noexcept { return get_stats(); }
    void reset_stats() { stats_ = RenderStats{}; }
    void ResetStats() { reset_stats(); }
    
    /// 设置调试渲染选项
    void set_debug_grid(bool enabled) { debug_grid_ = enabled; }
    void SetDebugGrid(bool enabled) { set_debug_grid(enabled); }
    void set_debug_walkability(bool enabled) { debug_walkability_ = enabled; }
    void SetDebugWalkability(bool enabled) { set_debug_walkability(enabled); }
    bool is_debug_grid_enabled() const noexcept { return debug_grid_; }
    bool IsDebugGridEnabled() const noexcept { return is_debug_grid_enabled(); }
    bool is_debug_walkability_enabled() const noexcept { return debug_walkability_; }
    bool IsDebugWalkabilityEnabled() const noexcept {
        return is_debug_walkability_enabled();
    }
    
private:
    SDLRenderer& renderer_;              // SDL渲染器引用
    IResourceProvider& resource_provider_;  // 资源提供者引用
    ITextureCache& texture_cache_;          // 纹理缓存引用
    mir2::client::AsyncLoader* async_loader_ = nullptr;
    const MapData* map_data_ = nullptr;  // 当前地图数据
    
    // 资源文件名
    std::string tiles_archive_ = "Tiles";       // 地面层资源
    std::string smtiles_archive_ = "SmTiles";   // 中间层资源
    std::string objects_archive_ = "Objects";   // 对象层资源
    
    // 层可见性
    bool layer_visible_[static_cast<int>(MapLayer::COUNT)] = {true, true, true};
    
    // 动画瓦片状态
    TileAnimation tile_animation_;
    std::atomic<int> main_ani_count_{0};  // 对齐原版MainAniCount
    std::string data_path_;       // Data目录路径缓存
    
    // 渲染统计
    RenderStats stats_;
    
    // 调试选项
    bool debug_grid_ = false;        // 是否显示网格
    bool debug_walkability_ = false; // 是否显示可行走区域

    // 预取状态
    std::unordered_set<TextureKey, TextureKeyHash> pending_prefetch_;
    struct TileOffset {
        int offset_x = 0;
        int offset_y = 0;
    };
    std::unordered_map<TextureKey, TileOffset, TextureKeyHash> tile_offsets_;

    // 档案ID缓存（避免频繁字符串哈希）
    static constexpr int kMaxObjectArchives = 4;  // Objects, Objects2, Objects3, Objects4
    uint16_t tiles_archive_id_ = 0;
    uint16_t smtiles_archive_id_ = 0;
    uint16_t objects_archive_id_ = 0;
    std::array<uint16_t, kMaxObjectArchives> object_archive_ids_{};
    std::array<std::string, kMaxObjectArchives> object_archive_names_{};

    // 保护异步回调的生命周期，避免 MapRenderer 销毁后回调访问悬垂指针。
    struct CallbackGuard {
        std::atomic<bool> alive{true};
    };
    std::shared_ptr<CallbackGuard> callback_guard_;
    
    /// 获取瓦片纹理
    /// @param archive_name 资源文件名
    /// @param tile_index 瓦片索引
    /// @return 纹理指针
    std::shared_ptr<Texture> get_tile_texture(const std::string& archive_name, const TextureKey& key);
    
    /// 渲染单个瓦片
    /// @param tile_index 瓦片索引
    /// @param screen_x 屏幕X坐标
    /// @param screen_y 屏幕Y坐标
    /// @param archive_name 资源文件名
    /// @param layer 当前层
    /// @param blend 是否使用混合绘制
    void render_tile(const TextureKey& key, int screen_x, int screen_y,
                     const std::string& archive_name, MapLayer layer, bool blend);
    
    /// 渲染调试网格
    void render_debug_grid(const Camera& camera);
    
    /// 渲染可行走区域覆盖层
    void render_walkability_overlay(const Camera& camera);
    
    /// 计算瓦片的屏幕位置
    Position tile_to_screen(int tile_x, int tile_y, const Camera& camera) const;
    
    /// 检查瓦片是否在视口内可见
    bool is_tile_visible(int tile_x, int tile_y, const Camera& camera) const;

    uint16_t get_archive_id(const std::string& archive_name, uint16_t& cache_slot);
    const std::string& get_objects_archive_name(int archive_id);
    uint16_t get_objects_archive_id(int archive_id);
    void reset_object_archive_cache();
    bool ensure_archive_loaded(const std::string& archive_name);
};

} // namespace mir2::game::map

#endif // LEGEND2_MAP_RENDERER_H
