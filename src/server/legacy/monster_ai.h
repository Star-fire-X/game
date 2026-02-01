/**
 * @file monster_ai.h
 * @brief Legend2 怪物AI系统
 * 
 * 本文件包含怪物AI系统的定义，包括：
 * - 怪物AI状态机
 * - 怪物复活管理
 * - 怪物管理器
 */

#ifndef LEGEND2_MONSTER_AI_H
#define LEGEND2_MONSTER_AI_H

#include "common/types.h"
#include "monster.h"
#include <memory>
#include <vector>
#include <queue>
#include <functional>
#include <unordered_map>
#include <chrono>
#include <cmath>

namespace legend2 {

class Character;

// =============================================================================
// 怪物AI配置 (Monster AI Configuration)
// =============================================================================

/**
 * @brief 怪物AI行为配置
 */
struct MonsterAIConfig {
    // 巡逻设置
    float patrol_interval = 3.0f;       ///< 巡逻移动间隔（秒）
    int patrol_radius = 5;              ///< 最大巡逻距离（从出生点）
    
    // 追击设置
    float chase_speed_multiplier = 1.5f; ///< 追击时速度倍率
    int max_chase_distance = 15;         ///< 最大追击距离（超过则返回）
    
    // 攻击设置
    float attack_interval = 2.0f;        ///< 攻击间隔（秒）
    
    // 返回设置
    float return_speed_multiplier = 2.0f; ///< 返回时速度倍率
    
    // 空闲设置
    float idle_duration = 2.0f;          ///< 空闲持续时间（秒）
    
    // 仇恨设置
    int default_aggro_range = 5;         ///< 默认仇恨检测范围
};

// =============================================================================
// 怪物出生点配置 (Monster Spawn Configuration)
// =============================================================================

/**
 * @brief 怪物出生点配置
 */
struct MonsterSpawn {
    uint32_t monster_template_id = 0;  ///< 怪物模板ID
    mir2::common::Position spawn_position;           ///< 出生位置
    int patrol_radius = 5;             ///< 巡逻半径
    float respawn_time = 30.0f;        ///< 复活时间（秒）
    int max_count = 1;                 ///< 该出生点最大怪物数
    int aggro_range = 5;               ///< 仇恨检测范围
    int attack_range = 1;              ///< 攻击范围
};

// =============================================================================
// 复活事件 (Respawn Event)
// =============================================================================

/**
 * @brief 怪物复活调度事件
 */
struct RespawnEvent {
    uint32_t monster_id = 0;     ///< 怪物ID
    uint32_t spawn_id = 0;       ///< 出生点ID
    float respawn_time = 0.0f;   ///< 剩余复活时间
    mir2::common::Position spawn_position;     ///< 复活位置
    uint32_t map_id = 0;         ///< 复活地图ID
};

// =============================================================================
// 怪物AI类 (Monster AI Class)
// =============================================================================

/**
 * @brief 怪物AI状态机
 * 
 * 管理单个怪物的AI行为，包括：
 * - 空闲、巡逻、追击、攻击、返回、死亡状态
 * - 目标检测和追踪
 * - 攻击执行
 */
// Deprecated: use LegacyMonsterAdapter or ECS systems directly.
class [[deprecated("Use LegacyMonsterAdapter or ECS systems instead.")]] MonsterAI {
public:
    /**
     * @brief 构造函数
     * @param monster 被控制的怪物引用
     * @param config AI配置
     */
    explicit MonsterAI(Monster& monster, const MonsterAIConfig& config = MonsterAIConfig{});
    
    /**
     * @brief 更新AI状态机
     * @param delta_time 自上次更新以来的时间（秒）
     */
    void update(float delta_time);
    
    /**
     * @brief 当玩家进入检测范围时调用
     * @param player 玩家角色引用
     */
    void on_player_enter_range(Character& player);
    
    /// 当当前目标离开范围或丢失时调用
    void on_player_exit_range();
    
    /**
     * @brief 当怪物受到伤害时调用
     * @param attacker 攻击者角色引用
     */
    void on_take_damage(Character& attacker);
    
    /// 获取当前AI状态
    mir2::common::MonsterState get_state() const { return state_; }
    
    /// 获取当前目标（如果有）
    Character* get_target() const { return target_; }
    
    /// 获取出生点
    const mir2::common::Position& get_spawn_point() const { return spawn_point_; }
    
    /// 设置出生点
    void set_spawn_point(const mir2::common::Position& pos) { spawn_point_ = pos; }
    
    /// 检查怪物是否在巡逻范围内
    bool is_within_patrol_bounds() const;
    
    /// 检查怪物是否应该返回出生点
    bool should_return() const;
    
    /// 获取距离出生点的距离
    float get_distance_from_spawn() const;
    
    /// 获取到目标的距离（如果有目标）
    float get_distance_to_target() const;
    
    /// 强制状态转换（用于测试）
    void force_state(mir2::common::MonsterState new_state);
    
    /// 重置AI到初始状态
    void reset();
    
    /// 获取AI配置
    const MonsterAIConfig& get_config() const { return config_; }
    
    /// 设置AI配置
    void set_config(const MonsterAIConfig& config) { config_ = config; }
    
    
private:
    Monster& monster_;
    MonsterAIConfig config_;
    mir2::common::MonsterState state_ = mir2::common::MonsterState::IDLE;
    Character* target_ = nullptr;
    mir2::common::Position spawn_point_;
    float state_timer_ = 0.0f;
    float attack_timer_ = 0.0f;
    std::vector<mir2::common::Position> current_path_;
    size_t path_index_ = 0;
    
    
    /// Enter a new state
    void enter_state(mir2::common::MonsterState new_state);
    
    /// Exit the current state
    void exit_state();
    
    /// Update IDLE state
    void update_idle(float delta_time);
    
    /// Update PATROL state
    void update_patrol(float delta_time);
    
    /// Update CHASE state
    void update_chase(float delta_time);
    
    /// Update ATTACK state
    void update_attack(float delta_time);
    
    /// Update RETURN state
    void update_return(float delta_time);
    
    /// Update DEAD state
    void update_dead(float delta_time);
    
    /// Move towards a target position
    /// @param target Target position
    /// @param speed_multiplier Speed multiplier
    /// @return true if reached target
    bool move_towards(const mir2::common::Position& target, float speed_multiplier = 1.0f);
    
    /// Get a random patrol position within bounds
    mir2::common::Position get_random_patrol_position() const;
    
    /// Check if target is in attack range
    bool is_target_in_attack_range() const;
    
    /// Check if target is in aggro range
    bool is_target_in_aggro_range() const;
    
    /// Perform an attack on the current target
    void perform_attack();
};

// =============================================================================
// Monster Respawn Manager
// =============================================================================

/// Manages monster respawning
// Deprecated: use LegacyMonsterAdapter or ECS systems directly.
class [[deprecated("Use LegacyMonsterAdapter or ECS systems instead.")]] MonsterRespawnManager {
public:
    /// Constructor
    MonsterRespawnManager() = default;
    
    /// Update respawn timers
    /// @param delta_time Time elapsed since last update (seconds)
    void update(float delta_time);
    
    /// Schedule a monster for respawn
    /// @param monster_id ID of the monster to respawn
    /// @param spawn_id ID of the spawn point
    /// @param respawn_time Time until respawn (seconds)
    /// @param spawn_position mir2::common::Position to respawn at
    /// @param map_id Map ID to respawn on
    void schedule_respawn(uint32_t monster_id, uint32_t spawn_id, 
                          float respawn_time, const mir2::common::Position& spawn_position,
                          uint32_t map_id);
    
    /// Cancel a scheduled respawn
    /// @param monster_id ID of the monster
    /// @return true if respawn was cancelled
    bool cancel_respawn(uint32_t monster_id);
    
    /// Get pending respawn events
    const std::vector<RespawnEvent>& get_pending_respawns() const { return pending_respawns_; }
    
    /// Get ready respawn events (time <= 0)
    std::vector<RespawnEvent> get_ready_respawns();
    
    /// Clear all pending respawns
    void clear_all();
    
    /// Get number of pending respawns
    size_t get_pending_count() const { return pending_respawns_.size(); }
    
    /// Check if a monster has a pending respawn
    bool has_pending_respawn(uint32_t monster_id) const;
    
    /// Get time remaining for a monster's respawn
    /// @param monster_id ID of the monster
    /// @return Time remaining in seconds, or -1 if not found
    float get_respawn_time_remaining(uint32_t monster_id) const;
    
    /// Set callback for when a monster is ready to respawn
    using RespawnCallback = std::function<void(const RespawnEvent&)>;
    void set_respawn_callback(RespawnCallback callback) { respawn_callback_ = std::move(callback); }
    
private:
    std::vector<RespawnEvent> pending_respawns_;
    RespawnCallback respawn_callback_;
};

// =============================================================================
// Monster Manager
// =============================================================================

/// Manages all monsters and their AI on a map
// Deprecated: use LegacyMonsterAdapter or ECS systems directly.
class [[deprecated("Use LegacyMonsterAdapter or ECS systems instead.")]] MonsterManager {
public:
    /// Constructor
    /// @param config AI configuration for all monsters
    explicit MonsterManager(const MonsterAIConfig& config = MonsterAIConfig{});
    
    /// Update all monster AI and respawns
    /// @param delta_time Time elapsed since last update (seconds)
    void update(float delta_time);
    
    /// Add a monster to the manager
    /// @param monster Monster to add
    /// @param spawn_id Associated spawn point ID (0 if none)
    /// @return Pointer to the added monster's AI
    MonsterAI* add_monster(Monster monster, uint32_t spawn_id = 0);
    
    /// Remove a monster from the manager
    /// @param monster_id ID of the monster to remove
    /// @return true if monster was removed
    bool remove_monster(uint32_t monster_id);
    
    /// Get a monster by ID
    /// @param monster_id ID of the monster
    /// @return Pointer to monster, or nullptr if not found
    Monster* get_monster(uint32_t monster_id);
    
    /// Get a monster's AI by ID
    /// @param monster_id ID of the monster
    /// @return Pointer to AI, or nullptr if not found
    MonsterAI* get_monster_ai(uint32_t monster_id);
    
    /// Get all monsters
    const std::unordered_map<uint32_t, Monster>& get_monsters() const { return monsters_; }
    
    /// Get all monster AIs
    const std::unordered_map<uint32_t, std::unique_ptr<MonsterAI>>& get_monster_ais() const { return monster_ais_; }
    
    /// Register a spawn point
    /// @param spawn_id Unique spawn point ID
    /// @param spawn Spawn configuration
    void register_spawn(uint32_t spawn_id, const MonsterSpawn& spawn);
    
    /// Unregister a spawn point
    /// @param spawn_id ID of the spawn point
    void unregister_spawn(uint32_t spawn_id);
    
    /// Get spawn point by ID
    /// @param spawn_id ID of the spawn point
    /// @return Pointer to spawn, or nullptr if not found
    const MonsterSpawn* get_spawn(uint32_t spawn_id) const;
    
    /// Handle monster death
    /// @param monster_id ID of the dead monster
    void on_monster_death(uint32_t monster_id);
    
    /// Notify all monsters of a player's presence
    /// @param player Reference to the player
    void notify_player_presence(Character& player);
    
    /// Get monsters in range of a position
    /// @param pos Center position
    /// @param range Range to check
    /// @return Vector of monster IDs in range
    std::vector<uint32_t> get_monsters_in_range(const mir2::common::Position& pos, int range) const;
    
    
    /// Get the respawn manager
    MonsterRespawnManager& get_respawn_manager() { return respawn_manager_; }
    const MonsterRespawnManager& get_respawn_manager() const { return respawn_manager_; }
    
    /// Clear all monsters and spawns
    void clear();
    
    /// Get total monster count
    size_t get_monster_count() const { return monsters_.size(); }
    
    /// Get alive monster count
    size_t get_alive_monster_count() const;
    
private:
    MonsterAIConfig config_;
    std::unordered_map<uint32_t, Monster> monsters_;
    std::unordered_map<uint32_t, std::unique_ptr<MonsterAI>> monster_ais_;
    std::unordered_map<uint32_t, MonsterSpawn> spawns_;
    std::unordered_map<uint32_t, uint32_t> monster_to_spawn_;  // monster_id -> spawn_id
    MonsterRespawnManager respawn_manager_;
    uint32_t next_monster_id_ = 1;
    
    /// Spawn a monster from a spawn point
    /// @param spawn_id ID of the spawn point
    /// @return ID of the spawned monster, or 0 if failed
    uint32_t spawn_monster_from_spawn(uint32_t spawn_id);
    
    /// Handle respawn event
    void handle_respawn(const RespawnEvent& event);
};

// =============================================================================
// 工具函数 (Utility Functions)
// =============================================================================

/**
 * @brief 计算两点之间的距离
 */
inline float calculate_distance(const mir2::common::Position& a, const mir2::common::Position& b) {
    int dx = a.x - b.x;
    int dy = a.y - b.y;
    return std::sqrt(static_cast<float>(dx * dx + dy * dy));
}

/**
 * @brief 检查位置是否在中心点的半径范围内
 */
inline bool is_within_radius(const mir2::common::Position& pos, const mir2::common::Position& center, int radius) {
    int dx = pos.x - center.x;
    int dy = pos.y - center.y;
    return (dx * dx + dy * dy) <= (radius * radius);
}

} // namespace legend2

#endif // LEGEND2_MONSTER_AI_H
