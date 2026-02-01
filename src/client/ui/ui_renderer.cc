/**
 * @file ui_renderer.cpp
 * @brief Legend2 UI渲染器实现
 *
 * 本文件实现了游戏的 UI 渲染系统，包括：
 * - UIRenderer: 底层 UI 绘制类，负责绘制各种 UI 元素
 *   - 伤害数字飘字效果
 *   - 按钮、面板、文本等基础元素
 *   - SDL_ttf 字体渲染支持
 */

#include "ui/ui_renderer.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>
#include <iostream>
#include <mutex>

namespace mir2::ui {

// =============================================================================
// UIRenderer UI 渲染器实现
// 负责所有 UI 元素的底层绘制
// =============================================================================

/**
 * @brief 构造函数
 * @param renderer SDL 渲染器引用
 */
UIRenderer::UIRenderer(SDLRenderer& renderer)
    : renderer_(renderer)
{
#ifdef HAS_SDL2_TTF
    // 尝试自动加载默认字体
    // 优先 Data/fonts/default.ttf -> 系统字体
    std::vector<std::string> font_paths = {
        "Data/fonts/default.ttf",
        "Data/fonts/simsun.ttc",
        "fonts/default.ttf",
#ifdef _WIN32
        "C:/Windows/Fonts/simsun.ttc",      // 中文宋体
        "C:/Windows/Fonts/msyh.ttc",        // 微软雅黑
        "C:/Windows/Fonts/simhei.ttf",      // 黑体
        "C:/Windows/Fonts/arial.ttf",       // Arial
        "C:/Windows/Fonts/consola.ttf",     // Consolas
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
#endif
    };
    
    for (const auto& path : font_paths) {
        if (load_font(path, 14)) {
            std::cout << "[UIRenderer] Loaded font: " << path << std::endl;
            break;
        }
    }
    
    if (!is_font_loaded()) {
        std::cerr << "[UIRenderer] Warning: No font loaded, using placeholder rendering" << std::endl;
    }
#endif
}

/**
 * @brief 析构函数 - 释放字体资源
 */
UIRenderer::~UIRenderer() {
    unload_font();
}

// =============================================================================
// 伤害数字飘字系统
// 用于显示战斗中的伤害、治疗、暴击等数字效果
// =============================================================================

/**
 * @brief 添加伤害数字
 * @param value 伤害值（负数表示治疗）
 * @param world_pos 显示位置（世界坐标）
 * @param is_critical 是否暴击
 * 
 * 伤害数字会向上飘动并逐渐消失
 */
void UIRenderer::add_damage_number(int value, const Position& world_pos, bool is_critical) {
    // 如果达到最大数量，移除最旧的
    if (damage_numbers_.size() >= MAX_DAMAGE_NUMBERS) {
        damage_numbers_.pop_front();
    }
    
    DamageNumber dmg;
    dmg.value = value;
    dmg.world_pos = world_pos;
    dmg.is_critical = is_critical;
    
    // 根据伤害类型设置颜色
    if (value < 0) {
        // 治疗效果（绿色）
        dmg.color = ui_colors::DAMAGE_HEAL;
        dmg.value = -value;  // 显示正数
    } else if (is_critical) {
        // 暴击伤害（黄橙色）
        dmg.color = ui_colors::DAMAGE_CRITICAL;
    } else {
        // 普通伤害（白色）
        dmg.color = ui_colors::DAMAGE_NORMAL;
    }
    
    // 添加随机水平偏移，避免数字重叠
    dmg.screen_x = static_cast<float>((rand() % 20) - 10);
    
    damage_numbers_.push_back(dmg);
}

/**
 * @brief 添加 "MISS" 未命中提示
 * @param world_pos 显示位置（世界坐标）
 */
void UIRenderer::add_miss_indicator(const Position& world_pos) {
    if (damage_numbers_.size() >= MAX_DAMAGE_NUMBERS) {
        damage_numbers_.pop_front();
    }
    
    DamageNumber dmg;
    dmg.value = 0;
    dmg.world_pos = world_pos;
    dmg.is_miss = true;
    dmg.color = ui_colors::DAMAGE_MISS;
    dmg.screen_x = static_cast<float>((rand() % 20) - 10);
    
    damage_numbers_.push_back(dmg);
}

/**
 * @brief 更新伤害数字动画
 * @param delta_time 帧间隔时间
 * 
 * 更新每个伤害数字的位置和透明度，移除已过期的数字
 */
void UIRenderer::update_damage_numbers(float delta_time) {
    auto it = damage_numbers_.begin();
    while (it != damage_numbers_.end()) {
        // update()返回false表示动画结束，需要移除
        if (!it->update(delta_time)) {
            it = damage_numbers_.erase(it);
        } else {
            ++it;
        }
    }
}

/**
 * @brief 渲染所有伤害数字
 * @param camera 摄像机（用于坐标转换）
 */
void UIRenderer::render_damage_numbers(const Camera& camera) {
    for (const auto& dmg : damage_numbers_) {
        // 将世界坐标转换为屏幕坐标
        Position screen_pos = camera.world_to_screen(dmg.world_pos);
        
        // 应用动画偏移
        int x = screen_pos.x + static_cast<int>(dmg.screen_x);
        int y = screen_pos.y + static_cast<int>(dmg.screen_y);
        
        // 应用透明度
        Color color = dmg.color;
        color.a = dmg.get_alpha();
        
        // 获取缩放因子（暴击数字会放大）
        float scale_factor = dmg.get_scale();
        
        if (dmg.is_miss) {
            // 绘制"MISS"文本
            draw_text("MISS", x - 15, y, color);
        } else {
            // 绘制伤害数字
            draw_number(dmg.value, x, y, color, scale_factor);
        }
    }
}

/**
 * @brief 清除所有伤害数字
 */
void UIRenderer::clear_damage_numbers() {
    damage_numbers_.clear();
}

// =============================================================================
// 基础UI绘制方法
// =============================================================================

/**
 * @brief 绘制面板（带背景和边框的矩形）
 * @param rect 面板区域
 * @param bg_color 背景颜色
 * @param border_color 边框颜色
 */
void UIRenderer::draw_panel(const Rect& rect, const Color& bg_color, const Color& border_color) {
    renderer_.draw_rect(rect, bg_color);
    renderer_.draw_rect_outline(rect, border_color);
}

/**
 * @brief 绘制按钮
 * @param rect 按钮区域
 * @param text 按钮文本
 * @param is_hovered 是否悬停
 * @param is_pressed 是否按下
 */
void UIRenderer::draw_button(const Rect& rect, const std::string& text,
                            bool is_hovered, bool is_pressed) {
    // 根据状态选择背景颜色
    Color bg_color = {60, 60, 60, 255};      // 默认颜色
    if (is_pressed) {
        bg_color = {40, 40, 40, 255};        // 按下状态（更暗）
    } else if (is_hovered) {
        bg_color = {80, 80, 80, 255};        // 悬停状态（更亮）
    }
    
    draw_panel(rect, bg_color, ui_colors::HP_BAR_BORDER);
    
    // 计算文本居中位置
    int text_width = static_cast<int>(text.length()) * scale(8);
    int text_x = rect.x + (rect.width - text_width) / 2;
    int text_y = rect.y + (rect.height - scale(12)) / 2;
    
    draw_text(text, text_x, text_y, Color::white());
}

// =============================================================================
// 字体管理 (Font Management)
// =============================================================================

/**
 * @brief 加载字体文件
 * @param font_path 字体文件路径
 * @param default_size 默认字体大小
 * @return 加载成功返回true
 */
bool UIRenderer::load_font(const std::string& font_path, int default_size) {
#ifdef HAS_SDL2_TTF
    // 先卸载旧字体
    unload_font();
    
    font_path_ = font_path;
    default_font_size_ = default_size;
    
    // 尝试加载默认大小的字体来验证路径是否有效
    TTF_Font* font = TTF_OpenFont(font_path.c_str(), default_size);
    if (!font) {
        std::cerr << "[UIRenderer] Failed to load font: " << font_path 
                  << " - " << TTF_GetError() << std::endl;
        return false;
    }
    
    font_cache_[default_size] = font;
    return true;
#else
    (void)font_path;
    (void)default_size;
    return false;
#endif
}

/**
 * @brief 检查字体是否已加载
 */
bool UIRenderer::is_font_loaded() const {
#ifdef HAS_SDL2_TTF
    return !font_cache_.empty();
#else
    return false;
#endif
}

/**
 * @brief 释放字体资源
 */
void UIRenderer::unload_font() {
#ifdef HAS_SDL2_TTF
    // 清理文本缓存 (避免在锁内析构纹理)
    std::unordered_map<std::string, CacheEntry> old_cache;
    {
        std::unique_lock<std::shared_mutex> lock(cache_mutex_);
        old_cache.swap(text_cache_);
    }
    
    // 关闭所有字体
    for (auto& pair : font_cache_) {
        if (pair.second) {
            TTF_CloseFont(pair.second);
        }
    }
    font_cache_.clear();
    font_path_.clear();
#endif
}

#ifdef HAS_SDL2_TTF
/**
 * @brief 获取指定大小的字体
 * @param size 字体大小
 * @return 字体指针，失败返回nullptr
 */
TTF_Font* UIRenderer::get_font(int size) {
    // 检查缓存
    auto it = font_cache_.find(size);
    if (it != font_cache_.end()) {
        return it->second;
    }
    
    // 加载新大小的字体
    if (font_path_.empty()) {
        return nullptr;
    }
    
    TTF_Font* font = TTF_OpenFont(font_path_.c_str(), size);
    if (font) {
        font_cache_[size] = font;
    }
    return font;
}

/**
 * @brief 生成文本缓存
 */
std::string UIRenderer::make_cache_key(const std::string& text, int size, const Color& color) const {
    std::ostringstream oss;
    oss << text << "|" << size << "|" 
        << static_cast<int>(color.r) << "," 
        << static_cast<int>(color.g) << ","
        << static_cast<int>(color.b) << ","
        << static_cast<int>(color.a);
    return oss.str();
}

/**
 * @brief 清理文本缓存
 */
void UIRenderer::cleanup_text_cache() {
    std::vector<std::shared_ptr<Texture>> removed;
    {
        std::unique_lock<std::shared_mutex> lock(cache_mutex_);
        if (text_cache_.size() > MAX_TEXT_CACHE_SIZE) {
            std::vector<std::unordered_map<std::string, CacheEntry>::iterator> entries;
            entries.reserve(text_cache_.size());
            for (auto it = text_cache_.begin(); it != text_cache_.end(); ++it) {
                entries.push_back(it);
            }

            std::sort(entries.begin(), entries.end(),
                      [](const auto& left, const auto& right) {
                          return left->second.last_access_frame < right->second.last_access_frame;
                      });

            size_t remove_count = entries.size() / 4;
            for (size_t i = 0; i < remove_count; ++i) {
                removed.push_back(std::move(entries[i]->second.texture));
                text_cache_.erase(entries[i]);
            }
        }
    }
}
#endif

/**
 * @brief 绘制占位符文本（无字体时的备用方案）
 * @param text 文本内容
 * @param x X坐标
 * @param y Y坐标
 * @param color 文本颜色
 */
void UIRenderer::draw_text_placeholder(const std::string& text, int x, int y, const Color& color) {
    int char_width = scale(6);
    int char_height = scale(10);
    int spacing = scale(2);
    
    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];
        if (c != ' ') {
            Rect char_rect = {
                x + static_cast<int>(i) * (char_width + spacing),
                y,
                char_width,
                char_height
            };
            renderer_.draw_rect(char_rect, color);
        }
    }
}

/**
 * @brief 绘制文本
 * @param text 文本内容
 * @param x X坐标
 * @param y Y坐标
 * @param color 文本颜色
 */
void UIRenderer::draw_text(const std::string& text, int x, int y, const Color& color) {
    draw_text_sized(text, x, y, color, default_font_size_);
}

/**
 * @brief 绘制带字体大小的文本
 * @param text 文本内容
 * @param x X坐标
 * @param y Y坐标
 * @param color 文本颜色
 * @param font_size 字体大小
 */
void UIRenderer::draw_text_sized(const std::string& text, int x, int y, const Color& color, int font_size) {
    if (text.empty()) return;
    
#ifdef HAS_SDL2_TTF
    if (!is_font_loaded()) {
        draw_text_placeholder(text, x, y, color);
        return;
    }
    
    int scaled_size = scale(font_size);
    TTF_Font* font = get_font(scaled_size);
    if (!font) {
        draw_text_placeholder(text, x, y, color);
        return;
    }
    
    // 检查文本缓存
    size_t access_frame = ++current_frame_;
    std::string cache_key = make_cache_key(text, scaled_size, color);
    std::shared_ptr<Texture> cached_texture;
    {
        std::unique_lock<std::shared_mutex> lock(cache_mutex_);
        auto cache_it = text_cache_.find(cache_key);
        if (cache_it != text_cache_.end() && cache_it->second.texture) {
            cache_it->second.last_access_frame = access_frame;
            cached_texture = cache_it->second.texture;
        }
    }
    
    if (cached_texture) {
        renderer_.draw_texture(*cached_texture, x, y);
        return;
    }
    
    // 渲染新文本
    SDL_Color sdl_color = {color.r, color.g, color.b, color.a};
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), sdl_color);
    
    if (!surface) {
        draw_text_placeholder(text, x, y, color);
        return;
    }
    
    SDL_Texture* sdl_texture = SDL_CreateTextureFromSurface(renderer_.get_renderer(), surface);
    int w = surface->w;
    int h = surface->h;
    SDL_FreeSurface(surface);
    
    if (!sdl_texture) {
        draw_text_placeholder(text, x, y, color);
        return;
    }
    
    SDL_SetTextureBlendMode(sdl_texture, SDL_BLENDMODE_BLEND);
    
    auto texture = std::make_shared<Texture>(sdl_texture, w, h);
    std::shared_ptr<Texture> texture_to_draw = texture;
    {
        std::unique_lock<std::shared_mutex> lock(cache_mutex_);
        auto [it, inserted] = text_cache_.emplace(cache_key, CacheEntry{texture, access_frame});
        if (!inserted) {
            if (it->second.texture) {
                texture_to_draw = it->second.texture;
            } else {
                it->second.texture = texture;
            }
            it->second.last_access_frame = access_frame;
        }
    }
    cleanup_text_cache();
    
    renderer_.draw_texture(*texture_to_draw, x, y);
#else
    (void)font_size;
    draw_text_placeholder(text, x, y, color);
#endif
}

/**
 * @brief 获取文本渲染宽度
 * @param text 文本内容
 * @return 文本宽度(像素)
 */
int UIRenderer::get_text_width(const std::string& text) {
#ifdef HAS_SDL2_TTF
    if (!is_font_loaded() || text.empty()) {
        return static_cast<int>(text.length()) * (scale(6) + scale(2));
    }
    
    int font_size = scale(default_font_size_);
    TTF_Font* font = get_font(font_size);
    if (!font) {
        return static_cast<int>(text.length()) * (scale(6) + scale(2));
    }
    
    int w = 0, h = 0;
    TTF_SizeUTF8(font, text.c_str(), &w, &h);
    return w;
#else
    return static_cast<int>(text.length()) * (scale(6) + scale(2));
#endif
}

/**
 * @brief 获取文本渲染高度
 * @return 文本高度(像素)
 */
int UIRenderer::get_text_height() {
#ifdef HAS_SDL2_TTF
    if (!is_font_loaded()) {
        return scale(10);
    }
    
    int font_size = scale(default_font_size_);
    TTF_Font* font = get_font(font_size);
    if (!font) {
        return scale(10);
    }
    
    return TTF_FontHeight(font);
#else
    return scale(10);
#endif
}

/**
 * @brief 绘制数字
 * @param value 数值
 * @param x X坐标（居中点）
 * @param y Y坐标（居中点）
 * @param color 颜色
 * @param scale_factor 缩放因子
 */
void UIRenderer::draw_number(int value, int x, int y, const Color& color, float scale_factor) {
    std::string num_str = std::to_string(value);
    
    // 计算缩放后的尺寸
    int digit_width = static_cast<int>(scale(8) * scale_factor);
    int digit_height = static_cast<int>(scale(12) * scale_factor);
    int spacing = static_cast<int>(scale(2) * scale_factor);
    
    // 计算居中起始位置
    int total_width = static_cast<int>(num_str.length()) * (digit_width + spacing) - spacing;
    int start_x = x - total_width / 2;
    
    // 绘制每个数字
    for (size_t i = 0; i < num_str.length(); ++i) {
        Rect digit_rect = {
            start_x + static_cast<int>(i) * (digit_width + spacing),
            y - digit_height / 2,
            digit_width,
            digit_height
        };
        renderer_.draw_rect(digit_rect, color);
    }
}

/**
 * @brief 绘制鼠标光标
 * @param x 光标X坐标
 * @param y 光标Y坐标
 * @param cursor_type 光标类型(0=默认, 1=攻击, 2=对话)
 */
void UIRenderer::draw_cursor(int x, int y, int cursor_type) {
    // 根据光标类型选择颜色
    Color cursor_color = Color::white();
    switch (cursor_type) {
        case 1: cursor_color = Color::red(); break;    // 攻击光标
        case 2: cursor_color = Color::green(); break;  // 对话光标
        default: break;
    }
    
    // 绘制简单的箭头光标
    renderer_.draw_line(x, y, x + 12, y + 12, cursor_color);
    renderer_.draw_line(x, y, x, y + 16, cursor_color);
    renderer_.draw_line(x, y, x + 12, y + 4, cursor_color);
}

} // namespace mir2::ui
