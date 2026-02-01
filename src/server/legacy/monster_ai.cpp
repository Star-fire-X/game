/**
 * @file monster_ai.cpp
 * @brief Legend2 怪物AI系统实现
 * 
 * 本文件实现怪物AI系统的核心功能，包括：
 * - MonsterAI 状态机（空闲、巡逻、追击、攻击、返回、死亡）
 * - 目标检测和追踪逻辑
 * - MonsterRespawnManager 复活调度
 * - MonsterManager 怪物管理器
 */

#include "monster_ai.h"
#include "legacy/character.h"
#include "ecs/systems/combat_system.h"
#include "ecs/components/character_components.h"
#include <algorithm>
#include <random>
#include <chrono>

namespace legend2 {

// =============================================================================
// MonsterAI 类实现
// =============================================================================

/**
 * @brief 构造函数 - 初始化怪物 AI
 * @param monster 怪物对象引用
 * @param config AI 配置参数
 */
MonsterAI::MonsterAI(Monster& monster, const MonsterAIConfig& config)
    : monster_(monster)
    , config_(config)
    , state_(mir2::common::MonsterState::IDLE)
    , spawn_point_(monster.spawn_position)
{
}

/**
 * @brief 更新 AI 状态
 * @param delta_time 帧间隔时间（秒）
 * 
 * 根据当前状态调用对应的更新函数。
 * 死亡状态优先处理。
 */
void MonsterAI::update(float delta_time) {
    // Don't update if monster is dead
    if (monster_.is_dead()) {
        if (state_ != mir2::common::MonsterState::DEAD) {
            enter_state(mir2::common::MonsterState::DEAD);
        }
        update_dead(delta_time);
        return;
    }
    
    // Update based on current state
    switch (state_) {
        case mir2::common::MonsterState::IDLE:
            update_idle(delta_time);
            break;
        case mir2::common::MonsterState::PATROL:
            update_patrol(delta_time);
            break;
        case mir2::common::MonsterState::CHASE:
            update_chase(delta_time);
            break;
        case mir2::common::MonsterState::ATTACK:
            update_attack(delta_time);
            break;
        case mir2::common::MonsterState::RETURN:
            update_return(delta_time);
            break;
        case mir2::common::MonsterState::DEAD:
            update_dead(delta_time);
            break;
    }
}

/**
 * @brief 玩家进入仇恨范围事件
 * @param player 进入范围的玩家
 * 
 * 只在空闲或巡逻状态时响应，切换到追击状态。
 */
void MonsterAI::on_player_enter_range(Character& player) {
    // Only react if in IDLE or PATROL state
    if (state_ == mir2::common::MonsterState::IDLE || state_ == mir2::common::MonsterState::PATROL) {
        target_ = &player;
        enter_state(mir2::common::MonsterState::CHASE);
    }
}

/**
 * @brief 玩家离开仇恨范围事件
 * 
 * 如果正在追击或攻击，切换到返回状态。
 */
void MonsterAI::on_player_exit_range() {
    // If we were chasing or attacking, return to spawn
    if (state_ == mir2::common::MonsterState::CHASE || state_ == mir2::common::MonsterState::ATTACK) {
        target_ = nullptr;
        enter_state(mir2::common::MonsterState::RETURN);
    }
}

/**
 * @brief 受到伤害事件
 * @param attacker 攻击者
 * 
 * 如果未在战斗中，切换目标到攻击者。
 * 如果已有目标，比较距离决定是否切换。
 */
void MonsterAI::on_take_damage(Character& attacker) {
    // If not already engaged, switch to chase the attacker
    if (state_ == mir2::common::MonsterState::IDLE || state_ == mir2::common::MonsterState::PATROL || 
        state_ == mir2::common::MonsterState::RETURN) {
        target_ = &attacker;
        enter_state(mir2::common::MonsterState::CHASE);
    }
    // If already chasing/attacking someone else, consider switching targets
    else if (state_ == mir2::common::MonsterState::CHASE || state_ == mir2::common::MonsterState::ATTACK) {
        // Switch to attacker if they're closer
        if (target_ != &attacker) {
            float dist_to_current = get_distance_to_target();
            float dist_to_attacker = calculate_distance(monster_.position, attacker.get_position());
            if (dist_to_attacker < dist_to_current) {
                target_ = &attacker;
            }
        }
    }
}

/**
 * @brief 检查是否在巡逻范围内
 * @return true 如果在出生点的巡逻半径内
 */
bool MonsterAI::is_within_patrol_bounds() const {
    return is_within_radius(monster_.position, spawn_point_, config_.patrol_radius);
}

/**
 * @brief 检查是否应该返回出生点
 * @return true 如果超出最大追击距离
 */
bool MonsterAI::should_return() const {
    float dist = get_distance_from_spawn();
    return dist > static_cast<float>(config_.max_chase_distance);
}

/**
 * @brief 获取到出生点的距离
 * @return 距离值
 */
float MonsterAI::get_distance_from_spawn() const {
    return calculate_distance(monster_.position, spawn_point_);
}

/**
 * @brief 获取到目标的距离
 * @return 距离值，无目标返回 -1
 */
float MonsterAI::get_distance_to_target() const {
    if (!target_) return -1.0f;
    return calculate_distance(monster_.position, target_->get_position());
}

/**
 * @brief 强制切换状态
 * @param new_state 新状态
 */
void MonsterAI::force_state(mir2::common::MonsterState new_state) {
    enter_state(new_state);
}

/**
 * @brief 重置 AI 状态
 * 
 * 清除目标、路径，将怪物移回出生点。
 */
void MonsterAI::reset() {
    target_ = nullptr;
    state_timer_ = 0.0f;
    attack_timer_ = 0.0f;
    current_path_.clear();
    path_index_ = 0;
    monster_.position = spawn_point_;
    monster_.state = mir2::common::MonsterState::IDLE;
    state_ = mir2::common::MonsterState::IDLE;
}

// -----------------------------------------------------------------------------
// 状态机实现
// -----------------------------------------------------------------------------

/**
 * @brief 进入新状态
 * @param new_state 新状态
 * 
 * 执行状态退出逻辑，重置计时器，执行状态进入逻辑。
 */
void MonsterAI::enter_state(mir2::common::MonsterState new_state) {
    exit_state();
    state_ = new_state;
    monster_.state = new_state;
    state_timer_ = 0.0f;
    
    switch (new_state) {
        case mir2::common::MonsterState::IDLE:
            // Reset path
            current_path_.clear();
            path_index_ = 0;
            break;
        case mir2::common::MonsterState::PATROL:
            // Get a new patrol destination
            current_path_.clear();
            path_index_ = 0;
            break;
        case mir2::common::MonsterState::CHASE:
            attack_timer_ = 0.0f;
            break;
        case mir2::common::MonsterState::ATTACK:
            attack_timer_ = 0.0f;
            break;
        case mir2::common::MonsterState::RETURN:
            target_ = nullptr;
            current_path_.clear();
            path_index_ = 0;
            break;
        case mir2::common::MonsterState::DEAD:
            target_ = nullptr;
            break;
    }
}

/**
 * @brief 退出当前状态
 * 
 * 执行状态清理逻辑（当前为空）。
 */
void MonsterAI::exit_state() {
    // Cleanup for current state if needed
}

/**
 * @brief 空闲状态更新
 * @param delta_time 帧间隔时间
 * 
 * 检测附近玩家，超时后切换到巡逻状态。
 */
void MonsterAI::update_idle(float delta_time) {
    state_timer_ += delta_time;
    
    // Check for nearby players (if we have a target set externally)
    if (target_ && target_->is_alive()) {
        float dist = get_distance_to_target();
        if (dist <= static_cast<float>(monster_.aggro_range)) {
            enter_state(mir2::common::MonsterState::CHASE);
            return;
        }
    }
    
    // After idle duration, start patrolling
    if (state_timer_ >= config_.idle_duration) {
        enter_state(mir2::common::MonsterState::PATROL);
    }
}

/**
 * @brief 巡逻状态更新
 * @param delta_time 帧间隔时间
 * 
 * 在巡逻范围内随机移动，检测附近玩家。
 */
void MonsterAI::update_patrol(float delta_time) {
    state_timer_ += delta_time;
    
    // Check for nearby players
    if (target_ && target_->is_alive()) {
        float dist = get_distance_to_target();
        if (dist <= static_cast<float>(monster_.aggro_range)) {
            enter_state(mir2::common::MonsterState::CHASE);
            return;
        }
    }
    
    // Move towards patrol destination
    if (current_path_.empty() || path_index_ >= current_path_.size()) {
        // Get new patrol destination
        if (state_timer_ >= config_.patrol_interval) {
            mir2::common::Position patrol_dest = get_random_patrol_position();
            
            // Simple direct path (no pathfinding in common AI)
            current_path_ = {patrol_dest};
            path_index_ = 0;
            state_timer_ = 0.0f;
        }
    } else {
        // Move along path
        if (move_towards(current_path_[path_index_], 1.0f)) {
            path_index_++;
            if (path_index_ >= current_path_.size()) {
                // Reached destination, go idle
                enter_state(mir2::common::MonsterState::IDLE);
            }
        }
    }
}

/**
 * @brief 追击状态更新
 * @param delta_time 帧间隔时间
 * 
 * 追踪目标，进入攻击范围后切换到攻击状态。
 * 超出追击距离或目标死亡则返回。
 */
void MonsterAI::update_chase(float delta_time) {
    (void)delta_time;

    // Check if target is still valid
    if (!target_ || target_->is_dead()) {
        enter_state(mir2::common::MonsterState::RETURN);
        return;
    }
    
    // Check if we've chased too far
    if (should_return()) {
        enter_state(mir2::common::MonsterState::RETURN);
        return;
    }
    
    // Check if target is in attack range
    if (is_target_in_attack_range()) {
        enter_state(mir2::common::MonsterState::ATTACK);
        return;
    }
    
    // Move towards target
    move_towards(target_->get_position(), config_.chase_speed_multiplier);
}

/**
 * @brief 攻击状态更新
 * @param delta_time 帧间隔时间
 * 
 * 按攻击间隔执行攻击，目标离开攻击范围则追击或返回。
 */
void MonsterAI::update_attack(float delta_time) {
    attack_timer_ += delta_time;
    
    // Check if target is still valid
    if (!target_ || target_->is_dead()) {
        enter_state(mir2::common::MonsterState::RETURN);
        return;
    }
    
    // Check if target moved out of attack range
    if (!is_target_in_attack_range()) {
        // Check if still in aggro range
        if (is_target_in_aggro_range() && !should_return()) {
            enter_state(mir2::common::MonsterState::CHASE);
        } else {
            enter_state(mir2::common::MonsterState::RETURN);
        }
        return;
    }
    
    // Perform attack at intervals
    if (attack_timer_ >= config_.attack_interval) {
        perform_attack();
        attack_timer_ = 0.0f;
    }
}

/**
 * @brief 返回状态更新
 * @param delta_time 帧间隔时间
 * 
 * 移动回出生点，到达后切换到空闲状态。
 */
void MonsterAI::update_return(float delta_time) {
    (void)delta_time;

    // Check if we've reached spawn point
    float dist = get_distance_from_spawn();
    if (dist <= 1.0f) {
        monster_.position = spawn_point_;
        enter_state(mir2::common::MonsterState::IDLE);
        return;
    }
    
    // Move towards spawn point
    move_towards(spawn_point_, config_.return_speed_multiplier);
}

/**
 * @brief 死亡状态更新
 * @param delta_time 帧间隔时间
 * 
 * 死亡状态不执行任何操作，复活由 RespawnManager 处理。
 */
void MonsterAI::update_dead(float delta_time) {
    (void)delta_time;

    // Dead state - do nothing, respawn is handled by RespawnManager
}

// -----------------------------------------------------------------------------
// 移动和战斗辅助方法
// -----------------------------------------------------------------------------

/**
 * @brief 向目标位置移动
 * @param target 目标位置
 * @param speed_multiplier 速度倍率
 * @return true 如果已到达目标
 * 
 * 每次移动一格，支持 8 方向移动。
 * 如果有地图系统，会检查可行走性。
 */
bool MonsterAI::move_towards(const mir2::common::Position& target, float speed_multiplier) {
    (void)speed_multiplier;

    if (monster_.position == target) {
        return true;
    }
    
    // Calculate direction
    int dx = target.x - monster_.position.x;
    int dy = target.y - monster_.position.y;
    
    // Normalize to single step (simple movement)
    int step_x = (dx != 0) ? (dx > 0 ? 1 : -1) : 0;
    int step_y = (dy != 0) ? (dy > 0 ? 1 : -1) : 0;
    
    // Apply movement
    mir2::common::Position new_pos = {
        monster_.position.x + step_x,
        monster_.position.y + step_y
    };
    
    monster_.position = new_pos;
    return monster_.position == target;
}

/**
 * @brief 获取随机巡逻位置
 * @return 巡逻范围内的随机位置
 */
mir2::common::Position MonsterAI::get_random_patrol_position() const {
    static std::mt19937 rng(static_cast<unsigned int>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    
    std::uniform_int_distribution<int> dist(-config_.patrol_radius, config_.patrol_radius);
    
    mir2::common::Position patrol_pos;
    patrol_pos.x = spawn_point_.x + dist(rng);
    patrol_pos.y = spawn_point_.y + dist(rng);
    
    // Ensure within patrol bounds
    if (!is_within_radius(patrol_pos, spawn_point_, config_.patrol_radius)) {
        // Clamp to patrol radius
        int dx = patrol_pos.x - spawn_point_.x;
        int dy = patrol_pos.y - spawn_point_.y;
        float dist_val = std::sqrt(static_cast<float>(dx * dx + dy * dy));
        if (dist_val > 0) {
            float scale = static_cast<float>(config_.patrol_radius) / dist_val;
            patrol_pos.x = spawn_point_.x + static_cast<int>(dx * scale);
            patrol_pos.y = spawn_point_.y + static_cast<int>(dy * scale);
        }
    }
    
    return patrol_pos;
}

/**
 * @brief 检查目标是否在攻击范围内
 * @return true 如果在攻击范围内
 */
bool MonsterAI::is_target_in_attack_range() const {
    if (!target_) return false;
    float dist = get_distance_to_target();
    return dist <= static_cast<float>(monster_.attack_range);
}

/**
 * @brief 检查目标是否在仇恨范围内
 * @return true 如果在仇恨范围内
 */
bool MonsterAI::is_target_in_aggro_range() const {
    if (!target_) return false;
    float dist = get_distance_to_target();
    return dist <= static_cast<float>(monster_.aggro_range);
}

/**
 * @brief 执行攻击
 * 
 * 使用战斗系统对目标造成伤害。
 */
void MonsterAI::perform_attack() {
    if (!target_) return;

    // TODO: integrate MonsterAdapter to map Monster to ECS registry/entity for CombatSystem.
}


// =============================================================================
// MonsterRespawnManager 类实现
// =============================================================================

/**
 * @brief 更新复活管理器
 * @param delta_time 帧间隔时间
 * 
 * 更新所有复活计时器，触发已就绪的复活事件。
 */
void MonsterRespawnManager::update(float delta_time) {
    // Update all respawn timers
    for (auto& event : pending_respawns_) {
        event.respawn_time -= delta_time;
    }
    
    // Check for ready respawns and trigger callbacks
    auto it = pending_respawns_.begin();
    while (it != pending_respawns_.end()) {
        if (it->respawn_time <= 0.0f) {
            if (respawn_callback_) {
                respawn_callback_(*it);
            }
            it = pending_respawns_.erase(it);
        } else {
            ++it;
        }
    }
}

/**
 * @brief 调度怪物复活
 * @param monster_id 怪物 ID
 * @param spawn_id 出生点 ID
 * @param respawn_time 复活时间（秒）
 * @param spawn_position 复活位置
 * @param map_id 地图 ID
 * 
 * 如果已有相同怪物的复活事件，更新其参数。
 */
void MonsterRespawnManager::schedule_respawn(uint32_t monster_id, uint32_t spawn_id,
                                              float respawn_time, const mir2::common::Position& spawn_position,
                                              uint32_t map_id) {
    // Check if already scheduled
    for (auto& event : pending_respawns_) {
        if (event.monster_id == monster_id) {
            // Update existing entry
            event.spawn_id = spawn_id;
            event.respawn_time = respawn_time;
            event.spawn_position = spawn_position;
            event.map_id = map_id;
            return;
        }
    }
    
    // Add new respawn event
    RespawnEvent event;
    event.monster_id = monster_id;
    event.spawn_id = spawn_id;
    event.respawn_time = respawn_time;
    event.spawn_position = spawn_position;
    event.map_id = map_id;
    pending_respawns_.push_back(event);
}

/**
 * @brief 取消复活调度
 * @param monster_id 怪物 ID
 * @return true 如果成功取消
 */
bool MonsterRespawnManager::cancel_respawn(uint32_t monster_id) {
    auto it = std::find_if(pending_respawns_.begin(), pending_respawns_.end(),
        [monster_id](const RespawnEvent& e) { return e.monster_id == monster_id; });
    
    if (it != pending_respawns_.end()) {
        pending_respawns_.erase(it);
        return true;
    }
    return false;
}

/**
 * @brief 获取并移除已就绪的复活事件
 * @return 就绪的复活事件列表
 */
std::vector<RespawnEvent> MonsterRespawnManager::get_ready_respawns() {
    std::vector<RespawnEvent> ready;
    
    auto it = pending_respawns_.begin();
    while (it != pending_respawns_.end()) {
        if (it->respawn_time <= 0.0f) {
            ready.push_back(*it);
            it = pending_respawns_.erase(it);
        } else {
            ++it;
        }
    }
    
    return ready;
}

/**
 * @brief 清除所有复活调度
 */
void MonsterRespawnManager::clear_all() {
    pending_respawns_.clear();
}

/**
 * @brief 检查是否有待处理的复活
 * @param monster_id 怪物 ID
 * @return true 如果有待处理的复活
 */
bool MonsterRespawnManager::has_pending_respawn(uint32_t monster_id) const {
    return std::any_of(pending_respawns_.begin(), pending_respawns_.end(),
        [monster_id](const RespawnEvent& e) { return e.monster_id == monster_id; });
}

/**
 * @brief 获取剩余复活时间
 * @param monster_id 怪物 ID
 * @return 剩余时间（秒），无复活返回 -1
 */
float MonsterRespawnManager::get_respawn_time_remaining(uint32_t monster_id) const {
    auto it = std::find_if(pending_respawns_.begin(), pending_respawns_.end(),
        [monster_id](const RespawnEvent& e) { return e.monster_id == monster_id; });
    
    if (it != pending_respawns_.end()) {
        return it->respawn_time;
    }
    return -1.0f;
}

// =============================================================================
// MonsterManager 类实现
// =============================================================================

/**
 * @brief 构造函数 - 初始化怪物管理器
 * @param config AI 配置参数
 */
MonsterManager::MonsterManager(const MonsterAIConfig& config)
    : config_(config)
{
    // Set up respawn callback
    respawn_manager_.set_respawn_callback([this](const RespawnEvent& event) {
        handle_respawn(event);
    });
}

/**
 * @brief 更新所有怪物
 * @param delta_time 帧间隔时间
 * 
 * 更新复活管理器和所有怪物 AI。
 */
void MonsterManager::update(float delta_time) {
    // Update respawn manager
    respawn_manager_.update(delta_time);
    
    // Update all monster AIs
    for (auto& [id, ai] : monster_ais_) {
        if (ai) {
            ai->update(delta_time);
        }
    }
}

/**
 * @brief 添加怪物
 * @param monster 怪物对象
 * @param spawn_id 关联的出生点 ID
 * @return MonsterAI* 怪物 AI 指针
 * 
 * 自动分配 ID（如果为 0），创建 AI 并关联出生点。
 */
MonsterAI* MonsterManager::add_monster(Monster monster, uint32_t spawn_id) {
    uint32_t id = monster.id;
    if (id == 0) {
        id = next_monster_id_++;
        monster.id = id;
    }
    
    // Store monster
    monsters_[id] = std::move(monster);
    
    // Create AI for monster
    auto ai = std::make_unique<MonsterAI>(monsters_[id], config_);
    MonsterAI* ai_ptr = ai.get();
    monster_ais_[id] = std::move(ai);
    
    // Track spawn association
    if (spawn_id != 0) {
        monster_to_spawn_[id] = spawn_id;
    }
    
    return ai_ptr;
}

/**
 * @brief 移除怪物
 * @param monster_id 怪物 ID
 * @return true 如果成功移除
 */
bool MonsterManager::remove_monster(uint32_t monster_id) {
    auto it = monsters_.find(monster_id);
    if (it == monsters_.end()) {
        return false;
    }
    
    monsters_.erase(it);
    monster_ais_.erase(monster_id);
    monster_to_spawn_.erase(monster_id);
    
    return true;
}

/**
 * @brief 获取怪物指针
 * @param monster_id 怪物 ID
 * @return Monster* 怪物指针，不存在返回 nullptr
 */
Monster* MonsterManager::get_monster(uint32_t monster_id) {
    auto it = monsters_.find(monster_id);
    if (it != monsters_.end()) {
        return &it->second;
    }
    return nullptr;
}

/**
 * @brief 获取怪物 AI 指针
 * @param monster_id 怪物 ID
 * @return MonsterAI* AI 指针，不存在返回 nullptr
 */
MonsterAI* MonsterManager::get_monster_ai(uint32_t monster_id) {
    auto it = monster_ais_.find(monster_id);
    if (it != monster_ais_.end()) {
        return it->second.get();
    }
    return nullptr;
}

/**
 * @brief 注册出生点
 * @param spawn_id 出生点 ID
 * @param spawn 出生点配置
 */
void MonsterManager::register_spawn(uint32_t spawn_id, const MonsterSpawn& spawn) {
    spawns_[spawn_id] = spawn;
}

/**
 * @brief 注销出生点
 * @param spawn_id 出生点 ID
 */
void MonsterManager::unregister_spawn(uint32_t spawn_id) {
    spawns_.erase(spawn_id);
}

/**
 * @brief 获取出生点配置
 * @param spawn_id 出生点 ID
 * @return const MonsterSpawn* 出生点指针，不存在返回 nullptr
 */
const MonsterSpawn* MonsterManager::get_spawn(uint32_t spawn_id) const {
    auto it = spawns_.find(spawn_id);
    if (it != spawns_.end()) {
        return &it->second;
    }
    return nullptr;
}

/**
 * @brief 怪物死亡事件处理
 * @param monster_id 死亡的怪物 ID
 * 
 * 查找关联的出生点，调度复活。
 */
void MonsterManager::on_monster_death(uint32_t monster_id) {
    auto monster = get_monster(monster_id);
    if (!monster) return;
    
    // Find associated spawn
    auto spawn_it = monster_to_spawn_.find(monster_id);
    if (spawn_it != monster_to_spawn_.end()) {
        uint32_t spawn_id = spawn_it->second;
        auto spawn = get_spawn(spawn_id);
        if (spawn) {
            // Schedule respawn
            respawn_manager_.schedule_respawn(
                monster_id,
                spawn_id,
                spawn->respawn_time,
                spawn->spawn_position,
                monster->map_id
            );
        }
    }
}

/**
 * @brief 通知玩家存在
 * @param player 玩家角色
 * 
 * 检查所有怪物，如果玩家在仇恨范围内则触发追击。
 */
void MonsterManager::notify_player_presence(Character& player) {
    for (auto& [id, ai] : monster_ais_) {
        if (!ai) continue;
        
        auto monster = get_monster(id);
        if (!monster || monster->is_dead()) continue;
        
        // Check if player is in aggro range
        float dist = calculate_distance(monster->position, player.get_position());
        if (dist <= static_cast<float>(monster->aggro_range)) {
            ai->on_player_enter_range(player);
        }
    }
}

/**
 * @brief 获取范围内的怪物
 * @param pos 中心位置
 * @param range 搜索范围
 * @return 范围内的怪物 ID 列表
 */
std::vector<uint32_t> MonsterManager::get_monsters_in_range(const mir2::common::Position& pos, int range) const {
    std::vector<uint32_t> result;
    
    for (const auto& [id, monster] : monsters_) {
        if (is_within_radius(monster.position, pos, range)) {
            result.push_back(id);
        }
    }
    
    return result;
}

/**
 * @brief 清除所有怪物和出生点
 */
void MonsterManager::clear() {
    monsters_.clear();
    monster_ais_.clear();
    spawns_.clear();
    monster_to_spawn_.clear();
    respawn_manager_.clear_all();
}

size_t MonsterManager::get_alive_monster_count() const {
    return std::count_if(monsters_.begin(), monsters_.end(),
        [](const auto& pair) { return pair.second.is_alive(); });
}

uint32_t MonsterManager::spawn_monster_from_spawn(uint32_t spawn_id) {
    auto spawn = get_spawn(spawn_id);
    if (!spawn) return 0;
    
    // Create monster from spawn template
    Monster monster;
    monster.id = next_monster_id_++;
    monster.template_id = spawn->monster_template_id;
    monster.position = spawn->spawn_position;
    monster.spawn_position = spawn->spawn_position;
    monster.aggro_range = spawn->aggro_range;
    monster.attack_range = spawn->attack_range;
    monster.respawn_time = spawn->respawn_time;
    monster.state = mir2::common::MonsterState::IDLE;
    
    // Default stats (should be loaded from template in real implementation)
    monster.stats.hp = 100;
    monster.stats.max_hp = 100;
    monster.stats.attack = 10;
    monster.stats.defense = 5;
    
    add_monster(std::move(monster), spawn_id);
    
    return monster.id;
}

void MonsterManager::handle_respawn(const RespawnEvent& event) {
    // Get spawn info
    auto spawn = get_spawn(event.spawn_id);
    if (!spawn) return;
    
    // Check if monster still exists (might have been removed)
    auto monster = get_monster(event.monster_id);
    if (monster) {
        // Reset existing monster
        monster->stats.hp = monster->stats.max_hp;
        monster->position = event.spawn_position;
        monster->spawn_position = event.spawn_position;
        monster->state = mir2::common::MonsterState::IDLE;
        
        // Reset AI
        auto ai = get_monster_ai(event.monster_id);
        if (ai) {
            ai->reset();
        }
    } else {
        // Spawn new monster
        spawn_monster_from_spawn(event.spawn_id);
    }
}

} // namespace legend2
