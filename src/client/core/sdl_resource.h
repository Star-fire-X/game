// =============================================================================
// SDL 资源RAII封装 (SDL Resource RAII)
// =============================================================================

#ifndef LEGEND2_CORE_SDL_RESOURCE_H
#define LEGEND2_CORE_SDL_RESOURCE_H

#include <SDL.h>
#include <memory>

#ifdef HAS_SDL2_TTF
#include <SDL_ttf.h>
#else
struct _TTF_Font;
using TTF_Font = _TTF_Font;
#endif

#ifdef HAS_SDL2_MIXER
#include <SDL_mixer.h>
#else
struct _Mix_Music;
struct Mix_Chunk;
using Mix_Music = _Mix_Music;
#endif

namespace mir2::core {

struct SDLWindowDeleter {
    void operator()(SDL_Window* window) const noexcept {
        if (window) {
            SDL_DestroyWindow(window);
        }
    }
};

struct SDLRendererDeleter {
    void operator()(SDL_Renderer* renderer) const noexcept {
        if (renderer) {
            SDL_DestroyRenderer(renderer);
        }
    }
};

struct SDLSurfaceDeleter {
    void operator()(SDL_Surface* surface) const noexcept {
        if (surface) {
            SDL_FreeSurface(surface);
        }
    }
};

struct SDLTextureDeleter {
    void operator()(SDL_Texture* texture) const noexcept {
        if (texture) {
            SDL_DestroyTexture(texture);
        }
    }
};

struct TTFFontDeleter {
    void operator()(TTF_Font* font) const noexcept {
#ifdef HAS_SDL2_TTF
        if (font) {
            TTF_CloseFont(font);
        }
#else
        (void)font;
#endif
    }
};

struct MixMusicDeleter {
    void operator()(Mix_Music* music) const noexcept {
#ifdef HAS_SDL2_MIXER
        if (music) {
            Mix_FreeMusic(music);
        }
#else
        (void)music;
#endif
    }
};

struct MixChunkDeleter {
    void operator()(Mix_Chunk* chunk) const noexcept {
#ifdef HAS_SDL2_MIXER
        if (chunk) {
            Mix_FreeChunk(chunk);
        }
#else
        (void)chunk;
#endif
    }
};

using SDLWindowPtr = std::unique_ptr<SDL_Window, SDLWindowDeleter>;
using SDLRendererPtr = std::unique_ptr<SDL_Renderer, SDLRendererDeleter>;
using SDLSurfacePtr = std::unique_ptr<SDL_Surface, SDLSurfaceDeleter>;
using SDLTexturePtr = std::unique_ptr<SDL_Texture, SDLTextureDeleter>;
using TTFFontPtr = std::unique_ptr<TTF_Font, TTFFontDeleter>;
using MixMusicPtr = std::unique_ptr<Mix_Music, MixMusicDeleter>;
using MixChunkPtr = std::unique_ptr<Mix_Chunk, MixChunkDeleter>;

} // namespace mir2::core

#endif // LEGEND2_CORE_SDL_RESOURCE_H
