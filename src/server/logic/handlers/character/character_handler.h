/**
 * @file character_handler.h
 * @brief Character handler for logic layer.
 */

#ifndef MIR2_LOGIC_HANDLERS_CHARACTER_CHARACTER_HANDLER_H_
#define MIR2_LOGIC_HANDLERS_CHARACTER_CHARACTER_HANDLER_H_

#include <cstddef>
#include <cstdint>

#include "logic/handler_context.h"
#include "logic/task.h"

namespace mir2::ecs {
class CharacterEntityManager;
}

namespace mir2::proto {
class RoleListReq;
class CreateRoleReq;
class SelectRoleReq;
}

namespace mir2::logic {

class ResponseSender;
class RoleStore;
class ClientRegistry;

class CharacterHandler {
 public:
  CharacterHandler(ResponseSender& response_sender,
                   mir2::ecs::CharacterEntityManager& entity_manager,
                   RoleStore& role_store,
                   ClientRegistry& client_registry);

  Task<void> HandleMessage(HandlerContext ctx, const uint8_t* payload, size_t payload_size);

 private:
  Task<void> HandleRoleList(HandlerContext ctx, const mir2::proto::RoleListReq* req);
  Task<void> HandleCreateRole(HandlerContext ctx, const mir2::proto::CreateRoleReq* req);
  Task<void> HandleSelectRole(HandlerContext ctx, const mir2::proto::SelectRoleReq* req);
  Task<void> HandleLogout(HandlerContext ctx);

  ResponseSender& response_sender_;
  mir2::ecs::CharacterEntityManager& entity_manager_;
  RoleStore& role_store_;
  ClientRegistry& client_registry_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLERS_CHARACTER_CHARACTER_HANDLER_H_
