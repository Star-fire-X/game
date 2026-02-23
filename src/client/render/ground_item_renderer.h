/**
 * @file ground_item_renderer.h
 * @brief 地面物品渲染器
 *
 * 负责在地图上绘制地面掉落物品的图标和名称标签。
 * 遵循 ActorRenderer 相同的纹理加载和屏幕坐标计算模式。
 */

#ifndef LEGEND2_CLIENT_RENDER_GROUND_ITEM_RENDERER_H
#define LEGEND2_CLIENT_RENDER_GROUND_ITEM_RENDERER_H

#include "render/renderer.h"
#include "render/i_texture_cache.h"
#include "render/camera.h"
#include "client/resource/resource_loader.h"
#include "client/game/entity_manager.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace mir2::render {

/**
 * @brief 地面物品渲染器
 *
 * 从 Items.wil 档案加载物品精灵图，按 template_id 索引绘制到屏幕上。
 * 同时绘制物品名称标签（当名称非空时）。
 */
class GroundItemRenderer {
public:
    GroundItemRenderer(SDLRenderer& renderer,
                       mir2::client::ResourceManager& resource_manager,
                       ITextureCache& texture_cache);

    ~GroundItemRenderer();

    /// 设置数据目录（包含 WIL 文件的路径）
    void set_data_path(const std::string& data_path) { data_path_ = data_path; }

    /// 加载 Items.wil 档案
    bool load_archives();

    /// 绘制单个地面物品
    /// @param entity 地面物品实体
    /// @param camera 摄像机
    void draw_ground_item(const mir2::game::Entity& entity, const Camera& camera);

    /// 绘制物品名称标签
    /// @param name 物品名称
    /// @param screen_x 屏幕X坐标
    /// @param screen_y 屏幕Y坐标
    void draw_item_label(const std::string& name, int screen_x, int screen_y);

    /// 批量绘制地面物品
    /// @param items 地面物品实体列表
    /// @param camera 摄像机
    void draw_ground_items(const std::vector<mir2::game::Entity*>& items,
                           const Camera& camera);

    /// 根据 template_id 计算精灵帧索引
    /// @param template_id 物品模板ID
    /// @return WIL 档案中的帧索引
    static int get_sprite_index(uint32_t template_id);

private:
    /// 计算地面物品的屏幕坐标
    void calc_screen_pos(const mir2::game::Entity& entity, const Camera& camera,
                         int& out_x, int& out_y);

    /// 获取物品帧纹理
    SDL_Texture* get_frame_texture(int frame_index,
                                   int& out_offset_x, int& out_offset_y);

    /// 尝试加载 WIL 档案
    bool try_load_archive(const std::string& archive_name);

    SDLRenderer& renderer_;
    mir2::client::ResourceManager& resource_manager_;
    ITextureCache& texture_cache_;

    std::string data_path_ = "Data/";

    // 帧缓存
    struct CachedFrame {
        std::weak_ptr<Texture> texture;
        int offset_x = 0;
        int offset_y = 0;
    };
    std::unordered_map<TextureKey, CachedFrame, TextureKeyHash> frame_cache_;

    // 已加载的档案
    std::unordered_map<std::string, bool> loaded_archives_;
    std::unordered_set<std::string> failed_archives_;

    bool resources_loaded_ = false;
};

} // namespace mir2::render

#endif // LEGEND2_CLIENT_RENDER_GROUND_ITEM_RENDERER_H
