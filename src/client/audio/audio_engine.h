// =============================================================================
// Legend2 音频管理器 (Audio Manager)
// 
// 功能说明:
//   - 基于SDL2_mixer的音频系统
//   - 支持WAV格式音效播放
//   - 支持MP3/OGG格式背景音乐
//   - 提供音量控制(主音量、音乐、音效)
//
// 主要组件:
//   - AudioConfig: 音频系统配置
//   - SoundEffect: 音效资源RAII封装
//   - Music: 音乐资源RAII封装
//   - IAudioManager: 音频管理器接口
//   - AudioManager: SDL2_mixer音频管理器实现
// =============================================================================

#ifndef LEGEND2_AUDIO_MANAGER_H
#define LEGEND2_AUDIO_MANAGER_H

#include <string>
#include <unordered_map>
#include <memory>
#include <optional>
#include "common/types.h"

#ifdef HAS_SDL2_MIXER
#include <SDL_mixer.h>
#endif

namespace mir2::audio {

// 引入公共类型定义
using namespace mir2::common;

// =============================================================================
// 音频配置 (Audio Configuration)
// =============================================================================

/// 音频系统配置结构
struct AudioConfig {
    int frequency = 44100;          // 采样率(Hz)
    int channels = 2;               // 声道数(1=单声道, 2=立体声)
    int chunk_size = 2048;          // 音频缓冲区大小
    int max_channels = 16;          // 最大同时播放的音效数
    float master_volume = 1.0f;     // 主音量(0.0 - 1.0)
    float music_volume = 0.7f;      // 音乐音量(0.0 - 1.0)
    float sfx_volume = 1.0f;        // 音效音量(0.0 - 1.0)
};

// =============================================================================
// 音效句柄 (Sound Effect Handle)
// =============================================================================

#ifdef HAS_SDL2_MIXER
/// Mix_Chunk的RAII封装(音效资源)
/// 自动管理音效资源的生命周期
class SoundEffect {
public:
    SoundEffect() = default;
    explicit SoundEffect(Mix_Chunk* chunk) : chunk_(chunk) {}
    ~SoundEffect() { if (chunk_) Mix_FreeChunk(chunk_); }
    
    // Move only
    SoundEffect(SoundEffect&& other) noexcept : chunk_(other.chunk_) {
        other.chunk_ = nullptr;
    }
    SoundEffect& operator=(SoundEffect&& other) noexcept {
        if (this != &other) {
            if (chunk_) Mix_FreeChunk(chunk_);
            chunk_ = other.chunk_;
            other.chunk_ = nullptr;
        }
        return *this;
    }
    
    // No copy
    SoundEffect(const SoundEffect&) = delete;
    SoundEffect& operator=(const SoundEffect&) = delete;
    
    Mix_Chunk* get() const { return chunk_; }
    bool valid() const { return chunk_ != nullptr; }
    
private:
    Mix_Chunk* chunk_ = nullptr;
};

/// Mix_Music的RAII封装(背景音乐资源)
/// 自动管理音乐资源的生命周期
class Music {
public:
    Music() = default;
    explicit Music(Mix_Music* music) : music_(music) {}
    ~Music() { if (music_) Mix_FreeMusic(music_); }
    
    // Move only
    Music(Music&& other) noexcept : music_(other.music_) {
        other.music_ = nullptr;
    }
    Music& operator=(Music&& other) noexcept {
        if (this != &other) {
            if (music_) Mix_FreeMusic(music_);
            music_ = other.music_;
            other.music_ = nullptr;
        }
        return *this;
    }
    
    // No copy
    Music(const Music&) = delete;
    Music& operator=(const Music&) = delete;
    
    Mix_Music* get() const { return music_; }
    bool valid() const { return music_ != nullptr; }
    
private:
    Mix_Music* music_ = nullptr;
};
#endif // HAS_SDL2_MIXER

// =============================================================================
// 音频管理器接口 (Audio Manager Interface)
// =============================================================================

/// 音频管理器接口
/// 定义音频系统的所有功能
class IAudioManager {
public:
    virtual ~IAudioManager() = default;
    
    /// 初始化音频系统
    /// @param config 音频配置
    /// @return 初始化成功返回true
    virtual bool initialize(const AudioConfig& config = AudioConfig{}) = 0;
    
    /// 关闭音频系统
    virtual void shutdown() = 0;
    
    /// 检查音频系统是否已初始化
    virtual bool is_initialized() const = 0;
    
    // --- 音效 (Sound Effects) ---
    
    /// 从文件加载WAV音效
    /// @param name 音效名称(用于后续引用)
    /// @param filepath 文件路径
    /// @return 加载成功返回true
    virtual bool load_sound(const std::string& name, const std::string& filepath) = 0;
    
    /// 播放已加载的音效
    /// @param name 音效名称
    /// @param loops 循环次数(-1=无限循环, 0=播放一次)
    /// @return 播放音效的通道号,失败返回-1
    virtual int play_sound(const std::string& name, int loops = 0) = 0;
    
    /// 停止指定通道的音效
    virtual void stop_sound(int channel) = 0;
    
    /// 停止所有音效
    virtual void stop_all_sounds() = 0;
    
    /// 检查音效是否已加载
    virtual bool is_sound_loaded(const std::string& name) const = 0;
    
    /// 卸载音效
    virtual void unload_sound(const std::string& name) = 0;
    
    // --- 音乐 (Music) ---
    
    /// 从文件加载音乐(支持MP3、OGG、WAV等)
    /// @param name 音乐名称
    /// @param filepath 文件路径
    /// @return 加载成功返回true
    virtual bool load_music(const std::string& name, const std::string& filepath) = 0;
    
    /// 播放已加载的音乐
    /// @param name 音乐名称
    /// @param loops 循环次数(-1=无限循环, 0=播放一次)
    /// @param fade_in_ms 淡入时间(毫秒,0=无淡入)
    /// @return 播放成功返回true
    virtual bool play_music(const std::string& name, int loops = -1, int fade_in_ms = 0) = 0;
    
    /// 停止当前播放的音乐
    /// @param fade_out_ms 淡出时间(毫秒,0=立即停止)
    virtual void stop_music(int fade_out_ms = 0) = 0;
    
    /// 暂停音乐
    virtual void pause_music() = 0;
    
    /// 恢复暂停的音乐
    virtual void resume_music() = 0;
    
    /// 检查音乐是否正在播放
    virtual bool is_music_playing() const = 0;
    
    /// 检查音乐是否已暂停
    virtual bool is_music_paused() const = 0;
    
    /// 检查音乐是否已加载
    virtual bool is_music_loaded(const std::string& name) const = 0;
    
    /// 卸载音乐
    virtual void unload_music(const std::string& name) = 0;
    
    /// 获取当前播放的音乐名称
    virtual std::string get_current_music() const = 0;
    
    // --- 音量控制 (Volume Control) ---
    
    /// 设置主音量(影响所有音频)
    virtual void set_master_volume(float volume) = 0;
    
    /// 获取主音量
    virtual float get_master_volume() const = 0;
    
    /// 设置音乐音量
    virtual void set_music_volume(float volume) = 0;
    
    /// 获取音乐音量
    virtual float get_music_volume() const = 0;
    
    /// 设置音效音量
    virtual void set_sfx_volume(float volume) = 0;
    
    /// 获取音效音量
    virtual float get_sfx_volume() const = 0;
    
    // --- 工具方法 (Utility) ---
    
    /// 清除所有已加载的音效和音乐
    virtual void clear_all() = 0;
    
    /// 获取已加载的音效数量
    virtual size_t get_loaded_sound_count() const = 0;
    
    /// 获取已加载的音乐数量
    virtual size_t get_loaded_music_count() const = 0;
};

// =============================================================================
// SDL2_mixer 音频管理器实现 (SDL2_mixer Audio Manager Implementation)
// =============================================================================

/// 基于SDL2_mixer的音频管理器实现
/// 提供完整的音效和音乐播放功能
class AudioManager : public IAudioManager {
public:
    AudioManager();
    ~AudioManager() override;
    
    // IAudioManager interface
    bool initialize(const AudioConfig& config = AudioConfig{}) override;
    void shutdown() override;
    bool is_initialized() const override;
    
    // Sound effects
    bool load_sound(const std::string& name, const std::string& filepath) override;
    int play_sound(const std::string& name, int loops = 0) override;
    void stop_sound(int channel) override;
    void stop_all_sounds() override;
    bool is_sound_loaded(const std::string& name) const override;
    void unload_sound(const std::string& name) override;
    
    // Music
    bool load_music(const std::string& name, const std::string& filepath) override;
    bool play_music(const std::string& name, int loops = -1, int fade_in_ms = 0) override;
    void stop_music(int fade_out_ms = 0) override;
    void pause_music() override;
    void resume_music() override;
    bool is_music_playing() const override;
    bool is_music_paused() const override;
    bool is_music_loaded(const std::string& name) const override;
    void unload_music(const std::string& name) override;
    std::string get_current_music() const override;
    
    // Volume control
    void set_master_volume(float volume) override;
    float get_master_volume() const override;
    void set_music_volume(float volume) override;
    float get_music_volume() const override;
    void set_sfx_volume(float volume) override;
    float get_sfx_volume() const override;
    
    // Utility
    void clear_all() override;
    size_t get_loaded_sound_count() const override;
    size_t get_loaded_music_count() const override;

private:
    bool initialized_ = false;           // 是否已初始化
    AudioConfig config_;                 // 音频配置
    std::string current_music_name_;     // 当前播放的音乐名称
    
#ifdef HAS_SDL2_MIXER
    std::unordered_map<std::string, std::unique_ptr<SoundEffect>> sounds_;      // 音效资源映射
    std::unordered_map<std::string, std::unique_ptr<Music>> music_tracks_;      // 音乐资源映射
#endif
    
    /// 应用音量设置到SDL_mixer
    void apply_volume_settings();
    
    /// 将音量值限制在有效范围[0.0, 1.0]
    static float clamp_volume(float volume);
};

} // namespace mir2::audio

#endif // LEGEND2_AUDIO_MANAGER_H
