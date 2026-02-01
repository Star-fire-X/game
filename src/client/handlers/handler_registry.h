/**
 * @file handler_registry.h
 * @brief Client handler registry entry point.
 */

#ifndef LEGEND2_CLIENT_HANDLERS_HANDLER_REGISTRY_H
#define LEGEND2_CLIENT_HANDLERS_HANDLER_REGISTRY_H

#include "client/network/i_network_manager.h"

namespace mir2::game::handlers {

/**
 * @brief Centralized registration for client message handlers.
 */
class HandlerRegistry {
public:
    /// Register stateless/static handlers with the network manager.
    /// Instance-bound handlers should call BindHandlers directly.
    static void RegisterHandlers(mir2::client::INetworkManager& manager);
};

} // namespace mir2::game::handlers

#endif // LEGEND2_CLIENT_HANDLERS_HANDLER_REGISTRY_H
