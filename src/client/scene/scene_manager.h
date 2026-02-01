#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include "common/types.h"  // 引入 legend2 命名空间的公共类型

namespace mir2::scene {

// =============================================================================
// Scene state
// =============================================================================
// Scene system uses this state machine directly (no IScene/ISceneManager layer).

/// Unified scene states (shared with GameState via alias).
enum class SceneState {
    INITIALIZING,     ///< 客户端初始化阶段：加载基础资源、初始化子系统
    LOGIN,            ///< 登录界面：用户输入账号密码，连接服务器
    CHARACTER_SELECT, ///< 角色选择界面：显示角色列表，选择/删除角色
    CHARACTER_CREATE, ///< 角色创建界面：设置角色名、职业、性别
    LOADING,          ///< 加载游戏场景：加载地图数据、周边玩家信息
    PLAYING,          ///< 游戏主场景：角色移动、战斗、交互等核心玩法
    DISCONNECTED,     ///< 断线状态：显示重连提示，尝试自动重连
    EXITING           ///< 退出流程：保存数据、释放资源、关闭连接
};

// State configuration
struct StateConfig {
    std::function<void()> on_enter = nullptr;       // Enter state callback
    std::function<void()> on_exit = nullptr;        // Exit state callback
    std::function<void(float)> update = nullptr;    // State update
    std::function<void()> render = nullptr;         // State render
    std::function<void()> process_input = nullptr;  // Input processing
    std::string name;                               // State name (debug)
};

// State machine manager
class SceneStateMachine {
public:
    SceneStateMachine() = default;

    // Register state configuration
    void register_state(SceneState state, StateConfig config);

    // State transition (triggers on_exit and on_enter)
    void set_state(SceneState new_state);

    // Get current state
    SceneState get_state() const { return current_state_; }

    // Per-frame calls
    void update(float delta_time);
    void render();
    void process_input();

    // State query helpers
    bool is_in_login_flow() const;
    bool is_playing() const { return current_state_ == SceneState::PLAYING; }

    // State name (debug)
    const std::string& get_state_name() const;

private:
    SceneState current_state_ = SceneState::INITIALIZING;
    std::unordered_map<SceneState, StateConfig> state_configs_;
};

// SceneState to string
const char* scene_state_to_string(SceneState state);

} // namespace mir2::scene
