#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include "common/types.h"  // 引入 legend2 命名空间的公共类型

namespace mir2::scene {

// 登录流程状态
enum class LoginFlowState {
    IDLE,               // 空闲，等待用户输入
    CONNECTING,         // 连接服务器中
    AUTHENTICATING,     // 认证中（已发送登录请求）
    LOADING_CHARACTERS, // 加载角色列表中
    SUCCESS,            // 登录成功
    TIMEOUT,            // 连接超时
    FAILED              // 错误
};

// 登录流程管理器
class LoginFlowManager {
public:
    // 配置
    struct Config {
        float connect_timeout = 5.0f;  // 连接超时（秒）
        float auth_timeout = 10.0f;    // 认证超时（秒）

        static constexpr size_t USERNAME_MIN_LENGTH = 3;
        static constexpr size_t USERNAME_MAX_LENGTH = 32;
        static constexpr size_t PASSWORD_MIN_LENGTH = 6;
        static constexpr size_t PASSWORD_MAX_LENGTH = 64;
        static constexpr size_t MAX_ERROR_LENGTH = 256;
    };

    explicit LoginFlowManager(const Config& config = Config{});

    // --- 流程控制 ---

    // 开始登录流程
    void start_login(const std::string& username, const std::string& password);

    // 取消当前登录
    void cancel();

    // 重置到空闲状态
    void reset();

    // --- 外部事件通知 ---

    // 连接成功
    void on_connect_success();

    // 连接失败
    void on_connect_failed(const std::string& error);

    // 认证成功
    void on_auth_success();

    // 认证失败
    void on_auth_failed(const std::string& error);

    // 角色列表加载完成
    void on_characters_loaded();

    // --- 每帧更新 ---
    void update(float delta_time);

    // --- 状态查询 ---

    LoginFlowState get_state() const { return state_; }
    bool is_idle() const { return state_ == LoginFlowState::IDLE; }
    bool is_in_progress() const;
    bool is_terminal() const;  // SUCCESS, TIMEOUT, FAILED

    // 获取错误信息（仅在 ERROR/TIMEOUT 状态有效）
    const std::optional<std::string>& get_error() const { return last_error_; }

    // 获取待处理的凭据（用于发送登录请求）
    const std::string& get_pending_username() const { return pending_username_; }
    const std::string& get_pending_password() const { return pending_password_; }

    // 获取进度信息（用于 UI 显示）
    float get_progress() const;
    const char* get_status_text() const;

    // --- 回调 ---

    using StateChangeCallback = std::function<void(LoginFlowState old_state, LoginFlowState new_state)>;
    using ReadyToSendLoginCallback = std::function<void(const std::string& username, const std::string& password)>;

    void set_on_state_change(StateChangeCallback callback) { on_state_change_ = std::move(callback); }
    void set_on_ready_to_send_login(ReadyToSendLoginCallback callback) { on_ready_to_send_login_ = std::move(callback); }

private:
    // Progress calculation constants
    static constexpr float CONNECT_PROGRESS_WEIGHT = 0.33f;  ///< 连接阶段进度权重
    static constexpr float AUTH_PROGRESS_WEIGHT = 0.33f;     ///< 认证阶段进度权重
    static constexpr float LOAD_PROGRESS_WEIGHT = 0.34f;     ///< 加载阶段进度权重
    static constexpr float ESTIMATED_LOAD_TIME = 3.0f;       ///< 角色列表加载估计时间（秒）

    Config config_;
    LoginFlowState state_ = LoginFlowState::IDLE;

    std::string pending_username_;
    std::string pending_password_;
    std::optional<std::string> last_error_;

    float state_timer_ = 0.0f;

    StateChangeCallback on_state_change_;
    ReadyToSendLoginCallback on_ready_to_send_login_;

    void set_state(LoginFlowState new_state);
    void set_error(const std::string& error);
    bool validate_username(const std::string& username) const;
    bool validate_password(const std::string& password) const;
};

// 状态转字符串
const char* login_flow_state_to_string(LoginFlowState state);

} // namespace mir2::scene
