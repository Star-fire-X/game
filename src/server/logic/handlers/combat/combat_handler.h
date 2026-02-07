/**
 * @file combat_handler.h
 * @brief 战斗相关处理
 */

#ifndef LEGEND2_SERVER_HANDLERS_COMBAT_HANDLER_H
#define LEGEND2_SERVER_HANDLERS_COMBAT_HANDLER_H

#include <functional>

#include "handlers/base_handler.h"

namespace legend2::handlers {

/**
 * @brief 战斗结果
 */
struct CombatResult {
    mir2::common::ErrorCode code = mir2::common::ErrorCode::kOk;
    int damage = 0;
    int healing = 0;
    int target_hp = 0;
    bool target_dead = false;
};

/**
 * @brief 战斗服务接口
 */
class CombatService {
public:
    virtual ~CombatService() = default;
    virtual CombatResult Attack(uint64_t attacker_id, uint64_t target_id) = 0;
    virtual CombatResult UseSkill(uint64_t caster_id, uint64_t target_id, uint32_t skill_id) = 0;
};

/**
 * @brief 战斗Handler - 调用 CombatService
 */
class CombatHandler : public BaseHandler {
public:
    explicit CombatHandler(CombatService& service);

protected:
    void DoHandle(const HandlerContext& context,
                  uint16_t msg_id,
                  const std::vector<uint8_t>& payload,
                  ResponseCallback callback) override;

    void OnError(const HandlerContext& context,
                 uint16_t msg_id,
                 mir2::common::ErrorCode error_code,
                 ResponseCallback callback) override;

private:
    void HandleAttack(const HandlerContext& context,
                      const std::vector<uint8_t>& payload,
                      ResponseCallback callback);
    void HandleSkill(const HandlerContext& context,
                     const std::vector<uint8_t>& payload,
                     ResponseCallback callback);

    CombatService& service_;
};

}  // namespace legend2::handlers

#endif  // LEGEND2_SERVER_HANDLERS_COMBAT_HANDLER_H
