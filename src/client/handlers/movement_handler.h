/**
 * @file movement_handler.h
 * @brief Client movement and entity message handlers.
 */

#ifndef LEGEND2_CLIENT_HANDLERS_MOVEMENT_HANDLER_H
#define LEGEND2_CLIENT_HANDLERS_MOVEMENT_HANDLER_H

#include "client/network/i_network_manager.h"

#include <cstdint>
#include <functional>
#include <string>

namespace mir2::proto {
enum class ErrorCode : uint16_t;
enum class EntityType : uint8_t;
}

namespace mir2::game::events {

struct EntityMovedEvent {
    uint64_t entity_id;
    mir2::proto::EntityType entity_type;
    int x;
    int y;
    uint8_t direction;
};

struct EntityEnteredEvent {
    uint64_t entity_id;
    mir2::proto::EntityType entity_type;
    int x;
    int y;
    uint8_t direction;
};

struct EntityLeftEvent {
    uint64_t entity_id;
};

struct EntityStatsUpdatedEvent {
    uint64_t entity_id;
    mir2::proto::EntityType entity_type;
    int hp;
    int max_hp;
    int mp;
    int max_mp;
    uint16_t level;
};

} // namespace mir2::game::events

namespace mir2::game::handlers {

/**
 * @brief Handles movement and entity presence messages.
 *
 * Responsibilities:
 * - Register movement handlers with NetworkManager.
 * - Parse FlatBuffers payloads and forward results to callbacks.
 *
 * Threading: expected to be used on the main thread; handlers are invoked from
 * NetworkManager::update() context.
 *
 * Ownership: stores callbacks by value and does not own NetworkManager. Caller
 * is responsible for binding handlers on the active instance.
 */
class MovementHandler {
public:
    using NetworkPacket = mir2::common::NetworkPacket;

    /**
     * @brief Callback hooks for movement flow.
     *
     * Ownership: stored by value; captured objects referenced by callbacks must
     * outlive the handler.
     */
    struct Callbacks {
        std::function<void(int x, int y)> on_move_response;
        std::function<void(const std::string& error)> on_move_failed;
        std::function<void(const events::EntityMovedEvent&)> on_entity_move;
        std::function<void(const events::EntityEnteredEvent&)> on_entity_enter;
        std::function<void(const events::EntityLeftEvent&)> on_entity_leave;
        std::function<void(const events::EntityStatsUpdatedEvent&)> on_entity_update;
        std::function<void(uint64_t id, uint32_t template_id, int x, int y, uint8_t dir, int hp, int max_hp)>
            on_monster_enter;
        std::function<void(uint64_t id)> on_monster_leave;
        std::function<void(uint64_t id, int x, int y, uint8_t dir)> on_monster_move;
        std::function<void(uint64_t id, int hp, int max_hp)> on_monster_stats;
        std::function<void(uint64_t id, uint64_t killer_id)> on_monster_death;
        std::function<void(const std::string& error)> on_parse_error;
    };

    explicit MovementHandler(Callbacks callbacks);
    ~MovementHandler();

    void BindHandlers(mir2::client::INetworkManager& manager);

    void HandleMoveResponse(const NetworkPacket& packet);
    void HandleMoveBroadcast(const NetworkPacket& packet);
    void HandleEntitySpawn(const NetworkPacket& packet);
    void HandleEntityDespawn(const NetworkPacket& packet);
    void HandleEntityUpdate(const NetworkPacket& packet);
    void HandleMonsterDeath(const NetworkPacket& packet);

private:
    Callbacks callbacks_;
};

} // namespace mir2::game::handlers

#endif // LEGEND2_CLIENT_HANDLERS_MOVEMENT_HANDLER_H
