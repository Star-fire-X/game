// =============================================================================
// Legend2 移动控制器 (Movement Controller)
// =============================================================================

#ifndef LEGEND2_CLIENT_GAME_MOVEMENT_CONTROLLER_H
#define LEGEND2_CLIENT_GAME_MOVEMENT_CONTROLLER_H

#include "client/game/map/i_walkability_provider.h"
#include "client/network/i_network_manager.h"
#include "client/core/position_interpolator.h"
#include "common/protocol/message_codec.h"
#include "common/types.h"

#include <cstdint>

namespace mir2::game {

using mir2::common::Position;
using mir2::client::INetworkManager;
using mir2::game::map::IWalkabilityProvider;

/// 移动控制器
/// 负责点击移动验证、发送请求和插值更新
class MovementController {
public:
    MovementController(IWalkabilityProvider& walkability,
                       INetworkManager& network,
                       legend2::PositionInterpolator& interpolator)
        : walkability_(walkability)
        , network_(network)
        , interpolator_(interpolator) {}

    /// 处理点击移动请求：验证目标可达 → 发送网络请求
    bool request_move(const Position& current, const Position& target) {
        if (!validate_click_target(target)) {
            return false;
        }
        if (!network_.is_connected()) {
            return false;
        }

        mir2::common::MoveRequest request;
        request.target_x = target.x;
        request.target_y = target.y;
        mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
        const auto payload = mir2::common::EncodeMoveRequest(request, &status);
        if (status != mir2::common::MessageCodecStatus::kOk || payload.empty()) {
            return false;
        }
        network_.send_message(mir2::common::MsgId::kMoveReq, payload);

        // 客户端预测：立即开始插值
        if (last_confirmed_.x < 0 || last_confirmed_.y < 0) {
            last_confirmed_ = current;
        }
        interpolator_.set_target(target);
        return true;
    }

    /// 处理服务器移动响应：更新插值器
    void on_move_response(const Position& confirmed_pos) {
        last_confirmed_ = confirmed_pos;
        interpolator_.set_target(confirmed_pos);
    }

    /// 处理服务器拒绝移动：回滚到上次确认位置
    void on_move_failed() {
        if (last_confirmed_.x >= 0 && last_confirmed_.y >= 0) {
            interpolator_.set_target(last_confirmed_);
        }
    }

    /// 处理其他实体移动广播
    void on_entity_move(uint64_t /*entity_id*/, const Position& /*new_pos*/, uint8_t /*direction*/) {
        // 留给 GameClient/EntityManager 处理
    }

    /// 每帧更新插值器
    void update(float delta_ms) {
        interpolator_.update(delta_ms);
    }

private:
    bool validate_click_target(const Position& target) const {
        if (!walkability_.is_valid_position(target.x, target.y)) {
            return false;
        }
        return walkability_.is_walkable(target.x, target.y);
    }

    IWalkabilityProvider& walkability_;
    INetworkManager& network_;
    legend2::PositionInterpolator& interpolator_;
    Position last_confirmed_{-1, -1};
};

} // namespace mir2::game

#endif // LEGEND2_CLIENT_GAME_MOVEMENT_CONTROLLER_H
