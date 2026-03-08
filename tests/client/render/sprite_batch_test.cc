#include <gtest/gtest.h>

#ifdef HAS_SDL2
#include <SDL.h>

#include <cstdint>
#include <vector>

namespace {

struct FakeTextureState {
    int width = 0;
    int height = 0;
    SDL_BlendMode blend_mode = SDL_BLENDMODE_BLEND;
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;
};

std::vector<FakeTextureState*> g_textures;
int g_render_copy_calls = 0;
#if SDL_VERSION_ATLEAST(2, 0, 18)
[[maybe_unused]] int g_render_geometry_calls = 0;
#endif

FakeTextureState* GetState(SDL_Texture* texture) {
    return reinterpret_cast<FakeTextureState*>(texture);
}

void ResetFakeSDL() {
    for (auto* state : g_textures) {
        delete state;
    }
    g_textures.clear();
    g_render_copy_calls = 0;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    g_render_geometry_calls = 0;
#endif
}

SDL_Texture* MakeFakeTexture(int width, int height, SDL_BlendMode blend_mode = SDL_BLENDMODE_BLEND) {
    auto* state = new FakeTextureState{width, height, blend_mode, 255, 255, 255, 255};
    g_textures.push_back(state);
    return reinterpret_cast<SDL_Texture*>(state);
}

void SetFakeBlendMode(SDL_Texture* texture, SDL_BlendMode blend_mode) {
    if (!texture) {
        return;
    }
    auto* state = GetState(texture);
    if (state) {
        state->blend_mode = blend_mode;
    }
}

int FakeSDL_QueryTexture(SDL_Texture* texture, Uint32* format, int* access, int* w, int* h) {
    if (!texture) {
        return -1;
    }
    auto* state = GetState(texture);
    if (!state) {
        return -1;
    }
    if (format) {
        *format = 0;
    }
    if (access) {
        *access = 0;
    }
    if (w) {
        *w = state->width;
    }
    if (h) {
        *h = state->height;
    }
    return 0;
}

int FakeSDL_GetTextureBlendMode(SDL_Texture* texture, SDL_BlendMode* blend_mode) {
    if (!texture || !blend_mode) {
        return -1;
    }
    auto* state = GetState(texture);
    if (!state) {
        return -1;
    }
    *blend_mode = state->blend_mode;
    return 0;
}

int FakeSDL_SetTextureBlendMode(SDL_Texture* texture, SDL_BlendMode blend_mode) {
    if (!texture) {
        return -1;
    }
    auto* state = GetState(texture);
    if (!state) {
        return -1;
    }
    state->blend_mode = blend_mode;
    return 0;
}

int FakeSDL_GetTextureColorMod(SDL_Texture* texture, Uint8* r, Uint8* g, Uint8* b) {
    if (!texture || !r || !g || !b) {
        return -1;
    }
    auto* state = GetState(texture);
    if (!state) {
        return -1;
    }
    *r = state->r;
    *g = state->g;
    *b = state->b;
    return 0;
}

int FakeSDL_SetTextureColorMod(SDL_Texture* texture, Uint8 r, Uint8 g, Uint8 b) {
    if (!texture) {
        return -1;
    }
    auto* state = GetState(texture);
    if (!state) {
        return -1;
    }
    state->r = r;
    state->g = g;
    state->b = b;
    return 0;
}

int FakeSDL_GetTextureAlphaMod(SDL_Texture* texture, Uint8* alpha) {
    if (!texture || !alpha) {
        return -1;
    }
    auto* state = GetState(texture);
    if (!state) {
        return -1;
    }
    *alpha = state->a;
    return 0;
}

int FakeSDL_SetTextureAlphaMod(SDL_Texture* texture, Uint8 alpha) {
    if (!texture) {
        return -1;
    }
    auto* state = GetState(texture);
    if (!state) {
        return -1;
    }
    state->a = alpha;
    return 0;
}

int FakeSDL_RenderCopy(SDL_Renderer* /*renderer*/, SDL_Texture* /*texture*/, const SDL_Rect* /*src*/, const SDL_Rect* /*dst*/) {
    ++g_render_copy_calls;
    return 0;
}

#if SDL_VERSION_ATLEAST(2, 0, 18)
int FakeSDL_RenderGeometry(SDL_Renderer* /*renderer*/, SDL_Texture* /*texture*/,
                           const SDL_Vertex* /*vertices*/, int /*num_vertices*/,
                           const int* /*indices*/, int /*num_indices*/) {
    ++g_render_geometry_calls;
    return -1;  // force fallback path
}
#endif

}  // namespace

#define SDL_QueryTexture FakeSDL_QueryTexture
#define SDL_GetTextureBlendMode FakeSDL_GetTextureBlendMode
#define SDL_SetTextureBlendMode FakeSDL_SetTextureBlendMode
#define SDL_GetTextureColorMod FakeSDL_GetTextureColorMod
#define SDL_SetTextureColorMod FakeSDL_SetTextureColorMod
#define SDL_GetTextureAlphaMod FakeSDL_GetTextureAlphaMod
#define SDL_SetTextureAlphaMod FakeSDL_SetTextureAlphaMod
#define SDL_RenderCopy FakeSDL_RenderCopy
#if SDL_VERSION_ATLEAST(2, 0, 18)
#define SDL_RenderGeometry FakeSDL_RenderGeometry
#endif

#include "render/sprite_batch.cc"

#undef SDL_QueryTexture
#undef SDL_GetTextureBlendMode
#undef SDL_SetTextureBlendMode
#undef SDL_GetTextureColorMod
#undef SDL_SetTextureColorMod
#undef SDL_GetTextureAlphaMod
#undef SDL_SetTextureAlphaMod
#undef SDL_RenderCopy
#if SDL_VERSION_ATLEAST(2, 0, 18)
#undef SDL_RenderGeometry
#endif

namespace {

class SpriteBatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        ResetFakeSDL();
    }

    void TearDown() override {
        ResetFakeSDL();
    }
};

TEST_F(SpriteBatchTest, BeginEndLifecycle) {
    SDL_Renderer* renderer = reinterpret_cast<SDL_Renderer*>(0x1);
    SDL_Texture* texture = MakeFakeTexture(16, 16);

    mir2::render::SpriteBatch batch(renderer);
    batch.begin();
    batch.draw(texture, 10, 20);

    EXPECT_EQ(g_render_copy_calls, 0);

    batch.end();
    EXPECT_EQ(g_render_copy_calls, 1);
    EXPECT_EQ(batch.get_sprite_count(), 1u);
    EXPECT_EQ(batch.get_draw_call_count(), 1u);

    batch.draw(texture, 1, 1);
    EXPECT_EQ(g_render_copy_calls, 2);
    EXPECT_EQ(batch.get_sprite_count(), 2u);
    EXPECT_EQ(batch.get_draw_call_count(), 2u);
}

TEST_F(SpriteBatchTest, SameTextureBatchedTogether) {
    SDL_Renderer* renderer = reinterpret_cast<SDL_Renderer*>(0x1);
    SDL_Texture* texture = MakeFakeTexture(32, 32);

    mir2::render::SpriteBatch batch(renderer);
    batch.begin();
    batch.draw(texture, 0, 0);
    batch.draw(texture, 5, 5);

    EXPECT_EQ(g_render_copy_calls, 0);

    batch.end();
    EXPECT_EQ(g_render_copy_calls, 2);
    EXPECT_EQ(batch.get_sprite_count(), 2u);
    EXPECT_EQ(batch.get_draw_call_count(), 2u);
}

TEST_F(SpriteBatchTest, TextureSwitchTriggersFlush) {
    SDL_Renderer* renderer = reinterpret_cast<SDL_Renderer*>(0x1);
    SDL_Texture* texture_a = MakeFakeTexture(16, 16);
    SDL_Texture* texture_b = MakeFakeTexture(16, 16);

    mir2::render::SpriteBatch batch(renderer);
    batch.begin();
    batch.draw(texture_a, 0, 0);
    EXPECT_EQ(g_render_copy_calls, 0);

    batch.draw(texture_b, 10, 10);
    EXPECT_EQ(g_render_copy_calls, 1);

    batch.end();
    EXPECT_EQ(g_render_copy_calls, 2);
    EXPECT_EQ(batch.get_sprite_count(), 2u);
}

TEST_F(SpriteBatchTest, BlendModeSwitchTriggersFlush) {
    SDL_Renderer* renderer = reinterpret_cast<SDL_Renderer*>(0x1);
    SDL_Texture* texture = MakeFakeTexture(16, 16, SDL_BLENDMODE_BLEND);

    mir2::render::SpriteBatch batch(renderer);
    batch.begin();
    batch.draw(texture, 0, 0);
    EXPECT_EQ(g_render_copy_calls, 0);

    SetFakeBlendMode(texture, SDL_BLENDMODE_ADD);
    batch.draw(texture, 5, 5);
    EXPECT_EQ(g_render_copy_calls, 1);

    batch.end();
    EXPECT_EQ(g_render_copy_calls, 2);
    EXPECT_EQ(batch.get_sprite_count(), 2u);
}

TEST_F(SpriteBatchTest, StatsCountImmediateDraws) {
    SDL_Renderer* renderer = reinterpret_cast<SDL_Renderer*>(0x1);
    SDL_Texture* texture = MakeFakeTexture(8, 8);

    mir2::render::SpriteBatch batch(renderer);
    batch.reset_stats();

    batch.draw(texture, 0, 0);
    batch.draw(texture, 2, 2);

    EXPECT_EQ(g_render_copy_calls, 2);
    EXPECT_EQ(batch.get_sprite_count(), 2u);
    EXPECT_EQ(batch.get_draw_call_count(), 2u);
}

}  // namespace
#else
TEST(SpriteBatchTest, SkippedWithoutSdl) {
    GTEST_SKIP() << "SDL2 not available; skipping SpriteBatch tests.";
}
#endif
