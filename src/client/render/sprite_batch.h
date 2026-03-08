// =============================================================================
// Legend2 Sprite Batch
//
// Notes:
//   - Collects draw requests per texture
//   - Uses SDL_RenderGeometry when available
//   - Reduces sprite draw calls
// =============================================================================

#ifndef LEGEND2_RENDER_SPRITE_BATCH_H
#define LEGEND2_RENDER_SPRITE_BATCH_H

#include <SDL.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mir2::render {

class Texture;

struct SpriteVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;
};

struct BatchEntry {
    SDL_Texture* texture = nullptr;
    SDL_Rect src{0, 0, 0, 0};
    SDL_Rect dst{0, 0, 0, 0};
    uint8_t alpha = 255;
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    SDL_BlendMode blend_mode = SDL_BLENDMODE_BLEND;
};

class SpriteBatch {
public:
    explicit SpriteBatch(SDL_Renderer* renderer, size_t initial_capacity = 1024);

    void set_renderer(SDL_Renderer* renderer);

    void begin();
    void end();

    void draw(SDL_Texture* texture, int x, int y);
    void draw(const Texture& texture, int x, int y);
    void draw(SDL_Texture* texture, const SDL_Rect& src, const SDL_Rect& dst);
    void draw(SDL_Texture* texture, int x, int y, uint8_t alpha);
    void draw(const Texture& texture, int x, int y, int offset_x, int offset_y);
    void draw(SDL_Texture* texture, int x, int y,
              uint8_t r, uint8_t g, uint8_t b, uint8_t alpha,
              SDL_BlendMode blend_mode = SDL_BLENDMODE_BLEND);

    void flush();

    size_t get_draw_call_count() const { return draw_call_count_; }
    size_t get_sprite_count() const { return sprite_count_; }
    void reset_stats();

private:
    void draw(SDL_Texture* texture,
              const SDL_Rect& src,
              const SDL_Rect& dst,
              uint8_t alpha,
              SDL_BlendMode blend_mode,
              uint8_t r,
              uint8_t g,
              uint8_t b);

    void flush_batch();

    SDL_Renderer* renderer_ = nullptr;
    std::vector<BatchEntry> entries_;
    SDL_Texture* current_texture_ = nullptr;
    SDL_BlendMode current_blend_mode_ = SDL_BLENDMODE_BLEND;
    bool active_ = false;

    // Persistent geometry buffers (avoid per-flush allocation)
#if SDL_VERSION_ATLEAST(2, 0, 18)
    std::vector<SDL_Vertex> vertices_;
    std::vector<int> indices_;
#endif

    size_t draw_call_count_ = 0;
    size_t sprite_count_ = 0;
};

}  // namespace mir2::render

#endif  // LEGEND2_RENDER_SPRITE_BATCH_H
