/**
 * @file attack_handler.h
 * @brief Attack handler for logic layer.
 */

#ifndef MIR2_LOGIC_HANDLERS_ATTACK_HANDLER_H_
#define MIR2_LOGIC_HANDLERS_ATTACK_HANDLER_H_

#include <cstddef>
#include <cstdint>

#include "logic/handler_context.h"
#include "logic/task.h"
#include "server/common/error_codes.h"

namespace mir2::proto {
struct AttackReq;
}  // namespace mir2::proto

namespace mir2::logic {

class CoroutineExecutor;
class ResponseSender;
class CombatService;
class RoleStore;

}  // namespace mir2::logic

namespace mir2::logic {

class AttackHandler {
 public:
  AttackHandler(CoroutineExecutor& executor,
                ResponseSender& response_sender,
                CombatService& service,
                RoleStore& role_store);

  Task<void> HandleMessage(HandlerContext ctx, const uint8_t* payload, size_t payload_size);
  Task<void> HandleHot(HandlerContext ctx, uint64_t target_id, uint16_t target_type);
  Task<void> Handle(HandlerContext ctx, uint64_t target_id);

 private:
  Task<void> SendAttackError(HandlerContext ctx, mir2::common::ErrorCode code);

  CoroutineExecutor& executor_;
  ResponseSender& response_sender_;
  CombatService& service_;
  RoleStore& role_store_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLERS_ATTACK_HANDLER_H_
