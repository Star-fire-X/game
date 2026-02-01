/**
 * @file pk_component.h
 * @brief PK系统相关组件定义
 */

#ifndef LEGEND2_SERVER_ECS_PK_COMPONENT_H
#define LEGEND2_SERVER_ECS_PK_COMPONENT_H

#include <cstdint>
#include <vector>

#include <entt/entt.hpp>

namespace mir2::ecs {

/**
 * @brief PK攻击记录
 */
struct PKHitRecord {
    entt::entity attacker = entt::null;
    int64_t hit_time_ms = 0;
};

/**
 * @brief PK组件（附加在玩家上）
 */
struct PKComponent {
    int pk_points = 0;                      ///< PK值（PlayerKillingPoint）
    std::vector<PKHitRecord> pk_hiter_list; ///< 攻击者列表（PKHiterList）
    int64_t last_pk_time_ms = 0;            ///< 最后一次PK时间
    bool is_red_name = false;               ///< 是否红名

    /**
     * @brief 添加攻击记录
     */
    void add_hiter(entt::entity attacker, int64_t current_time_ms) {
        // 移除旧记录（超过5分钟）
        const int64_t expire_time = current_time_ms - 300000; // 5分钟
        pk_hiter_list.erase(
            std::remove_if(
                pk_hiter_list.begin(),
                pk_hiter_list.end(),
                [expire_time](const PKHitRecord& record) {
                    return record.hit_time_ms < expire_time;
                }),
            pk_hiter_list.end());

        // 添加新记录
        PKHitRecord record;
        record.attacker = attacker;
        record.hit_time_ms = current_time_ms;
        pk_hiter_list.push_back(record);
    }

    /**
     * @brief 清理过期的攻击记录
     */
    void cleanup_expired_hiters(int64_t current_time_ms) {
        const int64_t expire_time = current_time_ms - 300000; // 5分钟
        pk_hiter_list.erase(
            std::remove_if(
                pk_hiter_list.begin(),
                pk_hiter_list.end(),
                [expire_time](const PKHitRecord& record) {
                    return record.hit_time_ms < expire_time;
                }),
            pk_hiter_list.end());
    }

    /**
     * @brief 更新红名状态
     */
    void update_red_name_status() {
        // PK值大于100为红名
        is_red_name = (pk_points > 100);
    }
};

} // namespace mir2::ecs

#endif // LEGEND2_SERVER_ECS_PK_COMPONENT_H
