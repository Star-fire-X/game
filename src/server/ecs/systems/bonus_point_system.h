/**
 * @file bonus_point_system.h
 * @brief 奖励点数分配系统
 */

#ifndef MIR2_SERVER_ECS_SYSTEMS_BONUS_POINT_SYSTEM_H_
#define MIR2_SERVER_ECS_SYSTEMS_BONUS_POINT_SYSTEM_H_

#include <entt/entt.hpp>
#include <string>

namespace mir2::ecs {

/**
 * @brief 奖励点数分配系统
 *
 * 管理角色奖励点数的计算、分配和重置。
 */
class BonusPointSystem {
public:
    explicit BonusPointSystem(entt::registry& registry);

    /// 根据职业和等级重算可用总点数
    void RecalcAvailablePoints(entt::entity entity);

    /// 分配一个点数到指定属性
    /// @return true 分配成功, false 无剩余点数或属性名无效
    bool AllocatePoint(entt::entity entity, const std::string& attribute_name);

    /// 重置所有已分配点数
    void ResetPoints(entt::entity entity);

private:
    entt::registry& registry_;
};

}  // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_SYSTEMS_BONUS_POINT_SYSTEM_H_
