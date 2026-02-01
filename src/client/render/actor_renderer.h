// =============================================================================
// Legend2 角色渲染器 (Actor Renderer)
//
// 功能说明:
//   - 角色(玩家、怪物、NPC)的绘制渲染
//   - 动画帧计算和方向处理
//   - Z-order 深度排序
//   - 特效叠加(武器光效、技能特效、状态效果)
//   - 阴影渲染
//
// 移植自原版 Actor.pas 和 PlayScn.pas
// =============================================================================

#ifndef LEGEND2_ACTOR_RENDERER_H
#define LEGEND2_ACTOR_RENDERER_H

#include "render/renderer.h"
#include "render/i_texture_cache.h"
#include "game/actor_data.h"
#include "../common/types.h"
#include "client/resource/resource_loader.h"
#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_set>

namespace mir2::render {

// 引入公共类型定义
using namespace mir2::common;
using mir2::client::ResourceManager;

// 跨模块类型引用
using mir2::game::DEF_SPELL_FRAME;
using mir2::game::ColorEffect;
using mir2::game::ActorRace;
using mir2::game::ActorAction;
using mir2::game::ActionInfo;
using mir2::game::HumanAction;
using mir2::game::MonsterAction;
using mir2::game::HA;  // HumanAction 常量
using mir2::game::MA9;
using mir2::game::MA10;
using mir2::game::MA11;
using mir2::game::MA12;
using mir2::game::MA13;
using mir2::game::MA14;
using mir2::game::MA19;
using mir2::game::MA50;
using mir2::game::MAX_WP_EFFECT_FRAME;
using mir2::game::HUMAN_FRAME;
using mir2::game::WP_EFFECT_BASE;
using mir2::game::MAG_BUBBLE_BASE;
using mir2::game::MAG_BUBBLE_STRUCK_BASE;
using mir2::game::MAX_HAIR_STYLES;
using mir2::game::WEAPON_ORDER;
using mir2::game::MON_FRAME;
using mir2::game::EXPMON_FRAME;
using mir2::game::SCULMON_FRAME;
using mir2::game::ZOMBI_FRAME;
using mir2::game::MERCHANT_FRAME;
using mir2::game::MagicType;
using mir2::game::EFFECT_BASE;
using mir2::game::HIT_EFFECT_BASE;
using mir2::game::MAX_EFFECT;
using mir2::game::MAX_HIT_EFFECT;
using mir2::game::MG_READY;
using mir2::game::MG_FLY;
using mir2::game::MG_EXPLOSION;
using mir2::game::FLYBASE;
using mir2::game::EXPLOSIONBASE;
using mir2::game::FIREGUN_FRAME;

// 引入 mir2::game 命名空间的辅助函数
using mir2::game::get_color_effect_from_state;
using mir2::game::get_monster_action;

// =============================================================================
// Optional animation config loader stubs (fall back to hardcoded defaults)
// =============================================================================

struct ActionConfig {
    int start = 0;
    int frame = 0;
    int skip = 0;
    int ftime = 0;

    int get_frame_index(int direction, int frame_offset) const {
        return start + direction * (frame + skip) + frame_offset;
    }
};

struct HumanActionConfig {
    ActionConfig stand;
    ActionConfig walk;
    ActionConfig run;
    ActionConfig rush_left;
    ActionConfig rush_right;
    ActionConfig hit;
    ActionConfig heavy_hit;
    ActionConfig big_hit;
    ActionConfig fire_hit_ready;
    ActionConfig spell;
    ActionConfig sitdown;
    ActionConfig struck;
    ActionConfig die;
};

struct MonsterActionConfig {
    ActionConfig stand;
    ActionConfig walk;
    ActionConfig attack;
    ActionConfig critical;
    ActionConfig struck;
    ActionConfig die;
    ActionConfig death;
};

class AnimationConfigLoader {
public:
    explicit AnimationConfigLoader(std::string base_dir) : base_dir_(std::move(base_dir)) {}

    int load_all_configs() { return 0; }
    const HumanActionConfig* get_human_config(Gender, CharacterClass) const { return nullptr; }
    const MonsterActionConfig* get_monster_config(int) const { return nullptr; }

private:
    std::string base_dir_;
};

// =============================================================================
// Actor 基础数据结构
// =============================================================================

/// 角色基础数据
struct ActorData {
    int32_t  id = 0;                    ///< 角色唯一ID
    uint16_t map_x = 0;                 ///< 地图X坐标(格子)
    uint16_t map_y = 0;                 ///< 地图Y坐标(格子)
    uint8_t  direction = 0;             ///< 面向方向(0-7)
    uint8_t  sex = 0;                   ///< 性别(0=男, 1=女)
    ActorRace race = ActorRace::HUMAN;  ///< 种族类型
    uint8_t  hair = 0;                  ///< 发型索引
    uint8_t  dress = 0;                 ///< 衣服索引
    uint8_t  weapon = 0;                ///< 武器索引
    uint8_t  job = 0;                   ///< 职业(0=战士, 1=法师, 2=道士)
    uint16_t appearance = 0;            ///< 外观索引(怪物用)
    uint32_t state = 0;                 ///< 状态位掩码

    bool     is_dead = false;           ///< 是否死亡
    bool     is_skeleton = false;       ///< 是否骷髅状态
    bool     is_visible = true;         ///< 是否可见
    bool     war_mode = false;          ///< 是否战斗姿态

    std::string name;                   ///< 角色名称
    int32_t  name_color = 0xFFFFFF;     ///< 名称颜色
};

/// 角色动画状态
struct ActorAnimState {
    ActorAction current_action = ActorAction::STAND;  ///< 当前动作
    int      start_frame = 0;           ///< 动作起始帧
    int      end_frame = 0;             ///< 动作结束帧
    int      current_frame = 0;         ///< 当前帧
    int      frame_time = 200;          ///< 每帧持续时间(ms)
    uint64_t last_frame_tick = 0;       ///< 上次帧更新时间

    int      shift_x = 0;               ///< X方向像素偏移(移动平滑)
    int      shift_y = 0;               ///< Y方向像素偏移

    bool     reverse_frame = false;     ///< 是否反向播放
    bool     lock_end_frame = false;    ///< 是否锁定在最后一帧

    // 特效相关
    bool     use_magic = false;         ///< 是否正在施法
    bool     hit_effect = false;        ///< 是否显示攻击特效
    int      hit_effect_number = 0;     ///< 攻击特效编号
    int      magic_effect_number = 0;   ///< 魔法特效编号(EffectBase索引)
    MagicType magic_type = MagicType::READY;  ///< 魔法类型
    int      spell_frame = DEF_SPELL_FRAME;  ///< 施法帧数
    int      cur_eff_frame = 0;         ///< 当前特效帧

    // 武器特效
    bool     weapon_effect = false;     ///< 是否显示武器特效
    int      cur_wp_effect = 0;         ///< 当前武器特效帧
    uint64_t wp_effect_time = 0;        ///< 武器特效时间
    bool     hide_weapon = false;       ///< 是否隐藏武器

    // 默认帧动画
    int      def_frame = 0;             ///< 当前默认帧
    int      def_frame_count = 4;       ///< 默认帧数
    uint64_t def_frame_time = 0;        ///< 默认帧时间
    int      gen_ani_count = 0;         ///< 通用动画计数
    uint64_t gen_ani_time = 0;          ///< 通用动画计时器
    uint64_t war_mode_time = 0;         ///< 战斗姿态时间

    // 变更检测
    ActorAction last_action = ActorAction::NONE;  ///< 上次动作
    int      last_direction = -1;       ///< 上次方向

    // 冲刺方向切换
    int      rush_dir = 0;              ///< 冲刺左右切换(0=左, 1=右)
};

/// 完整的角色实体
struct Actor {
    ActorData data;                     ///< 基础数据
    ActorAnimState anim;                ///< 动画状态

    // 绘制相关
    int      render_x = 0;              ///< 渲染X坐标(屏幕)
    int      render_y = 0;              ///< 渲染Y坐标(屏幕)
    int      down_draw_level = 0;       ///< 绘制层级偏移

    // 图像偏移
    int      body_offset = 0;           ///< 身体图像偏移
    int      hair_offset = 0;           ///< 头发图像偏移
    int      weapon_offset = 0;         ///< 武器图像偏移

    // 当前绘制偏移
    int      px = 0, py = 0;            ///< 身体绘制偏移
    int      hpx = 0, hpy = 0;          ///< 头发绘制偏移
    int      wpx = 0, wpy = 0;          ///< 武器绘制偏移
};

// =============================================================================
// ActorRenderer 角色渲染器
// =============================================================================

/// 角色渲染器类
/// 负责角色的帧计算、排序和绘制
class ActorRenderer {
public:
    /// 构造函数
    /// @param renderer SDL渲染器引用
    /// @param resource_manager 资源管理器引用
    /// @param texture_cache 纹理缓存引用
    ActorRenderer(SDLRenderer& renderer, ResourceManager& resource_manager, ITextureCache& texture_cache);

    /// 析构函数
    ~ActorRenderer();

    // =========================================================================
    // 帧计算 (Frame Calculation)
    // =========================================================================

    /// 计算角色的动作帧
    /// @param actor 角色对象
    void calc_actor_frame(Actor& actor);

    /// 计算人类角色的动作帧
    /// @param actor 角色对象
    void calc_human_frame(Actor& actor);

    /// 计算怪物/NPC的动作帧
    /// @param actor 角色对象
    void calc_monster_frame(Actor& actor);

    /// 获取默认帧(站立/死亡)
    /// @param actor 角色对象
    /// @param war_mode 是否战斗姿态
    /// @return 默认帧索引
    int get_default_frame(const Actor& actor, bool war_mode);

    /// 计算移动时的像素偏移
    /// @param actor 角色对象
    /// @param direction 方向
    /// @param step 步数
    /// @param current_tick 当前tick
    /// @param max_tick 最大tick
    void calc_shift(Actor& actor, int direction, int step, int current_tick, int max_tick);

    // =========================================================================
    // 动画更新 (Animation Update)
    // =========================================================================

    /// 更新角色动画状态
    /// @param actor 角色对象
    /// @param delta_ms 帧间隔时间(ms)
    void update(Actor& actor, uint32_t delta_ms);

    /// 设置角色动作
    /// @param actor 角色对象
    /// @param action 动作类型
    void set_action(Actor& actor, ActorAction action);

    /// 动作是否完成
    bool is_action_finished(const Actor& actor) const;

    // =========================================================================
    // 渲染 (Rendering)
    // =========================================================================

    /// 绘制单个角色
    /// @param actor 角色对象
    /// @param camera 摄像机
    /// @param is_focused 是否被选中
    void draw_actor(Actor& actor, const Camera& camera, bool is_focused = false);

    /// 绘制人类角色(有装备分层)
    /// @param actor 角色对象
    /// @param screen_x 屏幕X坐标
    /// @param screen_y 屏幕Y坐标
    /// @param blend 是否混合绘制(半透明)
    /// @param color_effect 颜色特效
    void draw_human(Actor& actor, int screen_x, int screen_y,
                    bool blend, ColorEffect color_effect);

    /// 绘制怪物/NPC
    /// @param actor 角色对象
    /// @param screen_x 屏幕X坐标
    /// @param screen_y 屏幕Y坐标
    /// @param blend 是否混合绘制
    /// @param color_effect 颜色特效
    void draw_monster(Actor& actor, int screen_x, int screen_y,
                      bool blend, ColorEffect color_effect);

    /// 绘制角色阴影
    /// @param actor 角色对象
    /// @param screen_x 屏幕X坐标
    /// @param screen_y 屏幕Y坐标
    void draw_shadow(const Actor& actor, int screen_x, int screen_y);

    /// 绘制角色名称
    /// @param actor 角色对象
    /// @param screen_x 屏幕X坐标
    /// @param screen_y 屏幕Y坐标
    void draw_name(const Actor& actor, int screen_x, int screen_y);

    /// 绘制血条
    /// @param actor 角色对象
    /// @param screen_x 屏幕X坐标
    /// @param screen_y 屏幕Y坐标
    /// @param hp_percent 血量百分比(0.0-1.0)

    // =========================================================================
    // 批量渲染 (Batch Rendering)
    // =========================================================================

    /// 按Z-order排序角色列表
    /// @param actors 角色列表
    void sort_actors_by_z_order(std::vector<Actor*>& actors);

    /// 批量绘制角色(按正确绘制顺序)
    /// @param actors 角色列表
    /// @param camera 摄像机
    /// @param focus_actor 当前选中的角色(可为nullptr)
    void draw_actors(std::vector<Actor*>& actors, const Camera& camera, Actor* focus_actor = nullptr);

    // =========================================================================
    // 特效 (Effects)
    // =========================================================================

    /// 绘制魔法施放特效
    /// @param actor 角色对象
    /// @param screen_x 屏幕X坐标
    /// @param screen_y 屏幕Y坐标
    void draw_spell_effect(const Actor& actor, int screen_x, int screen_y);

    /// 绘制攻击特效
    /// @param actor 角色对象
    /// @param screen_x 屏幕X坐标
    /// @param screen_y 屏幕Y坐标
    void draw_hit_effect(const Actor& actor, int screen_x, int screen_y);

    /// 绘制武器光效
    /// @param actor 角色对象
    /// @param screen_x 屏幕X坐标
    /// @param screen_y 屏幕Y坐标
    void draw_weapon_effect(const Actor& actor, int screen_x, int screen_y);

    /// 绘制防御罩特效
    /// @param actor 角色对象
    /// @param screen_x 屏幕X坐标
    /// @param screen_y 屏幕Y坐标
    void draw_shield_effect(const Actor& actor, int screen_x, int screen_y);

    // =========================================================================
    // 资源加载 (Resource Loading)
    // =========================================================================

    /// 加载角色图像资源
    /// @param actor 角色对象
    void load_actor_surfaces(Actor& actor);

    /// 清除纹理缓存
    void clear_texture_cache();

    /// 设置数据目录
    /// @param data_path 数据目录(包含WIL文件)
    void set_data_path(const std::string& data_path) { data_path_ = data_path; }

    /// 加载角色相关的WIL档案
    /// @return 成功返回true
    bool load_archives();

private:
    /// 应用颜色特效到纹理
    void apply_color_effect(SDL_Texture* texture, ColorEffect effect);

    /// 绘制带特效的纹理
    void draw_effect_surface(SDL_Texture* texture, int x, int y,
                             bool blend, ColorEffect effect);

    /// 绘制混合纹理(半透明/叠加)
    void draw_blend(SDL_Texture* texture, int x, int y, float alpha = 0.5f);

    /// 获取图像帧纹理
    SDL_Texture* get_frame_texture(const std::string& archive, int frame_index,
                                   int& out_offset_x, int& out_offset_y);

    /// 计算屏幕坐标
    void calc_screen_pos(const Actor& actor, const Camera& camera,
                         int& out_x, int& out_y);

    /// 尝试加载单个WIL档案
    bool try_load_archive(const std::string& archive_name);

    /// 获取怪物档案名称
    std::string get_monster_archive_name(int monster_id) const;

private:
    SDLRenderer& renderer_;
    ResourceManager& resource_manager_;
    ITextureCache& texture_cache_;

    // 数据目录
    std::string data_path_ = "Data/";

    // Optional animation config overrides
    AnimationConfigLoader config_loader_;
    bool use_config_system_ = true;

    // 精灵偏移量缓存(cache_key -> {offset_x, offset_y})
    struct SpriteOffset {
        int offset_x = 0;
        int offset_y = 0;
    };
    std::unordered_map<std::string, SpriteOffset> offset_cache_;

    // 已加载的档案集合
    std::unordered_map<std::string, bool> loaded_archives_;
    std::unordered_set<std::string> failed_archives_;

    // 状态标志
    bool resources_loaded_ = false;
};

// =============================================================================
// 辅助函数
// =============================================================================

/// 方向常量定义(顺时针从北开始)
/// 0=北, 1=东北, 2=东, 3=东南, 4=南, 5=西南, 6=西, 7=西北
constexpr int DIR_UP = 0;
constexpr int DIR_UP_RIGHT = 1;
constexpr int DIR_RIGHT = 2;
constexpr int DIR_DOWN_RIGHT = 3;
constexpr int DIR_DOWN = 4;
constexpr int DIR_DOWN_LEFT = 5;
constexpr int DIR_LEFT = 6;
constexpr int DIR_UP_LEFT = 7;

/// 方向对应的X偏移
constexpr int DIR_OFFSET_X[8] = {0, 1, 1, 1, 0, -1, -1, -1};

/// 方向对应的Y偏移
constexpr int DIR_OFFSET_Y[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

/// 获取相反方向
inline int get_back_direction(int dir) {
    return (dir + 4) % 8;
}

/// 获取左转方向
inline int get_left_direction(int dir) {
    return (dir + 7) % 8;
}

/// 获取右转方向
inline int get_right_direction(int dir) {
    return (dir + 1) % 8;
}

/// 获取怪物图像偏移
/// @param race 种族类型
/// @param appearance 外观索引
/// @return 图像偏移量
int get_monster_offset(ActorRace race, int appearance);

} // namespace mir2::render

#endif // LEGEND2_ACTOR_RENDERER_H
