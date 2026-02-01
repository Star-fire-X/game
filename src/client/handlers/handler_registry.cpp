#include "client/handlers/handler_registry.h"

#include "client/handlers/character_handler.h"
#include "client/handlers/login_handler.h"
#include "client/handlers/npc_handler.h"
#include "client/handlers/system_handler.h"

#include <array>

namespace mir2::game::handlers {

namespace {
using RegisterFn = void (*)(mir2::client::INetworkManager&);

constexpr std::array<RegisterFn, 4> kRegistrars = {
    &LoginHandler::RegisterHandlers,
    &CharacterHandler::RegisterHandlers,
    &SystemHandler::RegisterHandlers,
    &NpcHandler::RegisterHandlers
};
} // namespace

void HandlerRegistry::RegisterHandlers(mir2::client::INetworkManager& manager) {
    for (const auto registrar : kRegistrars) {
        registrar(manager);
    }
}

} // namespace mir2::game::handlers
