/**
 * @file effect_handler.h
 * @brief Client skill effect and audiovisual message handlers.
 */

#ifndef LEGEND2_CLIENT_HANDLERS_EFFECT_HANDLER_H
#define LEGEND2_CLIENT_HANDLERS_EFFECT_HANDLER_H

#include "client/network/i_network_manager.h"
#include "client/render/effect_player.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace mir2::game::handlers {

/**
 * @brief Skill effect playback parameters.
 */
struct SkillEffectParams {
    uint64_t caster_id = 0; ///< 施法者 ID
    uint64_t target_id = 0; ///< 目标 ID
    uint32_t skill_id = 0;  ///< 技能 ID

    mir2::render::EffectPlayType effect_type = mir2::render::EffectPlayType::CAST; ///< 播放类型
    std::string effect_id; ///< 特效资源 ID
    std::string sound_id;  ///< 音效资源 ID

    int x = 0; ///< 播放位置 X
    int y = 0; ///< 播放位置 Y
    uint32_t duration_ms = 0; ///< 持续时间（毫秒）

    // 未来扩展字段（可选）
    // mir2::common::SkillResult result = mir2::common::SkillResult::HIT;
    // int damage = 0;
};

/**
 * @brief Handles effect-related server notifications.
 *
 * Responsibilities:
 * - Register effect handlers with NetworkManager.
 * - Parse FlatBuffers payloads and forward results to callbacks.
 *
 * Threading: expected to be used on the main thread; handlers are invoked from
 * NetworkManager::update() context.
 *
 * Ownership: stores callbacks by value and does not own NetworkManager.
 * Caller manages handler lifetime and should keep it alive while handlers are bound.
 */
class EffectHandler : public std::enable_shared_from_this<EffectHandler> {
public:
    using NetworkPacket = mir2::common::NetworkPacket;

    /**
     * @brief Callback hooks for skill/effect playback.
     *
     * Ownership: stored by value. If owner is provided, callbacks are invoked only
     * while the owner is still alive; otherwise the caller must ensure captured
     * objects outlive the handler.
     */
    struct Callbacks {
        // Optional lifetime guard for objects captured by callbacks.
        std::optional<std::weak_ptr<void>> owner;
        std::function<void(const SkillEffectParams& params)> on_skill_effect;
        std::function<void(const std::string& effect_id,
                           int x,
                           int y,
                           uint8_t direction,
                           uint32_t duration_ms)> on_play_effect;
        std::function<void(const std::string& sound_id, int x, int y)> on_play_sound;
        std::function<void(const std::string& error)> on_parse_error;
    };

    explicit EffectHandler(Callbacks callbacks);
    ~EffectHandler() = default;

    // Bind handlers with lifetime safety. Requires shared_ptr ownership.
    void BindHandlers(mir2::client::INetworkManager& manager);
    static void RegisterHandlers(mir2::client::INetworkManager& manager);

    void HandleSkillEffect(const NetworkPacket& packet);
    void HandlePlayEffect(const NetworkPacket& packet);
    void HandlePlaySound(const NetworkPacket& packet);

private:
    Callbacks callbacks_;
};

} // namespace mir2::game::handlers

#endif // LEGEND2_CLIENT_HANDLERS_EFFECT_HANDLER_H
