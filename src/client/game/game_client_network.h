#ifndef LEGEND2_GAME_CLIENT_NETWORK_H
#define LEGEND2_GAME_CLIENT_NETWORK_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace mir2::client {
class INetworkManager;
} // namespace mir2::client

namespace mir2::game {

// =============================================================================
// 网络子模块 (Game Client Network)
// =============================================================================

/// 游戏客户端网络子模块
/// 负责连接管理、消息发送与回调注册
class GameClientNetwork {
public:
    struct Callbacks {
        std::function<void()> on_connected;
        std::function<void()> on_disconnected;
    };

    GameClientNetwork(mir2::client::INetworkManager* network_manager,
                      Callbacks callbacks);

    bool Connect(const std::string& host, uint16_t port);
    void Disconnect();
    bool IsConnected() const;

private:
    mir2::client::INetworkManager* network_manager_ = nullptr;
    Callbacks callbacks_;
};

} // namespace mir2::game

#endif // LEGEND2_GAME_CLIENT_NETWORK_H
