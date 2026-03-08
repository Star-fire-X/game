#ifndef MIR2_LOGIC_SERVICES_NPC_SHOP_RESPONSE_SERVICE_H_
#define MIR2_LOGIC_SERVICES_NPC_SHOP_RESPONSE_SERVICE_H_

#include <cstdint>
#include <vector>

#include "ecs/event_bus.h"
#include "logic/response_sender.h"

namespace mir2::logic {

class MerchantService;
class RoleStore;

class NpcShopResponseService {
 public:
  NpcShopResponseService(ResponseSender& response_sender,
                         ecs::EventBus& event_bus,
                         RoleStore& role_store,
                         MerchantService& merchant_service);

 private:
  ResponseSender& response_sender_;
  ecs::EventBus& event_bus_;
  RoleStore& role_store_;
  MerchantService& merchant_service_;
  ecs::EventBus::Subscription open_merchant_subscription_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_SERVICES_NPC_SHOP_RESPONSE_SERVICE_H_
