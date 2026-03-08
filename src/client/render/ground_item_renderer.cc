/**
 * @file ground_item_renderer.cc
 * @brief 地面物品渲染器实现
 *
 * 从 Items.wil 档案加载物品精灵图并渲染到地图上。
 * 遵循 ActorRenderer 相同的纹理加载和坐标计算模式。
 */

#include "render/ground_item_renderer.h"
#include "core/path_utils.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <iostream>

namespace mir2::render {

// 地图格子尺寸（与 ActorRenderer 一致）
constexpr int TILE_W = 48;
constexpr int TILE_H = 32;

// Items 档案名称
static const std::string kItemsArchive = "Items";

GroundItemRenderer::GroundItemRenderer(SDLRenderer& renderer,
                                       mir2::client::ResourceManager& resource_manager,
                                       ITextureCache& texture_cache)
    : renderer_(renderer),
      resource_manager_(resource_manager),
      texture_cache_(texture_cache) {}

GroundItemRenderer::~GroundItemRenderer() = default;

bool GroundItemRenderer::load_archives() {
    if (data_path_.empty()) {
        data_path_ = mir2::core::find_data_path("Items.wil");
        if (data_path_.empty()) {
            data_path_ = mir2::core::find_data_path("items.wil");
        }
    }

    if (data_path_.empty()) {
        std::cerr << "[GroundItemRenderer] Could not find Data directory for Items archive\n";
        return false;
    }

    resources_loaded_ = try_load_archive(kItemsArchive);
    return resources_loaded_;
}

int GroundItemRenderer::get_sprite_index(uint32_t template_id) {
    // In Mir2, item sprites are indexed directly by template_id in the Items.wil archive.
    return static_cast<int>(template_id);
}

void GroundItemRenderer::calc_screen_pos(const mir2::game::Entity& entity,
                                          const Camera& camera,
                                          int& out_x, int& out_y) {
    const float world_x = static_cast<float>(entity.position.x * TILE_W);
    const float world_y = static_cast<float>(entity.position.y * TILE_H);
    const float screen_x = (world_x - camera.x) * camera.zoom + camera.viewport_width * 0.5f;
    const float screen_y = (world_y - camera.y) * camera.zoom + camera.viewport_height * 0.5f;
    out_x = static_cast<int>(std::lround(screen_x + TILE_W * 0.5f));
    out_y = static_cast<int>(std::lround(screen_y + TILE_H));
}

SDL_Texture* GroundItemRenderer::get_frame_texture(int frame_index,
                                                    int& out_offset_x,
                                                    int& out_offset_y) {
    out_offset_x = 0;
    out_offset_y = 0;
    if (frame_index < 0) return nullptr;

    if (!resource_manager_.is_archive_loaded(kItemsArchive)) {
        if (!try_load_archive(kItemsArchive)) {
            return nullptr;
        }
    }

    const uint16_t archive_id = ArchiveRegistry::instance().get_or_register(kItemsArchive);
    if (archive_id == 0) {
        return nullptr;
    }
    const TextureKey key{archive_id, static_cast<uint32_t>(frame_index), false};

    // Check cache
    auto it = frame_cache_.find(key);
    if (it != frame_cache_.end()) {
        if (auto tex = it->second.texture.lock()) {
            out_offset_x = it->second.offset_x;
            out_offset_y = it->second.offset_y;
            return tex->get();
        }
        out_offset_x = it->second.offset_x;
        out_offset_y = it->second.offset_y;

        auto sprite_opt = resource_manager_.get_sprite(kItemsArchive, frame_index);
        if (!sprite_opt) return nullptr;

        auto texture = texture_cache_.get_or_create(key, *sprite_opt);
        if (!texture) return nullptr;

        it->second.texture = texture;
        return texture->get();
    }

    // Full miss
    auto sprite_opt = resource_manager_.get_sprite(kItemsArchive, frame_index);
    if (!sprite_opt) return nullptr;

    out_offset_x = sprite_opt->offset_x;
    out_offset_y = sprite_opt->offset_y;

    auto texture = texture_cache_.get_or_create(key, *sprite_opt);
    if (!texture) return nullptr;

    frame_cache_[key] = {texture, out_offset_x, out_offset_y};
    return texture->get();
}

void GroundItemRenderer::draw_ground_item(const mir2::game::Entity& entity,
                                           const Camera& camera) {
    if (entity.type != mir2::game::EntityType::GroundItem) {
        return;
    }

    int screen_x = 0, screen_y = 0;
    calc_screen_pos(entity, camera, screen_x, screen_y);

    const int frame_index = get_sprite_index(entity.template_id);
    int offset_x = 0, offset_y = 0;
    SDL_Texture* texture = get_frame_texture(frame_index, offset_x, offset_y);

    if (texture) {
        int w = 0, h = 0;
        SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);
        const int draw_x = screen_x - w / 2 + offset_x;
        const int draw_y = screen_y - h + offset_y;
        renderer_.get_batch().draw(texture, draw_x, draw_y, 255, 255, 255, 255);
    }

    // Draw item name label below the sprite
    if (!entity.name.empty()) {
        draw_item_label(entity.name, screen_x, screen_y);
    }
}

void GroundItemRenderer::draw_item_label(const std::string& name,
                                          int screen_x, int screen_y) {
    // Name rendering requires font support (SDL_ttf or bitmap font).
    // The label position is calculated; actual text rendering will be
    // implemented when the font system is available.
    (void)name;
    (void)screen_x;
    (void)screen_y;
}

void GroundItemRenderer::draw_ground_items(const std::vector<mir2::game::Entity*>& items,
                                            const Camera& camera) {
    for (const auto* entity : items) {
        if (entity) {
            draw_ground_item(*entity, camera);
        }
    }
}

bool GroundItemRenderer::try_load_archive(const std::string& archive_name) {
    if (archive_name.empty()) {
        return false;
    }

    auto loaded_it = loaded_archives_.find(archive_name);
    if (loaded_it != loaded_archives_.end()) {
        return loaded_it->second;
    }

    if (failed_archives_.count(archive_name)) {
        return false;
    }

    if (resource_manager_.is_archive_loaded(archive_name)) {
        loaded_archives_[archive_name] = true;
        return true;
    }

    if (data_path_.empty()) {
        std::cerr << "[GroundItemRenderer] Data path is empty, cannot load archive '"
                  << archive_name << "'\n";
        loaded_archives_[archive_name] = false;
        failed_archives_.insert(archive_name);
        return false;
    }

    auto load_with_retry = [&](const std::string& path) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (resource_manager_.load_archive(path)) {
                return true;
            }
        }
        return false;
    };

    std::string path = data_path_ + archive_name + ".wil";
    if (load_with_retry(path)) {
        loaded_archives_[archive_name] = true;
        return true;
    }

    std::string lower_name = archive_name;
    for (auto& ch : lower_name) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (lower_name != archive_name) {
        path = data_path_ + lower_name + ".wil";
        if (load_with_retry(path)) {
            loaded_archives_[archive_name] = true;
            return true;
        }
    }

    std::cerr << "[GroundItemRenderer] Failed to load archive '" << archive_name << "'\n";
    loaded_archives_[archive_name] = false;
    failed_archives_.insert(archive_name);
    return false;
}

} // namespace mir2::render
