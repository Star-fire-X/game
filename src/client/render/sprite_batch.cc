// =============================================================================
// Legend2 Sprite Batch Implementation
// =============================================================================

#include "render/sprite_batch.h"

#include "render/texture.h"

namespace mir2::render {
namespace {

uint8_t modulate_channel(uint8_t value, uint8_t mod) {
    const uint16_t scaled = static_cast<uint16_t>(value) * static_cast<uint16_t>(mod);
    return static_cast<uint8_t>((scaled + 127) / 255);
}

SDL_BlendMode get_texture_blend_mode(SDL_Texture* texture) {
    SDL_BlendMode mode = SDL_BLENDMODE_BLEND;
    if (texture) {
        SDL_GetTextureBlendMode(texture, &mode);
    }
    return mode;
}

}  // namespace

SpriteBatch::SpriteBatch(SDL_Renderer* renderer, size_t initial_capacity)
    : renderer_(renderer) {
    entries_.reserve(initial_capacity);
#if SDL_VERSION_ATLEAST(2, 0, 18)
    vertices_.reserve(initial_capacity * 4);
    indices_.reserve(initial_capacity * 6);
#endif
}

void SpriteBatch::set_renderer(SDL_Renderer* renderer) {
    renderer_ = renderer;
}

void SpriteBatch::begin() {
    active_ = true;
    entries_.clear();
    current_texture_ = nullptr;
    current_blend_mode_ = SDL_BLENDMODE_BLEND;
}

void SpriteBatch::end() {
    flush_batch();
    active_ = false;
}

void SpriteBatch::draw(SDL_Texture* texture, int x, int y) {
    if (!texture) {
        return;
    }
    int w = 0;
    int h = 0;
    if (SDL_QueryTexture(texture, nullptr, nullptr, &w, &h) != 0 || w <= 0 || h <= 0) {
        return;
    }
    SDL_Rect src{0, 0, w, h};
    SDL_Rect dst{x, y, w, h};
    draw(texture, src, dst, 255, get_texture_blend_mode(texture), 255, 255, 255);
}

void SpriteBatch::draw(const Texture& texture, int x, int y) {
    if (!texture.valid()) {
        return;
    }
    const int w = texture.width();
    const int h = texture.height();
    if (w <= 0 || h <= 0) {
        return;
    }
    SDL_Rect src{0, 0, w, h};
    SDL_Rect dst{x, y, w, h};
    draw(texture.get(), src, dst, 255, get_texture_blend_mode(texture.get()), 255, 255, 255);
}

void SpriteBatch::draw(SDL_Texture* texture, const SDL_Rect& src, const SDL_Rect& dst) {
    if (!texture) {
        return;
    }
    draw(texture, src, dst, 255, get_texture_blend_mode(texture), 255, 255, 255);
}

void SpriteBatch::draw(SDL_Texture* texture, int x, int y, uint8_t alpha) {
    if (!texture) {
        return;
    }
    int w = 0;
    int h = 0;
    if (SDL_QueryTexture(texture, nullptr, nullptr, &w, &h) != 0 || w <= 0 || h <= 0) {
        return;
    }
    SDL_Rect src{0, 0, w, h};
    SDL_Rect dst{x, y, w, h};
    draw(texture, src, dst, alpha, get_texture_blend_mode(texture), 255, 255, 255);
}

void SpriteBatch::draw(const Texture& texture, int x, int y, int offset_x, int offset_y) {
    if (!texture.valid()) {
        return;
    }
    const int w = texture.width();
    const int h = texture.height();
    if (w <= 0 || h <= 0) {
        return;
    }
    SDL_Rect src{0, 0, w, h};
    SDL_Rect dst{x - offset_x, y - offset_y, w, h};
    draw(texture.get(), src, dst, 255, get_texture_blend_mode(texture.get()), 255, 255, 255);
}

void SpriteBatch::draw(SDL_Texture* texture, int x, int y,
                       uint8_t r, uint8_t g, uint8_t b, uint8_t alpha,
                       SDL_BlendMode blend_mode) {
    if (!texture) {
        return;
    }
    int w = 0;
    int h = 0;
    if (SDL_QueryTexture(texture, nullptr, nullptr, &w, &h) != 0 || w <= 0 || h <= 0) {
        return;
    }
    SDL_Rect src{0, 0, w, h};
    SDL_Rect dst{x, y, w, h};
    draw(texture, src, dst, alpha, blend_mode, r, g, b);
}

void SpriteBatch::flush() {
    flush_batch();
}

void SpriteBatch::reset_stats() {
    draw_call_count_ = 0;
    sprite_count_ = 0;
}

void SpriteBatch::draw(SDL_Texture* texture,
                       const SDL_Rect& src,
                       const SDL_Rect& dst,
                       uint8_t alpha,
                       SDL_BlendMode blend_mode,
                       uint8_t r,
                       uint8_t g,
                       uint8_t b) {
    if (!texture || !renderer_) {
        return;
    }

    if (src.w <= 0 || src.h <= 0 || dst.w <= 0 || dst.h <= 0) {
        return;
    }

    if (!active_) {
        uint8_t prev_r = 255, prev_g = 255, prev_b = 255, prev_a = 255;
        SDL_BlendMode prev_blend = SDL_BLENDMODE_BLEND;
        SDL_GetTextureColorMod(texture, &prev_r, &prev_g, &prev_b);
        SDL_GetTextureAlphaMod(texture, &prev_a);
        SDL_GetTextureBlendMode(texture, &prev_blend);

        if (prev_blend != blend_mode) {
            SDL_SetTextureBlendMode(texture, blend_mode);
        }
        const uint8_t final_r = modulate_channel(r, prev_r);
        const uint8_t final_g = modulate_channel(g, prev_g);
        const uint8_t final_b = modulate_channel(b, prev_b);
        const uint8_t final_a = modulate_channel(alpha, prev_a);
        if (prev_r != final_r || prev_g != final_g || prev_b != final_b) {
            SDL_SetTextureColorMod(texture, final_r, final_g, final_b);
        }
        if (prev_a != final_a) {
            SDL_SetTextureAlphaMod(texture, final_a);
        }

        SDL_RenderCopy(renderer_, texture, &src, &dst);

        if (prev_a != final_a) {
            SDL_SetTextureAlphaMod(texture, prev_a);
        }
        if (prev_r != final_r || prev_g != final_g || prev_b != final_b) {
            SDL_SetTextureColorMod(texture, prev_r, prev_g, prev_b);
        }
        if (prev_blend != blend_mode) {
            SDL_SetTextureBlendMode(texture, prev_blend);
        }

        draw_call_count_++;
        sprite_count_++;
        return;
    }

    if (current_texture_ != nullptr &&
        (current_texture_ != texture || current_blend_mode_ != blend_mode)) {
        flush_batch();
    }

    if (current_texture_ == nullptr) {
        current_texture_ = texture;
        current_blend_mode_ = blend_mode;
    }

    BatchEntry entry;
    entry.texture = texture;
    entry.src = src;
    entry.dst = dst;
    entry.alpha = alpha;
    entry.r = r;
    entry.g = g;
    entry.b = b;
    entry.blend_mode = blend_mode;
    entries_.push_back(entry);
    sprite_count_++;
}

void SpriteBatch::flush_batch() {
    if (entries_.empty()) {
        current_texture_ = nullptr;
        current_blend_mode_ = SDL_BLENDMODE_BLEND;
        return;
    }

    if (!renderer_ || !current_texture_) {
        entries_.clear();
        current_texture_ = nullptr;
        current_blend_mode_ = SDL_BLENDMODE_BLEND;
        return;
    }

    int tex_w = 0;
    int tex_h = 0;
    if (SDL_QueryTexture(current_texture_, nullptr, nullptr, &tex_w, &tex_h) != 0 ||
        tex_w <= 0 || tex_h <= 0) {
        entries_.clear();
        current_texture_ = nullptr;
        current_blend_mode_ = SDL_BLENDMODE_BLEND;
        return;
    }

    SDL_BlendMode prev_blend = SDL_BLENDMODE_BLEND;
    SDL_GetTextureBlendMode(current_texture_, &prev_blend);
    if (prev_blend != current_blend_mode_) {
        SDL_SetTextureBlendMode(current_texture_, current_blend_mode_);
    }

    uint8_t mod_r = 255, mod_g = 255, mod_b = 255, mod_a = 255;
    SDL_GetTextureColorMod(current_texture_, &mod_r, &mod_g, &mod_b);
    SDL_GetTextureAlphaMod(current_texture_, &mod_a);

    bool geometry_ok = false;

#if SDL_VERSION_ATLEAST(2, 0, 18)
    vertices_.clear();
    indices_.clear();

    const float inv_w = 1.0f / static_cast<float>(tex_w);
    const float inv_h = 1.0f / static_cast<float>(tex_h);

    for (size_t i = 0; i < entries_.size(); ++i) {
        const BatchEntry& entry = entries_[i];
        if (entry.src.w <= 0 || entry.src.h <= 0 || entry.dst.w <= 0 || entry.dst.h <= 0) {
            continue;
        }

        const float x0 = static_cast<float>(entry.dst.x);
        const float y0 = static_cast<float>(entry.dst.y);
        const float x1 = static_cast<float>(entry.dst.x + entry.dst.w);
        const float y1 = static_cast<float>(entry.dst.y + entry.dst.h);

        const float u0 = static_cast<float>(entry.src.x) * inv_w;
        const float v0 = static_cast<float>(entry.src.y) * inv_h;
        const float u1 = static_cast<float>(entry.src.x + entry.src.w) * inv_w;
        const float v1 = static_cast<float>(entry.src.y + entry.src.h) * inv_h;

        const uint8_t final_r = modulate_channel(entry.r, mod_r);
        const uint8_t final_g = modulate_channel(entry.g, mod_g);
        const uint8_t final_b = modulate_channel(entry.b, mod_b);
        const uint8_t final_a = modulate_channel(entry.alpha, mod_a);
        const SDL_Color color{final_r, final_g, final_b, final_a};

        const int base_index = static_cast<int>(vertices_.size());

        vertices_.push_back(SDL_Vertex{SDL_FPoint{x0, y0}, color, SDL_FPoint{u0, v0}});
        vertices_.push_back(SDL_Vertex{SDL_FPoint{x1, y0}, color, SDL_FPoint{u1, v0}});
        vertices_.push_back(SDL_Vertex{SDL_FPoint{x1, y1}, color, SDL_FPoint{u1, v1}});
        vertices_.push_back(SDL_Vertex{SDL_FPoint{x0, y1}, color, SDL_FPoint{u0, v1}});

        indices_.push_back(base_index + 0);
        indices_.push_back(base_index + 1);
        indices_.push_back(base_index + 2);
        indices_.push_back(base_index + 0);
        indices_.push_back(base_index + 2);
        indices_.push_back(base_index + 3);
    }

    if (!vertices_.empty()) {
        if (SDL_RenderGeometry(renderer_, current_texture_,
                               vertices_.data(), static_cast<int>(vertices_.size()),
                               indices_.data(), static_cast<int>(indices_.size())) == 0) {
            draw_call_count_++;
            geometry_ok = true;
        }
    }
#endif

    if (!geometry_ok) {
        uint8_t prev_r = 255, prev_g = 255, prev_b = 255, prev_a = 255;
        SDL_GetTextureColorMod(current_texture_, &prev_r, &prev_g, &prev_b);
        SDL_GetTextureAlphaMod(current_texture_, &prev_a);

        for (const BatchEntry& entry : entries_) {
            if (entry.src.w <= 0 || entry.src.h <= 0 || entry.dst.w <= 0 || entry.dst.h <= 0) {
                continue;
            }
            const uint8_t final_r = modulate_channel(entry.r, mod_r);
            const uint8_t final_g = modulate_channel(entry.g, mod_g);
            const uint8_t final_b = modulate_channel(entry.b, mod_b);
            const uint8_t final_a = modulate_channel(entry.alpha, mod_a);

            SDL_SetTextureColorMod(current_texture_, final_r, final_g, final_b);
            SDL_SetTextureAlphaMod(current_texture_, final_a);
            SDL_RenderCopy(renderer_, current_texture_, &entry.src, &entry.dst);
            draw_call_count_++;
        }

        SDL_SetTextureColorMod(current_texture_, prev_r, prev_g, prev_b);
        SDL_SetTextureAlphaMod(current_texture_, prev_a);
    }

    if (prev_blend != current_blend_mode_) {
        SDL_SetTextureBlendMode(current_texture_, prev_blend);
    }

    entries_.clear();
    current_texture_ = nullptr;
    current_blend_mode_ = SDL_BLENDMODE_BLEND;
}

}  // namespace mir2::render
