/**
 * @file map_renderer.cpp
 * @brief Legend2 地图渲染器实现
 * 
 * 该文件实现了游戏的地图渲染系统，包括：
 * - 分层渲染：背景层(Tiles)、中间层(SmTiles)、物件层(Objects)
 * - 视口裁剪：只渲染摄像机可见范围内的地图块
 * - 纹理缓存：避免重复加载相同的地图块纹理
 * - 动画支持：支持物件层的帧动画
 * - 调试功能：网格显示、可行走区域显示
 * 
 * 地图数据来源于map文件，图片资源来源于WIL档案
 * - Tiles.wil: 大地砖（48x32像素）
 * - SmTiles.wil: 小地砖（24x16像素）
 * - Objects.wil: 地图物件（树木、建筑等）
 */

#include "game/map/map_renderer.h"
#include "client/resource/async_loader.h"
#include "core/path_utils.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <vector>

namespace mir2::game::map {

using mir2::client::MapTile;
using mir2::client::Sprite;
using mir2::common::Color;
using mir2::common::Rect;
namespace constants = mir2::common::constants;

namespace MapRenderConstants {
// 物件层高耸精灵的最大高度（格子数）。
// 来源：原版 Mir2 客户端 GameData 约定值。
// 用途：扩大物件层可见范围，避免高塔/树顶被裁剪。
// 选择理由：与原版一致，减少视口上缘闪烁。
constexpr int kLongHeightImage = 35;

// 预取边界填充（格子数）。
// 来源：原版客户端渲染经验值。
// 用途：预取时向四周扩展范围，降低视口边缘抖动导致的频繁加载。
// 选择理由：2 格兼顾预取收益与内存/IO 开销。
constexpr int kPrefetchPaddingTiles = 2;

// 左侧可见范围扩展（格子数）。
// 来源：原版客户端视口裁剪参数。
// 用途：向左额外渲染以避免移动时边缘空洞。
// 选择理由：与原版一致，保证左右滚动平滑。
constexpr int kVisibleMarginLeftTiles = 2;

// 右侧可见范围扩展（格子数）。
// 来源：原版客户端视口裁剪参数。
// 用途：向右额外渲染以覆盖视口边缘。
// 选择理由：1 格可覆盖常规移动的边缘变化。
constexpr int kVisibleMarginRightTiles = 1;

// 物件层右侧可见范围扩展（格子数）。
// 来源：原版客户端视口裁剪参数（物件层更宽）。
// 用途：物件层向右多渲染一格，减少大物件边缘裁剪。
// 选择理由：物件层需要更宽的安全区。
constexpr int kVisibleMarginRightTilesObjects = 2;

// 上方可见范围扩展（格子数）。
// 来源：原版客户端视口裁剪参数。
// 用途：向上额外渲染以覆盖移动边缘。
// 选择理由：1 格可避免边缘闪烁。
constexpr int kVisibleMarginTopTiles = 1;

// 物件层上方可见范围扩展（格子数）。
// 来源：原版客户端视口裁剪参数。
// 用途：物件层顶部通常不额外扩展。
// 选择理由：高耸精灵的上延由下方扩展与 kLongHeightImage 保障。
constexpr int kVisibleMarginTopTilesObjects = 0;

// 下方可见范围扩展（格子数）。
// 来源：原版客户端视口裁剪参数。
// 用途：向下额外渲染以覆盖移动边缘。
// 选择理由：1 格可避免底部闪烁。
constexpr int kVisibleMarginBottomTiles = 1;

// 屏幕可见性判断的像素级边距。
// 来源：原版客户端渲染经验值。
// 用途：覆盖超出瓦片边界的大型物件可见性判断。
// 选择理由：100 像素能涵盖常见大物件的超出范围。
constexpr int kTileVisibilityMarginPixels = 100;

// 背景层大地砖的格子步进。
// 来源：原版地图格式，Tiles.wil 的大地砖覆盖 2x2 格。
// 用途：只在偶数坐标渲染背景层，避免重复绘制。
// 选择理由：数据格式规定只有偶数坐标有有效索引。
constexpr int kBackgroundTileStride = 2;

// WIL 索引基准（1 基）。
// 来源：WIL/WIX 资源格式索引从 1 开始。
// 用途：将地图索引转换为 0 基数组索引。
// 选择理由：与资源文件索引协议一致。
constexpr int kWilIndexBase = 1;

// 动画帧混合标志位掩码（bit7）。
// 来源：原版 MapTile.anim_frame 位布局。
// 用途：判断是否启用混合绘制。
// 选择理由：格式规定 bit7 表示混合标志。
constexpr uint8_t kAnimBlendFlagMask = 0x80;

// 动画帧数掩码（bit0-6）。
// 来源：原版 MapTile.anim_frame 位布局。
// 用途：提取动画帧数量。
// 选择理由：格式规定低 7 位存储帧数。
constexpr uint8_t kAnimFrameCountMask = 0x7F;

// 门索引数值掩码（bit0-6）。
// 来源：原版 MapTile.door_index 位布局。
// 用途：提取门索引值。
// 选择理由：格式规定低 7 位存储索引。
constexpr uint8_t kDoorIndexValueMask = 0x7F;

// 门偏移有效标志位掩码（bit7）。
// 来源：原版 MapTile.door_offset 位布局。
// 用途：判断门偏移是否生效（开门状态）。
// 选择理由：格式规定 bit7 表示偏移有效。
constexpr uint8_t kDoorOffsetFlagMask = 0x80;

// 门偏移数值掩码（bit0-6）。
// 来源：原版 MapTile.door_offset 位布局。
// 用途：提取门偏移帧/位移量。
// 选择理由：格式规定低 7 位存储偏移值。
constexpr uint8_t kDoorOffsetValueMask = 0x7F;

// 地图瓦片索引值掩码（bit0-14）。
// 来源：原版地图格式，bit15 为阻挡标志。
// 用途：去除阻挡标志，得到真实瓦片索引。
// 选择理由：格式规定高位仅用于标志位。
constexpr uint16_t kMapTileIndexValueMask = 0x7FFF;

// 物件层档案编号位移（高 2 位）。
// 来源：原版地图格式，object 索引高 2 位为 Objects 档案编号。
// 用途：拆分物件档案编号与实际索引。
// 选择理由：格式规定高位用于档案选择。
constexpr int kObjectArchiveIdShift = 13;

// 物件层索引掩码（低 13 位）。
// 来源：原版地图格式，object 索引低 13 位为 1 基瓦片索引。
// 用途：提取物件瓦片索引。
// 选择理由：格式规定低 13 位存储索引。
constexpr uint16_t kObjectTileIndexMask = 0x1FFF;

// 物件档案编号到文件名后缀的偏移量。
// 来源：原版资源命名约定（Objects, Objects2, Objects3...）。
// 用途：将档案编号转换为文件名后缀。
// 选择理由：档案编号 1 对应 Objects2，因此需要 +1 偏移。
constexpr int kObjectsArchiveSuffixOffset = 1;

// 纹理未就绪时的占位块颜色（深灰）。
// 来源：调试视觉约定（非资源文件）。
// 用途：异步加载期间避免空白，提示加载状态。
// 选择理由：低亮度色减少视觉干扰。
constexpr Color kPlaceholderTileColor = {60, 60, 60, 255};

// 调试网格颜色（半透明灰）。
// 来源：调试视觉约定（非资源文件）。
// 用途：显示地图网格便于对齐检查。
// 选择理由：灰色低干扰且与背景区分明显。
constexpr Color kDebugGridColor = {100, 100, 100, 128};

// 可行走区域覆盖颜色（半透明绿）。
// 来源：调试视觉约定（非资源文件）。
// 用途：标记可行走瓦片。
// 选择理由：绿色与“可通行”含义一致。
constexpr Color kWalkableOverlayColor = {0, 255, 0, 64};

// 不可行走区域覆盖颜色（半透明红）。
// 来源：调试视觉约定（非资源文件）。
// 用途：标记阻挡瓦片。
// 选择理由：红色与“危险/阻挡”含义一致。
constexpr Color kBlockedOverlayColor = {255, 0, 0, 64};
}  // namespace MapRenderConstants

namespace {
struct AnimInfo {
    int frame_offset = 0;
    bool blend = false;
};

AnimInfo compute_anim_info(uint8_t anim_frame, uint8_t anim_tick, int main_ani_count) {
    AnimInfo info;
    info.blend = (anim_frame & MapRenderConstants::kAnimBlendFlagMask) != 0;
    const int frames = anim_frame & MapRenderConstants::kAnimFrameCountMask;
    if (frames <= 0) {
        return info;
    }

    const int tick = static_cast<int>(anim_tick);
    const int cycle = frames * (1 + tick);
    info.frame_offset = (main_ani_count % cycle) / (1 + tick);
    return info;
}

struct TileRenderData {
    int tile_index = 0;
    bool blend = false;
    uint16_t object_value = 0;
};

TileRenderData ExtractTileRenderData(const MapTile& tile, MapLayer layer,
                                     int main_ani_count) {
    TileRenderData data;
    switch (layer) {
        case MapLayer::BACKGROUND:
            data.tile_index = tile.background;
            break;
        case MapLayer::MIDDLE:
            data.tile_index = tile.middle;
            break;
        case MapLayer::OBJECTS: {
            data.object_value = tile.object;
            const uint16_t object_index = static_cast<uint16_t>(
                data.object_value & MapRenderConstants::kMapTileIndexValueMask);
            data.tile_index =
                static_cast<int>(object_index & MapRenderConstants::kObjectTileIndexMask);

            const AnimInfo anim_info =
                compute_anim_info(tile.anim_frame, tile.anim_tick, main_ani_count);
            data.blend = anim_info.blend;
            data.tile_index += anim_info.frame_offset;

            if ((tile.door_offset & MapRenderConstants::kDoorOffsetFlagMask) != 0 &&
                (tile.door_index & MapRenderConstants::kDoorIndexValueMask) > 0) {
                data.tile_index +=
                    (tile.door_offset & MapRenderConstants::kDoorOffsetValueMask);
            }
            break;
        }
        default:
            break;
    }
    return data;
}

int ResolveObjectArchiveSlot(uint16_t object_value) {
    const uint16_t object_index = static_cast<uint16_t>(
        object_value & MapRenderConstants::kMapTileIndexValueMask);
    return static_cast<int>(object_index >> MapRenderConstants::kObjectArchiveIdShift);
}
} // namespace

// =============================================================================
// MapRenderer 地图渲染器实现
// =============================================================================

/**
 * @brief 构造函数
 * @param renderer SDL渲染器引用
 * @param resource_manager 资源管理器引用
 */
MapRenderer::MapRenderer(SDLRenderer& renderer,
                         IResourceProvider& resource_provider,
                         ITextureCache& texture_cache,
                         mir2::client::AsyncLoader* async_loader)
    : renderer_(renderer)
    , resource_provider_(resource_provider)
    , texture_cache_(texture_cache)
    , async_loader_(async_loader)
    , callback_guard_(std::make_shared<CallbackGuard>())
{
}

MapRenderer::~MapRenderer() {
    if (callback_guard_) {
        callback_guard_->alive.store(false, std::memory_order_release);
    }
}

void MapRenderer::set_tiles_archive(const std::string& name) {
    tiles_archive_ = name;
    tiles_archive_id_ = 0;
}

void MapRenderer::set_smtiles_archive(const std::string& name) {
    smtiles_archive_ = name;
    smtiles_archive_id_ = 0;
}

void MapRenderer::set_objects_archive(const std::string& name) {
    objects_archive_ = name;
    reset_object_archive_cache();
}

/**
 * @brief 设置要渲染的地图数据
 * @param map_data 地图数据指针
 * 
 * 切换地图时会重置动画状态
 */
void MapRenderer::set_map(const MapData* map_data) {
    map_data_ = map_data;
    tile_animation_.reset();  // 重置动画计时器
    main_ani_count_.store(0, std::memory_order_relaxed);
    pending_prefetch_.clear();
    tile_offsets_.clear();
}

/**
 * @brief 更新地图动画
 * @param delta_time 帧间隔时间（秒）
 * 
 * 更新地图物件的帧动画（如水面波动、火焰等）
 */
void MapRenderer::update(float delta_time) {
    tile_animation_.update(delta_time);
    int current = main_ani_count_.load(std::memory_order_relaxed);
    while (true) {
        const int next = (current == std::numeric_limits<int>::max()) ? 0 : (current + 1);
        if (main_ani_count_.compare_exchange_weak(
                current, next, std::memory_order_relaxed)) {
            break;
        }
    }
}

/**
 * @brief 渲染地图
 * @param camera 摄像机（决定渲染的视口范围）
 * 
 * 渲染顺序（从底层到顶层）：
 * 1. 背景层(BACKGROUND) - 大地砖
 * 2. 中间层(MIDDLE) - 小地砖
 * 3. 物件层(OBJECTS) - 树木、建筑等
 * 4. 调试覆盖层（如果启用）
 */
void MapRenderer::render(const Camera& camera) {
    if (!map_data_) return;
    
    // 重置渲染统计
    reset_stats();

    if (async_loader_) {
        prefetch_visible_tiles(camera);
    }

    renderer_.begin_batch();

    // 按顺序渲染各层
    if (layer_visible_[static_cast<int>(MapLayer::BACKGROUND)]) {
        render_layer(MapLayer::BACKGROUND, camera);
    }

    if (layer_visible_[static_cast<int>(MapLayer::MIDDLE)]) {
        render_layer(MapLayer::MIDDLE, camera);
    }

    if (layer_visible_[static_cast<int>(MapLayer::OBJECTS)]) {
        render_layer(MapLayer::OBJECTS, camera);
    }

    renderer_.end_batch();

    // 渲染调试覆盖层
    if (debug_grid_) {
        render_debug_grid(camera);  // 网格显示
    }
    
    if (debug_walkability_) {
        render_walkability_overlay(camera);  // 可行走区域
    }
}

void MapRenderer::prefetch_visible_tiles(const Camera& camera) {
    if (!async_loader_ || !map_data_) {
        return;
    }

    // Async completion runs later on the main thread; guard prevents UAF if renderer is destroyed.
    std::weak_ptr<CallbackGuard> weak_guard = callback_guard_;
    auto schedule_prefetch = [this, weak_guard](const std::string& archive_name,
                                                uint16_t archive_id,
                                                int tile_index) {
        if (tile_index < 0 || archive_id == 0) {
            return;
        }
        const TextureKey key{archive_id, static_cast<uint32_t>(tile_index), true};
        if (texture_cache_.get(key)) {
            return;
        }
        if (!pending_prefetch_.insert(key).second) {
            return;
        }
        async_loader_->request_sprite(archive_name, tile_index,
            [this, key, weak_guard](std::optional<Sprite> sprite_opt) {
                auto guard = weak_guard.lock();
                if (!guard || !guard->alive.load(std::memory_order_acquire)) {
                    return;
                }
                if (sprite_opt && sprite_opt->is_valid()) {
                    tile_offsets_[key] = {sprite_opt->offset_x, sprite_opt->offset_y};
                    texture_cache_.get_or_create(key, *sprite_opt);
                }
                pending_prefetch_.erase(key);
            });
    };

    Rect visible = camera.get_visible_tile_bounds();
    const int main_ani_count = main_ani_count_.load(std::memory_order_relaxed);
    const int padding = MapRenderConstants::kPrefetchPaddingTiles;

    for (int layer_index = 0; layer_index < static_cast<int>(MapLayer::COUNT); ++layer_index) {
        MapLayer layer = static_cast<MapLayer>(layer_index);
        if (!layer_visible_[layer_index]) {
            continue;
        }

        const int margin_left = MapRenderConstants::kVisibleMarginLeftTiles + padding;
        const int margin_right =
            ((layer == MapLayer::OBJECTS) ? MapRenderConstants::kVisibleMarginRightTilesObjects
                                          : MapRenderConstants::kVisibleMarginRightTiles) +
            padding;
        const int margin_top =
            ((layer == MapLayer::OBJECTS) ? MapRenderConstants::kVisibleMarginTopTilesObjects
                                          : MapRenderConstants::kVisibleMarginTopTiles) +
            padding;
        const int margin_bottom =
            ((layer == MapLayer::OBJECTS) ? MapRenderConstants::kLongHeightImage
                                          : MapRenderConstants::kVisibleMarginBottomTiles) +
            padding;

        int start_x = std::max(0, visible.x - margin_left);
        int start_y = std::max(0, visible.y - margin_top);
        int end_x = std::min(map_data_->width, visible.x + visible.width + margin_right);
        int end_y = std::min(map_data_->height, visible.y + visible.height + margin_bottom);

        const std::string* base_archive_name = nullptr;
        uint16_t base_archive_id = 0;
        switch (layer) {
            case MapLayer::BACKGROUND:
                base_archive_name = &tiles_archive_;
                base_archive_id = get_archive_id(tiles_archive_, tiles_archive_id_);
                break;
            case MapLayer::MIDDLE:
                base_archive_name = &smtiles_archive_;
                base_archive_id = get_archive_id(smtiles_archive_, smtiles_archive_id_);
                break;
            case MapLayer::OBJECTS:
                base_archive_name = &objects_archive_;
                base_archive_id = get_archive_id(objects_archive_, objects_archive_id_);
                break;
            default:
                continue;
        }

        if (!base_archive_name || base_archive_id == 0) {
            continue;
        }

        const std::string& base_objects_name = get_objects_archive_name(0);
        const uint16_t base_objects_id = get_objects_archive_id(0);

        for (int y = start_y; y < end_y; ++y) {
            for (int x = start_x; x < end_x; ++x) {
                const MapTile* tile = map_data_->get_tile(x, y);
                if (!tile) continue;

                const TileRenderData tile_data =
                    ExtractTileRenderData(*tile, layer, main_ani_count);
                int tile_index = tile_data.tile_index;
                const uint16_t object_value = tile_data.object_value;

                if (layer != MapLayer::OBJECTS) {
                    tile_index &= MapRenderConstants::kMapTileIndexValueMask;
                }

                if (tile_index == 0) {
                    continue;
                }

                if (layer == MapLayer::BACKGROUND) {
                    if ((x % MapRenderConstants::kBackgroundTileStride != 0) ||
                        (y % MapRenderConstants::kBackgroundTileStride != 0)) {
                        continue;
                    }
                }

                tile_index -= MapRenderConstants::kWilIndexBase;
                if (tile_index < 0) {
                    continue;
                }

                const std::string* archive_name = base_archive_name;
                uint16_t archive_id = base_archive_id;
                if (layer == MapLayer::OBJECTS) {
                    const int object_archive_slot = ResolveObjectArchiveSlot(object_value);
                    const std::string& object_archive_name = get_objects_archive_name(object_archive_slot);
                    archive_name = &object_archive_name;
                    archive_id = get_objects_archive_id(object_archive_slot);
                    if (!ensure_archive_loaded(*archive_name)) {
                        archive_name = &base_objects_name;
                        archive_id = base_objects_id;
                        if (!ensure_archive_loaded(*archive_name)) {
                            continue;
                        }
                    }
                }

                schedule_prefetch(*archive_name, archive_id, tile_index);
            }
        }
    }
}

/**
 * @brief 渲染指定的地图层
 * @param layer 要渲染的层
 * @param camera 摄像机
 * 
 * 只渲染摄像机可见范围内的地图块，实现视口裁剪优化
 */
void MapRenderer::render_layer(MapLayer layer, const Camera& camera) {
    if (!map_data_) return;

    // 获取可见的地图块范围
    Rect visible = camera.get_visible_tile_bounds();
    const int main_ani_count = main_ani_count_.load(std::memory_order_relaxed);

    const int margin_left = MapRenderConstants::kVisibleMarginLeftTiles;
    const int margin_right =
        (layer == MapLayer::OBJECTS) ? MapRenderConstants::kVisibleMarginRightTilesObjects
                                     : MapRenderConstants::kVisibleMarginRightTiles;
    const int margin_top =
        (layer == MapLayer::OBJECTS) ? MapRenderConstants::kVisibleMarginTopTilesObjects
                                     : MapRenderConstants::kVisibleMarginTopTiles;
    const int margin_bottom =
        (layer == MapLayer::OBJECTS) ? MapRenderConstants::kLongHeightImage
                                     : MapRenderConstants::kVisibleMarginBottomTiles;

    // 限制在地图边界内
    int start_x = std::max(0, visible.x - margin_left);
    int start_y = std::max(0, visible.y - margin_top);
    int end_x = std::min(map_data_->width, visible.x + visible.width + margin_right);
    int end_y = std::min(map_data_->height, visible.y + visible.height + margin_bottom);

    // 根据层类型选择对应的资源档案
    const std::string* base_archive_name = nullptr;
    uint16_t base_archive_id = 0;
    switch (layer) {
        case MapLayer::BACKGROUND:
            base_archive_name = &tiles_archive_;      // Tiles.wil - 大地砖
            base_archive_id = get_archive_id(tiles_archive_, tiles_archive_id_);
            break;
        case MapLayer::MIDDLE:
            base_archive_name = &smtiles_archive_;    // SmTiles.wil - 小地砖
            base_archive_id = get_archive_id(smtiles_archive_, smtiles_archive_id_);
            break;
        case MapLayer::OBJECTS:
            base_archive_name = &objects_archive_;    // Objects.wil - 物件
            base_archive_id = get_archive_id(objects_archive_, objects_archive_id_);
            break;
        default:
            return;
    }

    if (!base_archive_name || base_archive_id == 0) {
        return;
    }

    const std::string& base_objects_name = get_objects_archive_name(0);
    const uint16_t base_objects_id = get_objects_archive_id(0);

    // 遍历并渲染可见范围内的地图块
    for (int y = start_y; y < end_y; ++y) {
        for (int x = start_x; x < end_x; ++x) {
            const MapTile* tile = map_data_->get_tile(x, y);
            if (!tile) continue;

            const TileRenderData tile_data =
                ExtractTileRenderData(*tile, layer, main_ani_count);
            int tile_index = tile_data.tile_index;
            const bool blend = tile_data.blend;
            const uint16_t object_value = tile_data.object_value;

            if (layer != MapLayer::OBJECTS) {
                // 屏蔽高位阻挡标志，获得真实图块索引
                tile_index &= MapRenderConstants::kMapTileIndexValueMask;
            }

            // 跳过空图块（索引0表示无图块）
            if (tile_index == 0) {
                stats_.tiles_culled++;
                continue;
            }

            if (layer == MapLayer::BACKGROUND) {
                if ((x % MapRenderConstants::kBackgroundTileStride != 0) ||
                    (y % MapRenderConstants::kBackgroundTileStride != 0)) {
                    continue;
                }
            }

            // WIL索引以1为起点
            tile_index -= MapRenderConstants::kWilIndexBase;
            if (tile_index < 0) {
                stats_.tiles_culled++;
                continue;
            }

            const std::string* archive_name = base_archive_name;
            uint16_t archive_id = base_archive_id;
            if (layer == MapLayer::OBJECTS) {
                const int object_archive_slot = ResolveObjectArchiveSlot(object_value);
                const std::string& object_archive_name = get_objects_archive_name(object_archive_slot);
                archive_name = &object_archive_name;
                archive_id = get_objects_archive_id(object_archive_slot);
                if (!ensure_archive_loaded(*archive_name)) {
                    archive_name = &base_objects_name;
                    archive_id = base_objects_id;
                    if (!ensure_archive_loaded(*archive_name)) {
                        stats_.tiles_culled++;
                        continue;
                    }
                }
            }
            if (archive_id == 0) {
                stats_.tiles_culled++;
                continue;
            }

            // 计算屏幕坐标
            Position screen_pos = tile_to_screen(x, y, camera);

            // 渲染图块
            const TextureKey key{archive_id, static_cast<uint32_t>(tile_index), true};
            render_tile(key, screen_pos.x, screen_pos.y, *archive_name, layer, blend);
        }
    }
}

/**
 * @brief 设置图层可见性
 * @param layer 图层类型
 * @param enabled 是否可见
 */
void MapRenderer::set_layer_visible(MapLayer layer, bool enabled) {
    int index = static_cast<int>(layer);
    if (index >= 0 && index < static_cast<int>(MapLayer::COUNT)) {
        layer_visible_[index] = enabled;
    }
}

/**
 * @brief 获取图层可见性
 * @param layer 图层类型
 * @return 是否可见
 */
bool MapRenderer::is_layer_visible(MapLayer layer) const {
    int index = static_cast<int>(layer);
    if (index >= 0 && index < static_cast<int>(MapLayer::COUNT)) {
        return layer_visible_[index];
    }
    return false;
}

/**
 * @brief 加载地图渲染所需的资源档案
 * @return 加载成功返回true
 * 
 * 尝试从多个可能的路径加载WIL档案
 * - Data/
 * - ../Data/
 * - ../../Data/
 * - ./Data/
 * 
 * 需要加载的档案
 * - Tiles.wil: 大地砖图片
 * - SmTiles.wil: 小地砖图片
 * - Objects.wil: 物件图片
 */
bool MapRenderer::load_archives() {
    bool success = true;
    
    data_path_ = mir2::core::find_data_path(tiles_archive_ + ".wil");
    if (data_path_.empty()) {
        std::cerr << "Could not find Data directory! Tried: Data/, ../Data/, ../../Data/, ./Data/" << std::endl;
        data_path_.clear();
        return false;
    }

    std::cout << "Found data directory at: " << data_path_ << std::endl;
    
    // 加载大地砖档案(Tiles.wil)
    if (!resource_provider_.is_archive_loaded(tiles_archive_)) {
        if (!resource_provider_.load_archive(data_path_ + tiles_archive_ + ".wil")) {
            std::cerr << "Failed to load " << tiles_archive_ << " archive" << std::endl;
            success = false;
        } else {
            std::cout << "Loaded " << tiles_archive_ << " archive" << std::endl;
        }
    }
    
    // 加载小地砖档案(SmTiles.wil)
    if (!resource_provider_.is_archive_loaded(smtiles_archive_)) {
        if (!resource_provider_.load_archive(data_path_ + smtiles_archive_ + ".wil")) {
            std::cerr << "Failed to load " << smtiles_archive_ << " archive" << std::endl;
            success = false;
        } else {
            std::cout << "Loaded " << smtiles_archive_ << " archive" << std::endl;
        }
    }
    
    // 加载物件档案 (Objects.wil)
    // 注意：可能有多个物件档案 (Objects, Objects2, 等)
    if (!resource_provider_.is_archive_loaded(objects_archive_)) {
        if (!resource_provider_.load_archive(data_path_ + objects_archive_ + ".wil")) {
            std::cerr << "Failed to load " << objects_archive_ << " archive" << std::endl;
            success = false;
        } else {
            std::cout << "Loaded " << objects_archive_ << " archive" << std::endl;
        }
    }

    if (success) {
        tiles_archive_id_ = get_archive_id(tiles_archive_, tiles_archive_id_);
        smtiles_archive_id_ = get_archive_id(smtiles_archive_, smtiles_archive_id_);
        objects_archive_id_ = get_archive_id(objects_archive_, objects_archive_id_);
    }

    return success;
}

uint16_t MapRenderer::get_archive_id(const std::string& archive_name, uint16_t& cache_slot) {
    if (cache_slot != 0) {
        return cache_slot;
    }
    cache_slot = ArchiveRegistry::instance().get_or_register(archive_name);
    return cache_slot;
}

const std::string& MapRenderer::get_objects_archive_name(int archive_id) {
    if (archive_id < 0 || archive_id >= kMaxObjectArchives) {
        return objects_archive_;
    }

    std::string& cached = object_archive_names_[archive_id];
    if (!cached.empty()) {
        return cached;
    }

    if (archive_id == 0) {
        cached = objects_archive_;
    } else {
        cached = objects_archive_ +
                 std::to_string(archive_id + MapRenderConstants::kObjectsArchiveSuffixOffset);
    }
    return cached;
}

uint16_t MapRenderer::get_objects_archive_id(int archive_id) {
    if (archive_id <= 0 || archive_id >= kMaxObjectArchives) {
        return get_archive_id(objects_archive_, objects_archive_id_);
    }

    uint16_t& cached = object_archive_ids_[archive_id];
    if (cached != 0) {
        return cached;
    }

    const std::string& archive_name = get_objects_archive_name(archive_id);
    cached = ArchiveRegistry::instance().get_or_register(archive_name);
    return cached;
}

void MapRenderer::reset_object_archive_cache() {
    objects_archive_id_ = 0;
    object_archive_ids_.fill(0);
    for (auto& name : object_archive_names_) {
        name.clear();
    }
}

bool MapRenderer::ensure_archive_loaded(const std::string& archive_name) {
    if (resource_provider_.is_archive_loaded(archive_name)) {
        return true;
    }
    if (data_path_.empty()) {
        return false;
    }
    if (!resource_provider_.load_archive(data_path_ + archive_name + ".wil")) {
        std::cerr << "Failed to load " << archive_name << " archive" << std::endl;
        return false;
    }
    std::cout << "Loaded " << archive_name << " archive" << std::endl;
    return true;
}

/**
 * @brief 清除纹理缓存
 * 
 * 在切换地图或内存不足时调用，释放缓存的纹理资源
 */
void MapRenderer::clear_cache() {
    texture_cache_.clear();
    pending_prefetch_.clear();
    tile_offsets_.clear();
}

/**
 * @brief 获取图块纹理
 * @param archive_name 档案名称
 * @param tile_index 图块索引
 * @return 纹理指针，如果加载失败返回nullptr
 * 
 * 使用缓存机制：首次加载时创建纹理并缓存，后续直接返回缓存的纹理
 */
std::shared_ptr<Texture> MapRenderer::get_tile_texture(const std::string& archive_name,
                                                       const TextureKey& key) {
    if (key.archive_id == 0) {
        return nullptr;
    }
    const int tile_index = static_cast<int>(key.frame_index);

    if (auto cached = texture_cache_.get(key)) {
        return cached;
    }

    // 异步模式：避免主线程阻塞，缺失时返回空并触发预取
    if (async_loader_) {
        if (pending_prefetch_.insert(key).second) {
            // Guard async callback in case MapRenderer is destroyed before poll dispatch.
            std::weak_ptr<CallbackGuard> weak_guard = callback_guard_;
            async_loader_->request_sprite(archive_name, tile_index,
                [this, key, weak_guard](std::optional<Sprite> sprite_opt) {
                    auto guard = weak_guard.lock();
                    if (!guard || !guard->alive.load(std::memory_order_acquire)) {
                        return;
                    }
                    if (sprite_opt && sprite_opt->is_valid()) {
                        tile_offsets_[key] = {sprite_opt->offset_x, sprite_opt->offset_y};
                        texture_cache_.get_or_create(key, *sprite_opt);
                    }
                    pending_prefetch_.erase(key);
                });
        }
        return nullptr;
    }
    
    // 从资源管理器获取精灵数据
    auto sprite_opt = resource_provider_.get_sprite(archive_name, tile_index);
    if (!sprite_opt) {
        return nullptr;
    }

    tile_offsets_[key] = {sprite_opt->offset_x, sprite_opt->offset_y};

    // 获取或创建纹理（内部有缓存机制）
    return texture_cache_.get_or_create(key, *sprite_opt);
}

/**
 * @brief 渲染单个图块
 * @param tile_index 图块索引
 * @param screen_x 屏幕X坐标
 * @param screen_y 屏幕Y坐标
 * @param archive_name 档案名称
 * @param layer 当前层
 * @param blend 是否使用混合绘制
 */
void MapRenderer::render_tile(const TextureKey& key, int screen_x, int screen_y,
                              const std::string& archive_name, MapLayer layer, bool blend) {
    (void)blend;
    // 获取图块纹理
    auto texture = get_tile_texture(archive_name, key);
    if (!texture) {
        stats_.cache_misses++;
        // 异步加载中，绘制占位块避免空白
        Rect placeholder{screen_x, screen_y, constants::TILE_WIDTH, constants::TILE_HEIGHT};
        renderer_.draw_rect(placeholder, MapRenderConstants::kPlaceholderTileColor);
        return;
    }

    // 更新统计
    stats_.cache_hits++;
    stats_.tiles_rendered++;

    auto offset_it = tile_offsets_.find(key);
    if (offset_it == tile_offsets_.end() && !async_loader_) {
        const int tile_index = static_cast<int>(key.frame_index);
        auto sprite_opt = resource_provider_.get_sprite(archive_name, tile_index);
        if (sprite_opt) {
            tile_offsets_[key] = {sprite_opt->offset_x, sprite_opt->offset_y};
            offset_it = tile_offsets_.find(key);
        }
    }

    if (layer == MapLayer::OBJECTS && offset_it != tile_offsets_.end()) {
        // 物件层锚点在格子底部居中
        const int draw_x = screen_x + constants::TILE_WIDTH / 2 - texture->width() / 2 +
                           offset_it->second.offset_x;
        const int draw_y = screen_y + constants::TILE_HEIGHT - texture->height() +
                           offset_it->second.offset_y;
        renderer_.draw_texture(*texture, draw_x, draw_y);
        return;
    }

    renderer_.draw_texture(*texture, screen_x, screen_y);
}

// =============================================================================
// 调试功能
// =============================================================================

/**
 * @brief 渲染调试网格
 * @param camera 摄像机
 * 
 * 在地图上绘制网格线，便于调试地图块对齐
 */
void MapRenderer::render_debug_grid(const Camera& camera) {
    if (!map_data_) return;
    
    // 获取可见范围
    Rect visible = camera.get_visible_tile_bounds();
    
    int start_x = std::max(0, visible.x);
    int start_y = std::max(0, visible.y);
    int end_x = std::min(map_data_->width, visible.x + visible.width);
    int end_y = std::min(map_data_->height, visible.y + visible.height);
    
    Color grid_color = MapRenderConstants::kDebugGridColor;
    
    // 绘制垂直线
    for (int x = start_x; x <= end_x; ++x) {
        Position top = tile_to_screen(x, start_y, camera);
        Position bottom = tile_to_screen(x, end_y, camera);
        renderer_.draw_line(top.x, top.y, bottom.x, bottom.y, grid_color);
    }
    
    // 绘制水平线
    for (int y = start_y; y <= end_y; ++y) {
        Position left = tile_to_screen(start_x, y, camera);
        Position right = tile_to_screen(end_x, y, camera);
        renderer_.draw_line(left.x, left.y, right.x, right.y, grid_color);
    }
}

/**
 * @brief 渲染可行走区域覆盖层
 * @param camera 摄像机
 * 
 * 用颜色标记地图块的可行走性：
 * - 绿色：可行走
 * - 红色：不可行走（阻挡）
 */
void MapRenderer::render_walkability_overlay(const Camera& camera) {
    if (!map_data_) return;
    
    // 获取可见范围
    Rect visible = camera.get_visible_tile_bounds();
    
    int start_x = std::max(0, visible.x);
    int start_y = std::max(0, visible.y);
    int end_x = std::min(map_data_->width, visible.x + visible.width);
    int end_y = std::min(map_data_->height, visible.y + visible.height);
    
    Color walkable_color = MapRenderConstants::kWalkableOverlayColor;
    Color blocked_color = MapRenderConstants::kBlockedOverlayColor;
    
    // 遍历可见范围内的每个地图块
    for (int y = start_y; y < end_y; ++y) {
        for (int x = start_x; x < end_x; ++x) {
            Position screen_pos = tile_to_screen(x, y, camera);
            
            // 根据可行走性选择颜色
            bool walkable = map_data_->is_walkable(x, y);
            Color color = walkable ? walkable_color : blocked_color;
            
            // 计算图块矩形（考虑缩放）
            Rect tile_rect = {
                screen_pos.x,
                screen_pos.y,
                static_cast<int>(constants::TILE_WIDTH * camera.zoom),
                static_cast<int>(constants::TILE_HEIGHT * camera.zoom)
            };
            
            renderer_.draw_rect(tile_rect, color);
        }
    }
}

// =============================================================================
// 坐标转换工具方法
// =============================================================================

/**
 * @brief 将地图块坐标转换为屏幕坐标
 * @param tile_x 地图块X坐标
 * @param tile_y 地图块Y坐标
 * @param camera 摄像机
 * @return 屏幕坐标
 * 
 * 转换步骤
 * 1. 地图块坐标 -> 世界像素坐标（乘以图块尺寸）
 * 2. 世界像素坐标 -> 屏幕坐标（应用摄像机变换）
 */
Position MapRenderer::tile_to_screen(int tile_x, int tile_y, const Camera& camera) const {
    // 转换为世界像素坐标
    float world_x = static_cast<float>(tile_x * constants::TILE_WIDTH);
    float world_y = static_cast<float>(tile_y * constants::TILE_HEIGHT);
    
    // 应用摄像机变换
    // 1. 减去摄像机位置（摄像机中心对应的世界坐标）
    // 2. 乘以缩放因子
    // 3. 加上视口中心偏移（使摄像机位置显示在屏幕中心）
    int screen_x = static_cast<int>((world_x - camera.x) * camera.zoom + camera.viewport_width / 2);
    int screen_y = static_cast<int>((world_y - camera.y) * camera.zoom + camera.viewport_height / 2);
    
    return {screen_x, screen_y};
}

/**
 * @brief 检查地图块是否在视口内可见
 * @param tile_x 地图块X坐标
 * @param tile_y 地图块Y坐标
 * @param camera 摄像机
 * @return 可见返回true
 * 
 * 使用额外的边距来处理超出图块边界的大型物件
 */
bool MapRenderer::is_tile_visible(int tile_x, int tile_y, const Camera& camera) const {
    Position screen_pos = tile_to_screen(tile_x, tile_y, camera);
    
    // 检查是否在视口内（带边距）
    // 边距用于处理超出图块边界的大型物件（如树木、建筑）
    const int margin = MapRenderConstants::kTileVisibilityMarginPixels;
    return screen_pos.x >= -margin && 
           screen_pos.x < camera.viewport_width + margin &&
           screen_pos.y >= -margin && 
           screen_pos.y < camera.viewport_height + margin;
}

} // namespace mir2::game::map
