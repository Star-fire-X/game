/**
 * @file character_handler.h
 * @brief 角色相关处理
 */

#ifndef LEGEND2_SERVER_HANDLERS_CHARACTER_HANDLER_H
#define LEGEND2_SERVER_HANDLERS_CHARACTER_HANDLER_H

#include "ecs/character_entity_manager.h"

#include "handlers/base_handler.h"
#include "world/role_store.h"

namespace legend2::handlers {

/**
 * @brief 角色Handler
 */
class CharacterHandler : public BaseHandler {
public:
    CharacterHandler(mir2::ecs::CharacterEntityManager& entity_manager,
                     mir2::world::RoleStore& role_store);

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
    void HandleRoleList(const HandlerContext& context,
                        const std::vector<uint8_t>& payload,
                        ResponseCallback callback);
    void HandleCreateRole(const HandlerContext& context,
                          const std::vector<uint8_t>& payload,
                          ResponseCallback callback);
    void HandleSelectRole(const HandlerContext& context,
                          const std::vector<uint8_t>& payload,
                          ResponseCallback callback);
    void HandleLogout(const HandlerContext& context,
                      const std::vector<uint8_t>& payload,
                      ResponseCallback callback);

    mir2::ecs::CharacterEntityManager& entity_manager_;
    mir2::world::RoleStore& role_store_;
};

}  // namespace legend2::handlers

#endif  // LEGEND2_SERVER_HANDLERS_CHARACTER_HANDLER_H
