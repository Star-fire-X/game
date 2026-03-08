#include "logic/services/npc_shop_response_service.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/enums.h"
#include "common/protocol/npc_message_codec.h"
#include "ecs/components/character_components.h"
#include "ecs/events/npc_events.h"
#include "log/logger.h"
#include "logic/services/merchant_service.h"
#include "logic/services/session_role_store.h"

namespace mir2::logic {

namespace {

using nlohmann::json;

std::vector<uint8_t> BuildNpcShopOpenPayload(uint64_t npc_id,
                                             uint32_t store_id,
                                             const ShopConfig& shop) {
  json payload;
  payload["version"] = mir2::common::kNpcCodecVersion;
  payload["npc_id"] = npc_id;
  payload["store_id"] = store_id;
  payload["items"] = json::array();

  for (const auto& item : shop.items) {
    json item_json;
    item_json["item_id"] = item.item_id;
    item_json["price"] = std::max(item.price, 0);
    if (item.stock >= 0) {
      item_json["stock"] = std::min<int>(item.stock, std::numeric_limits<uint16_t>::max());
    }
    payload["items"].push_back(std::move(item_json));
  }

  const auto dumped = payload.dump();
  return std::vector<uint8_t>(dumped.begin(), dumped.end());
}

}  // namespace

NpcShopResponseService::NpcShopResponseService(ResponseSender& response_sender,
                                               ecs::EventBus& event_bus,
                                               RoleStore& role_store,
                                               MerchantService& merchant_service)
    : response_sender_(response_sender),
      event_bus_(event_bus),
      role_store_(role_store),
      merchant_service_(merchant_service) {
  open_merchant_subscription_ =
      event_bus_.SubscribeScoped<ecs::events::NpcOpenMerchantEvent>(
          [this](ecs::events::NpcOpenMerchantEvent& event) {
            if (event.player == entt::null || !event_bus_.Registry().valid(event.player)) {
              SYSLOG_WARN("NpcShopResponseService ignores invalid player entity");
              return;
            }
            if (event.store_id == 0) {
              SYSLOG_WARN("NpcShopResponseService ignores open event with store_id=0");
              return;
            }

            const auto* identity =
                event_bus_.Registry().try_get<ecs::CharacterIdentityComponent>(event.player);
            if (!identity || identity->id == 0) {
              SYSLOG_WARN("NpcShopResponseService missing character identity");
              return;
            }

            const auto client_id = role_store_.GetClientIdByRoleId(identity->id);
            if (!client_id.has_value()) {
              SYSLOG_DEBUG("NpcShopResponseService no client binding for role_id={}",
                           identity->id);
              return;
            }

            const auto* shop = merchant_service_.GetShop(event.store_id);
            if (!shop) {
              SYSLOG_WARN("NpcShopResponseService missing shop store_id={}", event.store_id);
              return;
            }

            response_sender_.Send(
                *client_id,
                static_cast<uint16_t>(mir2::common::MsgId::kNpcShopOpen),
                BuildNpcShopOpenPayload(event.npc_id, event.store_id, *shop));
          });
}

}  // namespace mir2::logic
