/**
 * @file skill_handler.h
 * @brief Client skill message handlers.
 */

#ifndef LEGEND2_CLIENT_HANDLERS_SKILL_HANDLER_H
#define LEGEND2_CLIENT_HANDLERS_SKILL_HANDLER_H

#include "client/game/skill/skill_data.h"
#include "client/network/i_network_manager.h"
#include "common/protocol/skill_result.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mir2::proto {
enum class ErrorCode : uint16_t;
}

namespace mir2::game::handlers {

/**
 * @brief Handles skill-related server notifications.
 *
 * Responsibilities:
 * - Register skill handlers with NetworkManager.
 * - Parse FlatBuffers payloads and forward results to callbacks.
 *
 * Threading: expected to be used on the main thread; handlers are invoked from
 * NetworkManager::update() context.
 *
 * Ownership: stores callbacks by value and does not own NetworkManager.
 * Caller manages handler lifetime and should keep it alive while handlers are bound.
 */
class SkillHandler : public std::enable_shared_from_this<SkillHandler> {
public:
    using NetworkPacket = mir2::common::NetworkPacket;

    /**
     * @brief Callback hooks for skill flow.
     *
     * Ownership: stored by value. If owner is provided, callbacks are invoked only
     * while the owner is still alive; otherwise the caller must ensure captured
     * objects outlive the handler.
     */
    struct Callbacks {
        std::optional<std::weak_ptr<void>> owner;

        // Skill use result from server
        std::function<void(mir2::proto::ErrorCode code,
                           uint32_t skill_id,
                           uint64_t caster_id,
                           uint64_t target_id,
                           mir2::common::SkillResult result,
                           int damage,
                           int healing,
                           bool target_dead)> on_skill_result;

        // Cooldown update from server
        std::function<void(uint32_t skill_id, uint32_t cooldown_ms)> on_skill_cooldown;

        // Cast start notification (for cast bar)
        std::function<void(uint64_t caster_id, uint32_t skill_id,
                           uint64_t target_id, uint32_t cast_time_ms)> on_cast_start;

        // Cast interrupted
        std::function<void(uint64_t caster_id, uint32_t skill_id)> on_cast_interrupt;

        // Full skill list sync (on login/learn)
        std::function<void(const std::vector<client::skill::ClientLearnedSkill>&)> on_skill_list;

        // Skill effect to play
        std::function<void(uint32_t effect_id, uint64_t caster_id,
                           uint64_t target_id, int x, int y)> on_skill_effect;

        std::function<void(const std::string& error)> on_parse_error;
    };

    explicit SkillHandler(Callbacks callbacks);
    ~SkillHandler() = default;

    void BindHandlers(mir2::client::INetworkManager& manager);

    void HandleSkillResponse(const NetworkPacket& packet);
    void HandleSkillEffect(const NetworkPacket& packet);
    void HandleSkillListSync(const NetworkPacket& packet);
    void HandleCastStart(const NetworkPacket& packet);
    void HandleCastInterrupt(const NetworkPacket& packet);

private:
    Callbacks callbacks_;

    template<typename Fn>
    void invoke_callback(Fn&& fn) {
        if (callbacks_.owner.has_value() && callbacks_.owner->expired()) {
            return;
        }
        fn();
    }
};

} // namespace mir2::game::handlers

#endif // LEGEND2_CLIENT_HANDLERS_SKILL_HANDLER_H
