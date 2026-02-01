// =============================================================================
// Legend2 UI 渲染器 (UI Renderer)
//
// 功能说明:
//   - 渲染浮动伤害数字
//   - 提供基础 UI 元素（面板、按钮、文本等）
//   - 支持 SDL_ttf 字体渲染
//
// 主要组件:
//   - ui_colors: UI 颜色常量
//   - DamageNumber: 浮动伤害数字
//   - UIRenderer: UI 渲染器
// =============================================================================

#ifndef LEGEND2_UI_RENDERER_H
#define LEGEND2_UI_RENDERER_H

#include "render/renderer.h"
#include "common/types.h"
#include <memory>
#include <string>
#include <deque>
#include <unordered_map>
#include <shared_mutex>

#ifdef HAS_SDL2_TTF
#include <SDL_ttf.h>
#endif

namespace mir2::ui {

// 引入公共类型定义
using namespace mir2::common;

// 跨模块类型引用
using mir2::render::SDLRenderer;
using mir2::render::Texture;
using mir2::render::Camera;

// =============================================================================
// UI颜色常量 (UI Colors)
// =============================================================================

namespace ui_colors {
    // 血条颜色
    const Color HP_BAR_BG = {40, 40, 40, 200};       // 血条背景
    const Color HP_BAR_FILL = {220, 50, 50, 255};    // 血条填充
    const Color HP_BAR_BORDER = {100, 100, 100, 255}; // 血条边框
    
    // 蓝条颜色
    const Color MP_BAR_BG = {40, 40, 40, 200};
    const Color MP_BAR_FILL = {50, 100, 220, 255};
    const Color MP_BAR_BORDER = {100, 100, 100, 255};
    
    // 经验条颜色
    const Color EXP_BAR_BG = {40, 40, 40, 200};
    const Color EXP_BAR_FILL = {220, 180, 50, 255};
    const Color EXP_BAR_BORDER = {100, 100, 100, 255};
    
    // 伤害数字颜色
    const Color DAMAGE_NORMAL = {255, 255, 255, 255};   // 普通伤害
    const Color DAMAGE_CRITICAL = {255, 200, 50, 255};  // 暴击伤害
    const Color DAMAGE_HEAL = {50, 255, 50, 255};       // 治疗
    const Color DAMAGE_MISS = {150, 150, 150, 255};     // 未命中
}

// =============================================================================
// 浮动伤害数字 (Floating Damage Number)
// =============================================================================

/// 浮动伤害数字
/// 显示伤害/治疗数并上升淡出
struct DamageNumber {
    int value = 0;              // 伤害值（负数表示治疗）
    Position world_pos;         // 伤害发生的世界坐标
    float screen_x = 0.0f;      // 当前屏幕X坐标
    float screen_y = 0.0f;      // 当前屏幕Y坐标
    float velocity_y = -80.0f;  // 上升速度
    float lifetime = 0.0f;      // 已存在时间
    float max_lifetime = 1.5f;  // 最大存在时间
    Color color = ui_colors::DAMAGE_NORMAL;  // 颜色
    bool is_critical = false;   // 是否暴击
    bool is_miss = false;       // 是否未命中
    
    /// 更新位置和生命周期
    /// @return 如果仍然存活返回true
    bool update(float delta_time) {
        lifetime += delta_time;
        screen_y += velocity_y * delta_time;
        velocity_y += 50.0f * delta_time;  // 重力效果
        return lifetime < max_lifetime;
    }
    
    /// 获取当前透明度（淡出效果）
    uint8_t get_alpha() const {
        float fade_start = max_lifetime * 0.6f;
        if (lifetime < fade_start) return 255;
        float fade_progress = (lifetime - fade_start) / (max_lifetime - fade_start);
        return static_cast<uint8_t>(255 * (1.0f - fade_progress));
    }
    
    /// 获取当前缩放（先放大后缩小）
    float get_scale() const {
        if (lifetime < 0.1f) {
            return 1.0f + lifetime * 3.0f;  // 快速放大
        }
        return 1.3f - (lifetime - 0.1f) * 0.3f;  // 缓慢缩小
    }
};

// =============================================================================
// UI 渲染器 (UI Renderer)
// =============================================================================

/// UI 渲染器
/// 负责渲染所有 UI 元素（伤害数字、按钮、文本等）
class UIRenderer {
public:
    /// 构造函数
    /// @param renderer SDL 渲染器引用
    UIRenderer(SDLRenderer& renderer);
    
    /// 析构函数 - 释放字体资源
    ~UIRenderer();
    
    // 禁止拷贝
    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;
    
    // --- 伤害数字 (Damage Numbers) ---
    
    /// 添加伤害数字显示
    /// @param value 伤害值（负数表示治疗）
    /// @param world_pos 世界坐标
    /// @param is_critical 是否暴击
    void add_damage_number(int value, const Position& world_pos, bool is_critical = false);
    
    /// 添加未命中指示器
    /// @param world_pos 世界坐标
    void add_miss_indicator(const Position& world_pos);
    
    /// 更新所有伤害数字
    /// @param delta_time 帧间隔时间
    void update_damage_numbers(float delta_time);
    
    /// 渲染所有伤害数字
    /// @param camera 用于坐标转换的摄像机
    void render_damage_numbers(const Camera& camera);
    
    /// 清除所有伤害数字
    void clear_damage_numbers();
    
    // --- 基础UI元素 (Basic UI Elements) ---
    
    /// 绘制面板背景
    /// @param rect 面板边界
    /// @param bg_color 背景颜色
    /// @param border_color 边框颜色
    void draw_panel(const Rect& rect, const Color& bg_color, const Color& border_color);
    
    /// 绘制按钮
    /// @param rect 按钮边界
    /// @param text 按钮文本
    /// @param is_hovered 是否悬停
    /// @param is_pressed 是否按下
    void draw_button(const Rect& rect, const std::string& text, 
                    bool is_hovered = false, bool is_pressed = false);
    
    /// 绘制文本
    /// @param text 要绘制的文本
    /// @param x 屏幕X坐标
    /// @param y 屏幕Y坐标
    /// @param color 文本颜色
    /// @param font_size 字体大小（可选，默认使用当前字体大小）
    void draw_text(const std::string& text, int x, int y, const Color& color = Color::white());
    
    /// 绘制带字体大小的文本
    /// @param text 要绘制的文本
    /// @param x 屏幕X坐标
    /// @param y 屏幕Y坐标
    /// @param color 文本颜色
    /// @param font_size 字体大小
    void draw_text_sized(const std::string& text, int x, int y, const Color& color, int font_size);
    
    /// 获取文本渲染宽度
    /// @param text 文本内容
    /// @return 文本宽度(像素)
    int get_text_width(const std::string& text);
    
    /// 获取文本渲染高度
    /// @return 文本高度(像素)
    int get_text_height();
    
    /// 绘制数字(使用简单的数字渲染)
    /// @param value 要绘制的数字
    /// @param x 屏幕X坐标
    /// @param y 屏幕Y坐标
    /// @param color 数字颜色
    /// @param scale 缩放因子
    void draw_number(int value, int x, int y, const Color& color = Color::white(), float scale = 1.0f);
    
    // --- 光标 (Cursor) ---
    
    /// 绘制自定义光标
    /// @param x 光标X坐标
    /// @param y 光标Y坐标
    /// @param cursor_type 光标类型(0=普通, 1=攻击, 2=对话)
    void draw_cursor(int x, int y, int cursor_type = 0);
    
    // --- 设置 (Settings) ---
    
    /// 设置UI缩放比例
    void set_ui_scale(float scale) { ui_scale_ = scale; }
    float get_ui_scale() const { return ui_scale_; }
    
    // --- 字体管理 (Font Management) ---
    
    /// 加载字体文件
    /// @param font_path 字体文件路径
    /// @param default_size 默认字体大小
    /// @return 加载成功返回true
    bool load_font(const std::string& font_path, int default_size = 14);
    
    /// 检查字体是否已加载
    /// @return 字体已加载返回true
    bool is_font_loaded() const;
    
    /// 释放字体资源
    void unload_font();

private:
    SDLRenderer& renderer_;
    
    // Damage numbers queue
    std::deque<DamageNumber> damage_numbers_;
    static constexpr size_t MAX_DAMAGE_NUMBERS = 50;
    
    // UI scale
    float ui_scale_ = 1.0f;
    
#ifdef HAS_SDL2_TTF
    // 字体管理
    std::string font_path_;                                    // 字体文件路径
    int default_font_size_ = 14;                               // 默认字体大小
    std::unordered_map<int, TTF_Font*> font_cache_;           // 按大小缓存的字体
    struct CacheEntry {
        std::shared_ptr<Texture> texture;
        size_t last_access_frame = 0;
    };
    mutable std::unordered_map<std::string, CacheEntry> text_cache_;  // 文本纹理缓存
    static constexpr size_t MAX_TEXT_CACHE_SIZE = 500;        // 最大缓存条目数
    size_t current_frame_ = 0;
    mutable std::shared_mutex cache_mutex_;
    
    /// 获取指定大小的字体
    TTF_Font* get_font(int size);
    
    /// 生成文本缓存键
    std::string make_cache_key(const std::string& text, int size, const Color& color) const;
    
    /// 清理文本缓存
    void cleanup_text_cache();
#endif
    
    /// Scale a value by UI scale
    int scale(int value) const { return static_cast<int>(value * ui_scale_); }
    
    /// 绘制占位符文本（无字体时的备用方案）
    void draw_text_placeholder(const std::string& text, int x, int y, const Color& color);
};

} // namespace mir2::ui

#endif // LEGEND2_UI_RENDERER_H
