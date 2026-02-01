/**
 * @file skill_component.h
 * @brief ECS 技能组件定义
 *
 * 包含已学技能、技能列表、冷却追踪、施法状态等组件。
 */

#ifndef LEGEND2_SERVER_ECS_SKILL_COMPONENT_H
#define LEGEND2_SERVER_ECS_SKILL_COMPONENT_H

#include "common/types.h"

#include <entt/entt.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace mir2::ecs {

/**
 * @brief 已学技能实例（POD）
 *
 * 经典传奇技能进阶：模板ID、等级、经验值。
 */
struct SkillComponent {
    uint32_t skill_id = 0;  ///< 技能模板ID
    int level = 1;          ///< 技能等级
    int exp = 0;            ///< 技能经验值
};

/**
 * @brief 单个已学技能
 */
struct LearnedSkill {
    uint32_t skill_id = 0;      ///< 技能模板ID
    uint8_t level = 0;          ///< 当前等级(0-3)
    int32_t train_points = 0;   ///< 训练点
    uint8_t hotkey = 0;         ///< 快捷键(0=未绑定, 1-8=F1-F8)
};

/**
 * @brief 技能列表组件（附加到角色实体）
 */
struct SkillListComponent {
    static constexpr int MAX_SKILLS = 20;

    std::array<std::optional<LearnedSkill>, MAX_SKILLS> skills;  ///< 技能列表
    std::unordered_map<uint32_t, size_t> skill_index_;  ///< skill_id -> skills索引
    int count = 0;  ///< 已学技能数量

    /**
     * @brief 检查是否已学习指定技能
     */
    bool has_skill(uint32_t skill_id) const {
        return skill_index_.contains(skill_id);
    }

    /**
     * @brief 获取已学技能指针
     * @return 技能指针，未找到返回nullptr
     */
    LearnedSkill* get_skill(uint32_t skill_id) {
        auto it = skill_index_.find(skill_id);
        if (it == skill_index_.end()) {
            return nullptr;
        }
        const size_t index = it->second;
        if (index >= skills.size()) {
            return nullptr;
        }
        auto& slot = skills[index];
        if (!slot || slot->skill_id != skill_id) {
            return nullptr;
        }
        return &(*slot);
    }

    /**
     * @brief 获取已学技能常量指针
     */
    const LearnedSkill* get_skill(uint32_t skill_id) const {
        auto it = skill_index_.find(skill_id);
        if (it == skill_index_.end()) {
            return nullptr;
        }
        const size_t index = it->second;
        if (index >= skills.size()) {
            return nullptr;
        }
        const auto& slot = skills[index];
        if (!slot || slot->skill_id != skill_id) {
            return nullptr;
        }
        return &(*slot);
    }

    /**
     * @brief 添加技能
     * @return 成功返回true，槽位满或已存在返回false
     */
    bool add_skill(uint32_t skill_id) {
        if (has_skill(skill_id)) {
            return false;
        }
        for (size_t i = 0; i < skills.size(); ++i) {
            auto& slot = skills[i];
            if (!slot) {
                slot = LearnedSkill{skill_id, 0, 0, 0};
                skill_index_[skill_id] = i;
                ++count;
                return true;
            }
        }
        return false;  // 槽位已满
    }

    /**
     * @brief 移除技能
     * @return 成功返回true，未找到返回false
     */
    bool remove_skill(uint32_t skill_id) {
        auto it = skill_index_.find(skill_id);
        if (it == skill_index_.end()) {
            return false;
        }
        const size_t index = it->second;
        if (index >= skills.size()) {
            skill_index_.erase(it);
            return false;
        }
        auto& slot = skills[index];
        if (!slot || slot->skill_id != skill_id) {
            skill_index_.erase(it);
            return false;
        }
        slot.reset();
        skill_index_.erase(it);
        --count;
        return true;
    }
};

/**
 * @brief 技能冷却追踪组件
 */
struct SkillCooldownComponent {
    std::unordered_map<uint32_t, int64_t> cooldowns;  ///< skill_id -> end_time_ms

    /**
     * @brief 检查技能是否就绪
     */
    bool is_ready(uint32_t skill_id, int64_t now_ms) const {
        auto it = cooldowns.find(skill_id);
        if (it == cooldowns.end()) {
            return true;
        }
        return now_ms >= it->second;
    }

    /**
     * @brief 开始冷却
     */
    void start_cooldown(uint32_t skill_id, int duration_ms, int64_t now_ms) {
        cooldowns[skill_id] = now_ms + duration_ms;
    }

    /**
     * @brief 获取剩余冷却时间
     * @return 剩余毫秒数，已就绪返回0
     */
    int64_t get_remaining_ms(uint32_t skill_id, int64_t now_ms) const {
        auto it = cooldowns.find(skill_id);
        if (it == cooldowns.end()) {
            return 0;
        }
        int64_t remaining = it->second - now_ms;
        return remaining > 0 ? remaining : 0;
    }

    /**
     * @brief 清理已过期的冷却记录
     */
    void cleanup_expired(int64_t now_ms) {
        for (auto it = cooldowns.begin(); it != cooldowns.end();) {
            if (now_ms >= it->second) {
                it = cooldowns.erase(it);
            } else {
                ++it;
            }
        }
    }
};

/**
 * @brief 施法状态组件
 */
struct CastingComponent {
    bool is_casting = false;        ///< 是否正在施法
    bool can_be_interrupted = true; ///< 是否可被打断
    uint32_t skill_id = 0;          ///< 施放的技能ID
    entt::entity target_entity = entt::null;  ///< 目标实体ID（entt::null 表示无目标）
    mir2::common::Position target_pos;   ///< 目标位置
    int64_t cast_start_ms = 0;      ///< 施法开始时间
    int64_t cast_end_ms = 0;        ///< 施法结束时间

    /**
     * @brief 开始施法
     */
    void start_cast(uint32_t skill, entt::entity target, const mir2::common::Position& pos,
                    int64_t start_ms, int cast_time_ms) {
        is_casting = true;
        skill_id = skill;
        target_entity = target;
        target_pos = pos;
        cast_start_ms = start_ms;
        cast_end_ms = start_ms + cast_time_ms;
    }

    /**
     * @brief 取消施法
     */
    void cancel() {
        is_casting = false;
        skill_id = 0;
        target_entity = entt::null;
        cast_start_ms = 0;
        cast_end_ms = 0;
    }

    /**
     * @brief 检查施法是否完成
     */
    bool is_complete(int64_t now_ms) const {
        return is_casting && now_ms >= cast_end_ms;
    }

    /**
     * @brief 获取施法进度（0.0-1.0）
     */
    float get_progress(int64_t now_ms) const {
        if (!is_casting || cast_end_ms <= cast_start_ms) {
            return 0.0f;
        }
        int64_t elapsed = now_ms - cast_start_ms;
        int64_t total = cast_end_ms - cast_start_ms;
        float progress = static_cast<float>(elapsed) / static_cast<float>(total);
        return progress > 1.0f ? 1.0f : progress;
    }
};

}  // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_SKILL_COMPONENT_H
