#include "client/handlers/handler_registry.h"

#include "client/handlers/achievement_handler.h"
#include "client/handlers/auction_handler.h"
#include "client/handlers/character_handler.h"
#include "client/handlers/chat_handler.h"
#include "client/handlers/guild_handler.h"
#include "client/handlers/item_handler.h"
#include "client/handlers/login_handler.h"
#include "client/handlers/mail_handler.h"
#include "client/handlers/npc_handler.h"
#include "client/handlers/party_handler.h"
#include "client/handlers/ranking_handler.h"
#include "client/handlers/system_handler.h"
#include "client/handlers/trade_handler.h"

#include <array>

namespace mir2::game::handlers {

namespace {
using RegisterFn = void (*)(mir2::client::INetworkManager&);

constexpr std::array<RegisterFn, 13> kRegistrars = {
    &LoginHandler::RegisterHandlers,
    &CharacterHandler::RegisterHandlers,
    &SystemHandler::RegisterHandlers,
    &NpcHandler::RegisterHandlers,
    &ItemHandler::RegisterHandlers,
    &ChatHandler::RegisterHandlers,
    &GuildHandler::RegisterHandlers,
    &TradeHandler::RegisterHandlers,
    &PartyHandler::RegisterHandlers,
    &MailHandler::RegisterHandlers,
    &RankingHandler::RegisterHandlers,
    &AchievementHandler::RegisterHandlers,
    &AuctionHandler::RegisterHandlers
};
} // namespace

void HandlerRegistry::RegisterHandlers(mir2::client::INetworkManager& manager) {
    for (const auto registrar : kRegistrars) {
        registrar(manager);
    }
}

} // namespace mir2::game::handlers
