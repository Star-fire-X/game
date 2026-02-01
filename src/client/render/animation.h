// =============================================================================
// Legend2 动画系统 (Animation System)
//
// 功能说明:
//   - 管理角色和怪物的精灵动画
//   - 支持多种动画动作(站立、行走、攻击、施法等)
//   - 支持8方向动画
//   - 从WIL资源文件加载动画帧
//
// 主要组件:
//   - AnimationAction: 动画动作类型枚举
//   - AnimationDef: 动画序列定义
//   - AnimationSet: 实体的完整动画集
//   - AnimationState: 当前动画播放状态
//   - AnimationManager: 动画管理器，负责注册和渲染动画
//   - AnimatedEntity: 带动画的实体基类
//   - CharacterAnimator: 角色动画辅助类
// =============================================================================

#ifndef LEGEND2_ANIMATION_SYSTEM_H
#define LEGEND2_ANIMATION_SYSTEM_H

#include "render/renderer.h"
#include "common/types.h"
#include "client/resource/resource_loader.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mir2::render {

// 引入公共类型定义
using namespace mir2::common;
using mir2::client::ResourceManager;

// =============================================================================
// Animation Types
// =============================================================================

/// 动画动作类型枚举
/// 定义角色和怪物可执行的所有动画动作
enum class AnimationAction : uint8_t {
    STAND = 0,      // 站立/待机
    WALK = 1,       // 行走
    RUN = 2,        // 奔跑
    ATTACK = 3,     // 普通攻击
    CAST = 4,       // 施法
    HIT = 5,        // 受击
    DIE = 6,        // 死亡动画
    DEAD = 7,       // 死亡状态(躺在地上)
    PICKUP = 8,     // 拾取物品
    COUNT = 9       // 动作总数
};

/// 获取动画动作的字符串名称
/// @param action 动画动作类型
/// @return 动作名称字符串
inline const char* animation_action_name(AnimationAction action) {
    switch (action) {
        case AnimationAction::STAND: return "Stand";
        case AnimationAction::WALK: return "Walk";
        case AnimationAction::RUN: return "Run";
        case AnimationAction::ATTACK: return "Attack";
        case AnimationAction::CAST: return "Cast";
        case AnimationAction::HIT: return "Hit";
        case AnimationAction::DIE: return "Die";
        case AnimationAction::DEAD: return "Dead";
        case AnimationAction::PICKUP: return "Pickup";
        default: return "Unknown";
    }
}

// =============================================================================
// Animation Definition
// =============================================================================

/// 动画序列定义
/// 描述动画的帧信息和播放参数
struct AnimationDef {
    int start_frame = 0;        // 精灵表中的起始帧索引
    int frame_count = 1;        // 帧数
    int skip_frames = 0;        // 跳过的帧数(用于对齐，与ActionInfo::skip对应)
    float frame_duration = 0.1f; // 每帧持续时间(秒)
    bool loop = true;           // 是否循环播放

    /// 获取动画总时长
    /// @return 动画完整播放一次所需的时间(秒)
    float get_duration() const {
        return frame_count * frame_duration;
    }
};

/// 实体动画集，包含所有动作和方向的动画
/// 传奇2使用8方向动画系统
struct AnimationSet {
    std::string archive_name;   // WIL资源文件名(如 "Hum" 或 "Mon1")
    int base_index = 0;         // 资源文件中的基础精灵索引
    
    // 每个动作的帧数，传奇2通常使用8个方向
    // 布局: [动作][方向] = 帧数
    // 标准传奇2布局:
    // - 站立: 每个方向帧数
    // - 行走: 每个方向帧数
    // - 攻击: 每个方向帧数
    // 等等...
    
    AnimationDef actions[static_cast<int>(AnimationAction::COUNT)];
    
    /// 获取特定动作、方向和帧的精灵索引
    /// @param action 动画动作
    /// @param dir 朝向方向
    /// @param frame 当前帧号
    /// @return 在WIL资源文件中的精灵索引
    int get_frame_index(AnimationAction action, Direction dir, int frame) const {
        const AnimationDef& def = actions[static_cast<int>(action)];
        int action_offset = def.start_frame;

        // 计算方向偏移，需要考虑跳过的帧数(用于对齐动作帧)
        // 每个方向的帧数 = frame_count + skip_frames
        int frames_per_direction = def.frame_count + def.skip_frames;
        int dir_offset = static_cast<int>(dir) * frames_per_direction;
        int frame_offset = (def.frame_count > 0) ? (frame % def.frame_count) : 0;

        return base_index + action_offset + dir_offset + frame_offset;
    }
};

// =============================================================================
// Animation State
// =============================================================================

/// 实体的当前动画状态
/// 跟踪动画播放进度、当前帧、方向等信息
class AnimationState {
public:
    AnimationState() = default;
    
    /// 设置要使用的动画集
    void set_animation_set(const AnimationSet* set) { animation_set_ = set; }
    
    /// 获取当前动画集
    const AnimationSet* get_animation_set() const { return animation_set_; }
    
    /// 设置当前动作
    /// @param action 要切换到的动作
    /// @param reset 是否重置到第一帧
    void set_action(AnimationAction action, bool reset = true);
    
    /// 获取当前动作
    AnimationAction get_action() const { return current_action_; }
    
    /// 设置朝向方向
    void set_direction(Direction dir) { direction_ = dir; }
    
    /// 获取朝向方向
    Direction get_direction() const { return direction_; }
    
    /// 更新动画(推进时间)
    /// @param delta_time 距离上次更新的时间(秒)
    /// @return 对于非循环动画，完成时返回true
    bool update(float delta_time);
    
    /// 获取当前帧在精灵资源中的索引
    int get_current_frame_index() const;
    
    /// 获取动画内的当前帧号(0到frame_count-1)
    int get_current_frame() const { return current_frame_; }
    
    /// 检查动画是否正在播放
    bool is_playing() const { return playing_; }
    
    /// 检查动画是否已完成(用于非循环动画)
    bool is_completed() const { return completed_; }
    
    /// 开始/恢复播放动画
    void play() { playing_ = true; }
    
    /// 暂停动画
    void pause() { playing_ = false; }
    
    /// 停止并重置动画
    void stop();
    
    /// 重置到第一帧
    void reset();
    
    /// 设置播放速度倍率
    void set_speed(float speed) { speed_multiplier_ = speed; }
    
    /// 获取播放速度倍率
    float get_speed() const { return speed_multiplier_; }
    
private:
    const AnimationSet* animation_set_ = nullptr;  // 当前使用的动画集
    AnimationAction current_action_ = AnimationAction::STAND;  // 当前动作
    Direction direction_ = Direction::DOWN;  // 当前朝向
    
    int current_frame_ = 0;       // 当前帧号
    float frame_time_ = 0.0f;     // 当前帧已播放时间
    float speed_multiplier_ = 1.0f;  // 播放速度倍率
    
    bool playing_ = true;         // 是否正在播放
    bool completed_ = false;      // 是否已完成(非循环动画)
};

// =============================================================================
// 动画管理器 (Animation Manager)
// =============================================================================

/// 动画管理器
/// 负责管理动画定义、加载资源和渲染动画
class AnimationManager {
public:
    /// 构造函数
    /// @param renderer SDL渲染器引用
    /// @param resource_manager 资源管理器引用
    AnimationManager(SDLRenderer& renderer, ResourceManager& resource_manager);
    
    ~AnimationManager() = default;
    
    // 禁止拷贝
    AnimationManager(const AnimationManager&) = delete;
    AnimationManager& operator=(const AnimationManager&) = delete;
    
    /// 注册动画集
    /// @param name 动画集的唯一名称
    /// @param set 动画集定义
    void register_animation_set(const std::string& name, const AnimationSet& set);
    
    /// 根据名称获取动画集
    /// @param name 动画集名称
    /// @return 动画集指针，未找到时返回nullptr
    const AnimationSet* get_animation_set(const std::string& name) const;
    
    /// 创建默认的人类角色动画集
    /// @param gender 角色性别
    /// @return 人类角色的动画集
    static AnimationSet create_human_animation_set(Gender gender);
    
    /// 创建怪物动画集
    /// @param monster_id 怪物类型ID
    /// @return 怪物的动画集
    static AnimationSet create_monster_animation_set(int monster_id);
    
    /// 在屏幕位置渲染动画
    /// @param state 要渲染的动画状态
    /// @param screen_x 屏幕X坐标
    /// @param screen_y 屏幕Y坐标
    void render(const AnimationState& state, int screen_x, int screen_y);
    
    /// 在世界位置渲染动画
    /// @param state 要渲染的动画状态
    /// @param world_pos 世界坐标位置
    /// @param camera 用于坐标转换的摄像机
    void render_at_world_pos(const AnimationState& state, const Position& world_pos, const Camera& camera);
    
    /// 为动画集加载所需的资源文件
    /// @param set 要加载资源的动画集
    /// @return 加载成功返回true
    bool load_archives_for_set(const AnimationSet& set);
    
    /// 清除纹理缓存
    void clear_cache();
    
private:
    SDLRenderer& renderer_;              // SDL渲染器引用
    ResourceManager& resource_manager_;  // 资源管理器引用
    
    // 已注册的动画集映射: 名称 -> 动画集
    std::unordered_map<std::string, AnimationSet> animation_sets_;
    
    /// 获取精灵帧的纹理
    /// @param archive 资源文件名
    /// @param frame_index 帧索引
    /// @return 纹理指针
    std::shared_ptr<Texture> get_frame_texture(const std::string& archive, int frame_index);
};

// =============================================================================
// 动画实体 (Animated Entity)
// =============================================================================

/// 带动画的实体基类
/// 所有需要动画的游戏对象(角色、怪物等)都应继承此类
class AnimatedEntity {
public:
    AnimatedEntity() = default;
    virtual ~AnimatedEntity() = default;
    
    /// 获取动画状态
    AnimationState& get_animation_state() { return animation_state_; }
    const AnimationState& get_animation_state() const { return animation_state_; }
    
    /// 获取世界坐标位置
    const Position& get_position() const { return position_; }
    void set_position(const Position& pos) { position_ = pos; }
    
    /// 获取朝向方向
    Direction get_direction() const { return animation_state_.get_direction(); }
    void set_direction(Direction dir) { animation_state_.set_direction(dir); }
    
    /// 更新动画
    /// @param delta_time 帧间隔时间(秒)
    virtual void update(float delta_time);
    
    /// 播放指定动作的动画
    /// @param action 要播放的动作
    void play_action(AnimationAction action);
    
    /// 检查当前动作是否已完成
    bool is_action_complete() const { return animation_state_.is_completed(); }
    
protected:
    AnimationState animation_state_;  // 动画状态
    Position position_;               // 世界坐标位置
};

// =============================================================================
// 角色动画辅助类 (Character Animation Helper)
// =============================================================================

/// 角色动画辅助类
/// 提供角色和怪物动画的便捷设置与控制方法
class CharacterAnimator {
public:
    /// 构造函数
    /// @param animation_manager 动画管理器引用
    CharacterAnimator(AnimationManager& animation_manager);
    
    /// 为角色设置动画
    /// @param entity 要设置的实体
    /// @param char_class 角色职业
    /// @param gender 角色性别
    void setup_character(AnimatedEntity& entity, CharacterClass char_class, Gender gender);
    
    /// 为怪物设置动画
    /// @param entity 要设置的实体
    /// @param monster_id 怪物类型ID
    void setup_monster(AnimatedEntity& entity, int monster_id);
    
    /// 根据移动状态更新动作
    /// @param entity 要更新的实体
    /// @param is_moving 是否正在移动
    /// @param direction 移动方向
    void update_movement(AnimatedEntity& entity, bool is_moving, Direction direction);
    
    /// 播放攻击动画
    void play_attack(AnimatedEntity& entity);
    
    /// 播放施法动画
    void play_cast(AnimatedEntity& entity);
    
    /// 播放受击动画
    void play_hit(AnimatedEntity& entity);
    
    /// 播放死亡动画
    void play_death(AnimatedEntity& entity);
    
private:
    AnimationManager& animation_manager_;  // 动画管理器引用
};

}  // namespace mir2::render

#endif  // LEGEND2_ANIMATION_SYSTEM_H
