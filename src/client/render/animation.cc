// =============================================================================
// Legend2 动画系统实现 (Animation System Implementation)
// 
// 功能说明:
//   - 实现角色和怪物的精灵动画播放
//   - 管理动画状态更新和帧切换
//   - 从WIL资源文件加载并渲染动画帧
// =============================================================================

#include "render/animation.h"
#include "core/path_utils.h"
#include <iostream>
#include <algorithm>

namespace mir2::render {

// =============================================================================
// AnimationState 实现 - 动画状态管理
// =============================================================================

/// 设置当前动画动作
/// @param action 要切换到的动作
/// @param reset 是否重置到第一帧
void AnimationState::set_action(AnimationAction action, bool reset) {
    if (current_action_ != action || reset) {
        current_action_ = action;
        if (reset) {
            this->reset();
        }
        completed_ = false;
    }
}

/// 更新动画状态
/// 根据时间推进动画，处理循环和完成逻辑
/// @param delta_time 距上次更新的时间(秒)
/// @return 对于非循环动画，完成时返回true
bool AnimationState::update(float delta_time) {
    if (!animation_set_ || !playing_) {
        return false;  // 无动画集或未初始化,直接返回
    }
    
    const AnimationDef& def = animation_set_->actions[static_cast<int>(current_action_)];
    
    if (def.frame_count <= 1) {
        return false;  // 单帧动画,无需更新
    }
    
    // 推进时间(考虑速度倍率)
    frame_time_ += delta_time * speed_multiplier_;
    
    // 检查是否需要切换帧
    while (frame_time_ >= def.frame_duration) {
        frame_time_ -= def.frame_duration;
        current_frame_++;
        
        // 检查动画是否结束
        if (current_frame_ >= def.frame_count) {
            if (def.loop) {
                current_frame_ = 0;  // 循环播放,回到第一帧
            } else {
                current_frame_ = def.frame_count - 1;  // 停在最后一帧
                completed_ = true;
                playing_ = false;
                return true;  // 动画播放完成
            }
        }
    }
    
    return false;
}

/// 获取当前帧在精灵资源中的索引
/// 根据当前动作、方向和帧号计算实际的精灵索引
int AnimationState::get_current_frame_index() const {
    if (!animation_set_) {
        return 0;  // 无动画集,返回默认索引
    }
    return animation_set_->get_frame_index(current_action_, direction_, current_frame_);
}

/// 停止动画并重置状态
void AnimationState::stop() {
    playing_ = false;
    reset();
}

/// 重置动画到初始状态
void AnimationState::reset() {
    current_frame_ = 0;
    frame_time_ = 0.0f;
    completed_ = false;
    playing_ = true;
}

// =============================================================================
// AnimationManager 实现 - 动画管理器
// =============================================================================

/// 构造函数
/// @param renderer SDL渲染器引用
/// @param resource_manager 资源管理器引用
AnimationManager::AnimationManager(SDLRenderer& renderer, ResourceManager& resource_manager)
    : renderer_(renderer)
    , resource_manager_(resource_manager)
{
}

/// 注册动画集
/// @param name 动画集的唯一名称
/// @param set 动画集定义
void AnimationManager::register_animation_set(const std::string& name, const AnimationSet& set) {
    animation_sets_[name] = set;
}

/// 根据名称获取动画集
/// @param name 动画集名称
/// @return 动画集指针，未找到时返回nullptr
const AnimationSet* AnimationManager::get_animation_set(const std::string& name) const {
    auto it = animation_sets_.find(name);
    if (it != animation_sets_.end()) {
        return &it->second;
    }
    return nullptr;
}

/// 创建人类角色动画集
/// 根据性别设置不同的基础索引偏移
/// @param gender 角色性别
/// @return 配置好的人类角色动画集
AnimationSet AnimationManager::create_human_animation_set(Gender gender) {
    AnimationSet set;

    // 人类角色使用Hum.wil资源文件
    // 男角色从索引0开始，女角色有偏移
    set.archive_name = "Hum";
    set.base_index = (gender == Gender::FEMALE) ? 600 : 0;

    // 传奇2人类动画布局 (基于 actor_data.h 中的 HA 常量):
    // AnimationDef: {start_frame, frame_count, skip_frames, frame_duration, loop}
    // 每个动作有8个方向的帧
    // 站立: 起始0, 每方向帧数 skip 4 = (4+4)*8=64帧
    // 行走: 起始64, 每方向帧数 skip 2 = (6+2)*8=64帧
    // 奔跑: 起始128, 每方向帧数 skip 2 = (6+2)*8=64帧
    // 攻击: 起始200, 每方向帧数 skip 2 = (6+2)*8=64帧
    // 施法: 起始392, 每方向帧数 skip 2 = (6+2)*8=64帧
    // 受击: 起始472, 每方向帧数 skip 5 = (3+5)*8=64帧
    // 死亡: 起始536, 每方向帧数 skip 4 = (4+4)*8=64帧

    set.actions[static_cast<int>(AnimationAction::STAND)] = {0, 4, 4, 0.2f, true};
    set.actions[static_cast<int>(AnimationAction::WALK)] = {64, 6, 2, 0.09f, true};
    set.actions[static_cast<int>(AnimationAction::RUN)] = {128, 6, 2, 0.12f, true};
    set.actions[static_cast<int>(AnimationAction::ATTACK)] = {200, 6, 2, 0.085f, false};
    set.actions[static_cast<int>(AnimationAction::CAST)] = {392, 6, 2, 0.06f, false};
    set.actions[static_cast<int>(AnimationAction::HIT)] = {472, 3, 5, 0.07f, false};
    set.actions[static_cast<int>(AnimationAction::DIE)] = {536, 4, 4, 0.12f, false};
    set.actions[static_cast<int>(AnimationAction::DEAD)] = {536 + 4*8 - 8, 1, 7, 1.0f, false}; // 死亡最后一帧
    set.actions[static_cast<int>(AnimationAction::PICKUP)] = {456, 2, 0, 0.3f, false}; // 坐下动作

    return set;
}

/// 创建怪物动画集
/// 根据怪物ID选择对应的Mon*.wil资源文件
/// @param monster_id 怪物类型ID(1-58)
/// @return 配置好的怪物动画集
AnimationSet AnimationManager::create_monster_animation_set(int monster_id) {
    AnimationSet set;

    // 怪物资源文件为Mon1.wil到Mon58.wil
    set.archive_name = "Mon" + std::to_string(monster_id);
    set.base_index = 0;

    // 怪物动画布局 (基于 actor_data.h 中的 MA14 骷髅类常量):
    // AnimationDef: {start_frame, frame_count, skip_frames, frame_duration, loop}
    // 站立: 起始0, 每方向帧数 skip 6 = (4+6)*8=80帧
    // 行走: 起始80, 每方向帧数 skip 4 = (6+4)*8=80帧
    // 攻击: 起始160, 每方向帧数 skip 4 = (6+4)*8=80帧
    // 受击: 起始240, 每方向帧数 skip 0 = (2+0)*8=16帧(+4填充=20)
    // 死亡: 起始260, 每方向0帧 skip 0 = (10+0)*8=80帧

    set.actions[static_cast<int>(AnimationAction::STAND)] = {0, 4, 6, 0.2f, true};
    set.actions[static_cast<int>(AnimationAction::WALK)] = {80, 6, 4, 0.16f, true};
    set.actions[static_cast<int>(AnimationAction::RUN)] = {80, 6, 4, 0.08f, true};  // 大多数怪物与行走相同
    set.actions[static_cast<int>(AnimationAction::ATTACK)] = {160, 6, 4, 0.1f, false};
    set.actions[static_cast<int>(AnimationAction::CAST)] = {160, 6, 4, 0.12f, false};  // 与攻击相同
    set.actions[static_cast<int>(AnimationAction::HIT)] = {240, 2, 0, 0.1f, false};
    set.actions[static_cast<int>(AnimationAction::DIE)] = {260, 10, 0, 0.12f, false};
    set.actions[static_cast<int>(AnimationAction::DEAD)] = {340, 10, 0, 0.1f, false}; // 白骨/尸体
    set.actions[static_cast<int>(AnimationAction::PICKUP)] = {0, 1, 0, 0.1f, false};  // 怪物不拾取物品

    return set;
}

/// 在屏幕位置渲染动画
/// @param state 要渲染的动画状态
/// @param screen_x 屏幕X坐标
/// @param screen_y 屏幕Y坐标
void AnimationManager::render(const AnimationState& state, int screen_x, int screen_y) {
    const AnimationSet* set = state.get_animation_set();
    if (!set) {
        return;  // 无动画集,跳过渲染
    }
    
    // 获取当前帧索引和对应纹理
    int frame_index = state.get_current_frame_index();
    auto texture = get_frame_texture(set->archive_name, frame_index);
    
    if (!texture) {
        return;  // 纹理加载失败,跳过渲染
    }
    
    // 获取精灵的偏移信息
    auto sprite_opt = resource_manager_.get_sprite(set->archive_name, frame_index);
    if (sprite_opt) {
        // 应用精灵偏移(精灵通常以脚部为锚点)
        renderer_.draw_sprite(*texture, screen_x, screen_y,
                             sprite_opt->offset_x, sprite_opt->offset_y);
    } else {
        renderer_.draw_texture(*texture, screen_x, screen_y);
    }
}

/// 在世界位置渲染动画
/// 将世界坐标转换为屏幕坐标后渲染
/// @param state 要渲染的动画状态
/// @param world_pos 世界坐标位置
/// @param camera 用于坐标转换的摄像机
void AnimationManager::render_at_world_pos(const AnimationState& state, 
                                           const Position& world_pos, 
                                           const Camera& camera) {
    // 将世界坐标转换为屏幕坐标
    Position screen_pos = camera.world_to_screen(world_pos);
    render(state, screen_pos.x, screen_pos.y);
}

/// 为动画集加载所需的资源文件
/// 尝试多个可能的数据目录路径
/// @param set 要加载资源的动画集
/// @return 加载成功返回true
bool AnimationManager::load_archives_for_set(const AnimationSet& set) {
    // 检查是否已加载
    if (resource_manager_.is_archive_loaded(set.archive_name)) {
        return true;
    }

    const std::string data_path = mir2::core::find_data_path(set.archive_name + ".wil");
    if (data_path.empty()) {
        return false;
    }

    const std::string path = data_path + set.archive_name + ".wil";
    return resource_manager_.load_wil_archive(path);
}

/// 清除纹理缓存
void AnimationManager::clear_cache() {
    renderer_.clear_texture_cache();
}

/// 获取精灵帧的纹理
/// 从资源管理器获取精灵数据并创建缓存纹理
/// @param archive 资源文件名
/// @param frame_index 帧索引
/// @return 纹理指针,失败时返回nullptr
std::shared_ptr<Texture> AnimationManager::get_frame_texture(const std::string& archive, int frame_index) {
    // 生成缓存键
    std::string cache_key = archive + ":" + std::to_string(frame_index);
    
    // 尝试从资源管理器获取精灵
    auto sprite_opt = resource_manager_.get_sprite(archive, frame_index);
    if (!sprite_opt) {
        return nullptr;
    }
    
    // 获取或创建纹理
    return renderer_.get_sprite_texture(cache_key, *sprite_opt, true);
}

// =============================================================================
// AnimatedEntity 实现 - 动画实体
// =============================================================================

/// 更新动画
/// @param delta_time 帧间隔时间(秒)
void AnimatedEntity::update(float delta_time) {
    animation_state_.update(delta_time);
}

/// 播放指定动作的动画
/// @param action 要播放的动作
void AnimatedEntity::play_action(AnimationAction action) {
    animation_state_.set_action(action, true);
}

// =============================================================================
// CharacterAnimator 实现 - 角色动画辅助类
// =============================================================================

/// 构造函数
/// @param animation_manager 动画管理器引用
CharacterAnimator::CharacterAnimator(AnimationManager& animation_manager)
    : animation_manager_(animation_manager)
{
}

void CharacterAnimator::setup_character(AnimatedEntity& entity, 
                                        CharacterClass char_class, 
                                        Gender gender) {
    // Create animation set name based on class and gender
    std::string set_name = "human_";
    set_name += (gender == Gender::MALE) ? "male" : "female";
    
    // Check if already registered
    const AnimationSet* existing = animation_manager_.get_animation_set(set_name);
    if (!existing) {
        // Create and register
        AnimationSet set = AnimationManager::create_human_animation_set(gender);
        animation_manager_.register_animation_set(set_name, set);
        animation_manager_.load_archives_for_set(set);
        existing = animation_manager_.get_animation_set(set_name);
    }
    
    entity.get_animation_state().set_animation_set(existing);
    entity.get_animation_state().set_action(AnimationAction::STAND);
}

void CharacterAnimator::setup_monster(AnimatedEntity& entity, int monster_id) {
    std::string set_name = "monster_" + std::to_string(monster_id);
    
    // Check if already registered
    const AnimationSet* existing = animation_manager_.get_animation_set(set_name);
    if (!existing) {
        // Create and register
        AnimationSet set = AnimationManager::create_monster_animation_set(monster_id);
        animation_manager_.register_animation_set(set_name, set);
        animation_manager_.load_archives_for_set(set);
        existing = animation_manager_.get_animation_set(set_name);
    }
    
    entity.get_animation_state().set_animation_set(existing);
    entity.get_animation_state().set_action(AnimationAction::STAND);
}

void CharacterAnimator::update_movement(AnimatedEntity& entity, 
                                        bool is_moving, 
                                        Direction direction) {
    entity.set_direction(direction);
    
    AnimationAction current = entity.get_animation_state().get_action();
    
    // Don't interrupt non-looping actions (attack, cast, hit, die)
    if (current == AnimationAction::ATTACK ||
        current == AnimationAction::CAST ||
        current == AnimationAction::HIT ||
        current == AnimationAction::DIE) {
        if (!entity.is_action_complete()) {
            return;
        }
    }
    
    // Set appropriate action
    if (is_moving) {
        if (current != AnimationAction::WALK && current != AnimationAction::RUN) {
            entity.play_action(AnimationAction::WALK);
        }
    } else {
        if (current != AnimationAction::STAND) {
            entity.play_action(AnimationAction::STAND);
        }
    }
}

void CharacterAnimator::play_attack(AnimatedEntity& entity) {
    entity.play_action(AnimationAction::ATTACK);
}

void CharacterAnimator::play_cast(AnimatedEntity& entity) {
    entity.play_action(AnimationAction::CAST);
}

void CharacterAnimator::play_hit(AnimatedEntity& entity) {
    entity.play_action(AnimationAction::HIT);
}

void CharacterAnimator::play_death(AnimatedEntity& entity) {
    entity.play_action(AnimationAction::DIE);
}

} // namespace mir2::render
