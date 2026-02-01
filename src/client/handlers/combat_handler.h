/**
 * @file combat_handler.h
 * @brief Client combat and pickup message handlers.
 */

#ifndef LEGEND2_CLIENT_HANDLERS_COMBAT_HANDLER_H
#define LEGEND2_CLIENT_HANDLERS_COMBAT_HANDLER_H

#include "client/network/i_network_manager.h"
#include "common/protocol/skill_result.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace mir2::proto {
enum class ErrorCode : uint16_t;
}

namespace mir2::game::handlers {

/**
 * @brief Handles combat and item pickup responses.
 *
 * Responsibilities:
 * - Register combat handlers with NetworkManager.
 * - Parse FlatBuffers payloads and forward results to callbacks.
 *
 * Threading: expected to be used on the main thread; handlers are invoked from
 * NetworkManager::update() context.
 *
 * Ownership: stores callbacks by value and does not own NetworkManager.
 * Caller manages handler lifetime and should keep it alive while handlers are bound.
 */
class CombatHandler : public std::enable_shared_from_this<CombatHandler> {
public:
    using NetworkPacket = mir2::common::NetworkPacket;

    /**
     * @brief Callback hooks for combat flow.
     *
     * Ownership: stored by value. If owner is provided, callbacks are invoked only
     * while the owner is still alive; otherwise the caller must ensure captured
     * objects outlive the handler.
     */
    struct Callbacks {
        // Optional lifetime guard for objects captured by callbacks.
        std::optional<std::weak_ptr<void>> owner;
        std::function<void(mir2::proto::ErrorCode code,
                           uint64_t attacker_id,
                           uint64_t target_id,
                           int damage,
                           int target_hp,
                           bool target_dead)> on_attack_response;
        std::function<void(mir2::proto::ErrorCode code,
                           uint32_t skill_id,
                           uint64_t caster_id,
                           uint64_t target_id,
                           mir2::common::SkillResult result,
                           int damage,
                           int healing,
                           bool target_dead)> on_skill_result;
        std::function<void(uint32_t skill_id, uint32_t cooldown_ms)> on_skill_cooldown;
        std::function<void(mir2::proto::ErrorCode code, uint32_t item_id)> on_pickup_item_response;
        std::function<void(const std::string& error)> on_parse_error;
    };

    explicit CombatHandler(Callbacks callbacks);
    ~CombatHandler() = default;

    // Bind handlers with lifetime safety. Requires shared_ptr ownership.
    void BindHandlers(mir2::client::INetworkManager& manager);
    static void RegisterHandlers(mir2::client::INetworkManager& manager);

    void HandleAttackResponse(const NetworkPacket& packet);
    void HandleSkillResponse(const NetworkPacket& packet);
    void HandlePickupItemResponse(const NetworkPacket& packet);

private:
    Callbacks callbacks_;
};

} // namespace mir2::game::handlers

#endif // LEGEND2_CLIENT_HANDLERS_COMBAT_HANDLER_H
