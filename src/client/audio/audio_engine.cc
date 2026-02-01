// =============================================================================
// Legend2 音频管理器实现 (Audio Manager Implementation)
// 
// 功能说明:
//   - 基于SDL2_mixer的音频系统实现
//   - 管理音效和背景音乐的加载、播放、停止
//   - 提供音量控制功能
// =============================================================================

#include "audio/audio_engine.h"
#include <algorithm>
#include <iostream>

namespace mir2::audio {

// =============================================================================
// AudioManager 实现 - 音频管理器
// =============================================================================

AudioManager::AudioManager() = default;

AudioManager::~AudioManager() {
    shutdown();  // 释放资源并清理
}

/// 鍒濆鍖栭煶棰戠郴缁?
/// @param config 音频配置
/// @return 初始化成功返回true
bool AudioManager::initialize(const AudioConfig& config) {
    if (initialized_) {
        return true;  // 已初始化,直接返回
    }
    
#ifdef HAS_SDL2_MIXER
    // 鍒濆鍖朣DL_mixer
    if (Mix_OpenAudio(config.frequency, MIX_DEFAULT_FORMAT, 
                      config.channels, config.chunk_size) < 0) {
        std::cerr << "AudioManager: Failed to initialize SDL_mixer: " 
                  << Mix_GetError() << std::endl;
        return false;
    }
    
    // 分配音效混音通道
    Mix_AllocateChannels(config.max_channels);
    
    // 保存配置
    config_ = config;
    
    // 应用初始音量设置
    apply_volume_settings();
    
    initialized_ = true;
    std::cout << "AudioManager: Initialized successfully" << std::endl;
    return true;
#else
    std::cerr << "AudioManager: SDL2_mixer not available" << std::endl;
    return false;
#endif
}

/// 关闭音频系统
/// 停止所有音频并释放资源
void AudioManager::shutdown() {
    if (!initialized_) {
        return;
    }
    
#ifdef HAS_SDL2_MIXER
    // 鍋滄鎵€鏈夐煶棰?
    stop_all_sounds();
    stop_music(0);
    
    // 清除所有已加载的资源
    clear_all();
    
    // 关闭SDL_mixer
    Mix_CloseAudio();
    
    std::cout << "AudioManager: Shutdown complete" << std::endl;
#endif
    
    initialized_ = false;
}

bool AudioManager::is_initialized() const {
    return initialized_;
}

// =============================================================================
// Sound Effects
// =============================================================================

bool AudioManager::load_sound(const std::string& name, const std::string& filepath) {
#ifdef HAS_SDL2_MIXER
    if (!initialized_) {
        std::cerr << "AudioManager: Not initialized" << std::endl;
        return false;
    }
    
    // Check if already loaded
    if (sounds_.find(name) != sounds_.end()) {
        return true;
    }
    
    // Load WAV file
    Mix_Chunk* chunk = Mix_LoadWAV(filepath.c_str());
    if (!chunk) {
        std::cerr << "AudioManager: Failed to load sound '" << filepath 
                  << "': " << Mix_GetError() << std::endl;
        return false;
    }
    
    sounds_[name] = std::make_unique<SoundEffect>(chunk);
    return true;
#else
    (void)name;
    (void)filepath;
    return false;
#endif
}

int AudioManager::play_sound(const std::string& name, int loops) {
#ifdef HAS_SDL2_MIXER
    if (!initialized_) {
        return -1;
    }
    
    auto it = sounds_.find(name);
    if (it == sounds_.end() || !it->second->valid()) {
        std::cerr << "AudioManager: Sound '" << name << "' not loaded" << std::endl;
        return -1;
    }
    
    // Play on first available channel (-1)
    int channel = Mix_PlayChannel(-1, it->second->get(), loops);
    if (channel < 0) {
        std::cerr << "AudioManager: Failed to play sound '" << name 
                  << "': " << Mix_GetError() << std::endl;
    }
    
    return channel;
#else
    (void)name;
    (void)loops;
    return -1;
#endif
}

void AudioManager::stop_sound(int channel) {
#ifdef HAS_SDL2_MIXER
    if (initialized_ && channel >= 0) {
        Mix_HaltChannel(channel);
    }
#else
    (void)channel;
#endif
}

void AudioManager::stop_all_sounds() {
#ifdef HAS_SDL2_MIXER
    if (initialized_) {
        Mix_HaltChannel(-1);  // -1 halts all channels
    }
#endif
}

bool AudioManager::is_sound_loaded(const std::string& name) const {
#ifdef HAS_SDL2_MIXER
    auto it = sounds_.find(name);
    return it != sounds_.end() && it->second->valid();
#else
    (void)name;
    return false;
#endif
}

void AudioManager::unload_sound(const std::string& name) {
#ifdef HAS_SDL2_MIXER
    sounds_.erase(name);
#else
    (void)name;
#endif
}

// =============================================================================
// Music
// =============================================================================

bool AudioManager::load_music(const std::string& name, const std::string& filepath) {
#ifdef HAS_SDL2_MIXER
    if (!initialized_) {
        std::cerr << "AudioManager: Not initialized" << std::endl;
        return false;
    }
    
    // Check if already loaded
    if (music_tracks_.find(name) != music_tracks_.end()) {
        return true;
    }
    
    // Load music file (supports MP3, OGG, WAV, etc.)
    Mix_Music* music = Mix_LoadMUS(filepath.c_str());
    if (!music) {
        std::cerr << "AudioManager: Failed to load music '" << filepath 
                  << "': " << Mix_GetError() << std::endl;
        return false;
    }
    
    music_tracks_[name] = std::make_unique<Music>(music);
    return true;
#else
    (void)name;
    (void)filepath;
    return false;
#endif
}

bool AudioManager::play_music(const std::string& name, int loops, int fade_in_ms) {
#ifdef HAS_SDL2_MIXER
    if (!initialized_) {
        return false;
    }
    
    auto it = music_tracks_.find(name);
    if (it == music_tracks_.end() || !it->second->valid()) {
        std::cerr << "AudioManager: Music '" << name << "' not loaded" << std::endl;
        return false;
    }
    
    int result;
    if (fade_in_ms > 0) {
        result = Mix_FadeInMusic(it->second->get(), loops, fade_in_ms);
    } else {
        result = Mix_PlayMusic(it->second->get(), loops);
    }
    
    if (result < 0) {
        std::cerr << "AudioManager: Failed to play music '" << name 
                  << "': " << Mix_GetError() << std::endl;
        return false;
    }
    
    current_music_name_ = name;
    return true;
#else
    (void)name;
    (void)loops;
    (void)fade_in_ms;
    return false;
#endif
}

void AudioManager::stop_music(int fade_out_ms) {
#ifdef HAS_SDL2_MIXER
    if (!initialized_) {
        return;
    }
    
    if (fade_out_ms > 0) {
        Mix_FadeOutMusic(fade_out_ms);
    } else {
        Mix_HaltMusic();
    }
    
    current_music_name_.clear();
#else
    (void)fade_out_ms;
#endif
}

void AudioManager::pause_music() {
#ifdef HAS_SDL2_MIXER
    if (initialized_) {
        Mix_PauseMusic();
    }
#endif
}

void AudioManager::resume_music() {
#ifdef HAS_SDL2_MIXER
    if (initialized_) {
        Mix_ResumeMusic();
    }
#endif
}

bool AudioManager::is_music_playing() const {
#ifdef HAS_SDL2_MIXER
    return initialized_ && Mix_PlayingMusic() != 0;
#else
    return false;
#endif
}

bool AudioManager::is_music_paused() const {
#ifdef HAS_SDL2_MIXER
    return initialized_ && Mix_PausedMusic() != 0;
#else
    return false;
#endif
}

bool AudioManager::is_music_loaded(const std::string& name) const {
#ifdef HAS_SDL2_MIXER
    auto it = music_tracks_.find(name);
    return it != music_tracks_.end() && it->second->valid();
#else
    (void)name;
    return false;
#endif
}

void AudioManager::unload_music(const std::string& name) {
#ifdef HAS_SDL2_MIXER
    // Stop if currently playing
    if (current_music_name_ == name) {
        stop_music(0);
    }
    music_tracks_.erase(name);
#else
    (void)name;
#endif
}

std::string AudioManager::get_current_music() const {
    return current_music_name_;
}

// =============================================================================
// Volume Control
// =============================================================================

void AudioManager::set_master_volume(float volume) {
    config_.master_volume = clamp_volume(volume);
    apply_volume_settings();
}

float AudioManager::get_master_volume() const {
    return config_.master_volume;
}

void AudioManager::set_music_volume(float volume) {
    config_.music_volume = clamp_volume(volume);
    apply_volume_settings();
}

float AudioManager::get_music_volume() const {
    return config_.music_volume;
}

void AudioManager::set_sfx_volume(float volume) {
    config_.sfx_volume = clamp_volume(volume);
    apply_volume_settings();
}

float AudioManager::get_sfx_volume() const {
    return config_.sfx_volume;
}

void AudioManager::apply_volume_settings() {
#ifdef HAS_SDL2_MIXER
    if (!initialized_) {
        return;
    }
    
    // Calculate effective volumes
    float effective_music = config_.master_volume * config_.music_volume;
    float effective_sfx = config_.master_volume * config_.sfx_volume;
    
    // SDL_mixer uses 0-128 for volume
    int music_vol = static_cast<int>(effective_music * MIX_MAX_VOLUME);
    int sfx_vol = static_cast<int>(effective_sfx * MIX_MAX_VOLUME);
    
    // Set music volume
    Mix_VolumeMusic(music_vol);
    
    // Set volume for all channels (sound effects)
    Mix_Volume(-1, sfx_vol);
#endif
}

float AudioManager::clamp_volume(float volume) {
    return std::max(0.0f, std::min(1.0f, volume));
}

// =============================================================================
// Utility
// =============================================================================

void AudioManager::clear_all() {
#ifdef HAS_SDL2_MIXER
    stop_all_sounds();
    stop_music(0);
    sounds_.clear();
    music_tracks_.clear();
    current_music_name_.clear();
#endif
}

size_t AudioManager::get_loaded_sound_count() const {
#ifdef HAS_SDL2_MIXER
    return sounds_.size();
#else
    return 0;
#endif
}

size_t AudioManager::get_loaded_music_count() const {
#ifdef HAS_SDL2_MIXER
    return music_tracks_.size();
#else
    return 0;
#endif
}

} // namespace mir2::audio
