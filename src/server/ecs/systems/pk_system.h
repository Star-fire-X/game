/**
 * @file pk_system.h
 * @brief PK系统
 */

#ifndef LEGEND2_SERVER_ECS_PK_SYSTEM_H
#define LEGEND2_SERVER_ECS_PK_SYSTEM_H

#include "ecs/components/pk_component.h"

#include <entt/entt.hpp>

namespace mir2::ecs {

class PKSystem {
public:
    explicit PKSystem(entt::registry& registry);

    /**
     * @brief 记录PK攻击
     * @param attacker 攻击者
     * @param victim 受害者
     */
    void record_pk_attack(entt::entity attacker, entt::entity victim);

    /**
     * @brief 处理PK击杀
     * @param killer 击杀者
     * @param victim 受害者
     */
    void handle_pk_kill(entt::entity killer, entt::entity victim);

    /**
     * @brief 更新PK系统（清理过期记录、更新红名状态）
     * @param current_time_ms 当前时间
     */
    void update(int64_t current_time_ms);

    /**
     * @brief 检查是否为红名玩家
     */
    bool is_red_name(entt::entity entity) const;

private:
    entt::registry& registry_;
    int64_t current_time_ms_ = 0;
};

} // namespace mir2::ecs

#endif // LEGEND2_SERVER_ECS_PK_SYSTEM_H
