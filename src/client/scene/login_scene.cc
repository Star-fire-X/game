#include "scene/login_scene.h"

#include <algorithm>

namespace mir2::scene {

LoginFlowManager::LoginFlowManager(const Config& config)
    : config_(config) {
}

void LoginFlowManager::start_login(const std::string& username, const std::string& password) {
    if (state_ != LoginFlowState::IDLE) {
        return;
    }

    last_error_.reset();
    if (!validate_username(username)) {
        set_error("用户名长度必须在3到32之间");
        set_state(LoginFlowState::FAILED);
        return;
    }
    if (!validate_password(password)) {
        set_error("密码长度必须在6到64之间");
        set_state(LoginFlowState::FAILED);
        return;
    }

    pending_username_ = username;
    pending_password_ = password;
    set_state(LoginFlowState::CONNECTING);
}

void LoginFlowManager::cancel() {
    reset();
}

void LoginFlowManager::reset() {
    pending_username_.clear();
    pending_password_.clear();
    last_error_.reset();
    set_state(LoginFlowState::IDLE);
}

void LoginFlowManager::on_connect_success() {
    if (state_ != LoginFlowState::CONNECTING) {
        return;
    }

    set_state(LoginFlowState::AUTHENTICATING);
    if (on_ready_to_send_login_) {
        on_ready_to_send_login_(pending_username_, pending_password_);
    }
}

void LoginFlowManager::on_connect_failed(const std::string& error) {
    if (state_ != LoginFlowState::CONNECTING) {
        return;
    }

    set_error(error);
    set_state(LoginFlowState::FAILED);
}

void LoginFlowManager::on_auth_success() {
    if (state_ != LoginFlowState::AUTHENTICATING) {
        return;
    }

    set_state(LoginFlowState::LOADING_CHARACTERS);
}

void LoginFlowManager::on_auth_failed(const std::string& error) {
    if (state_ != LoginFlowState::AUTHENTICATING) {
        return;
    }

    set_error(error);
    set_state(LoginFlowState::FAILED);
}

void LoginFlowManager::on_characters_loaded() {
    if (state_ != LoginFlowState::LOADING_CHARACTERS) {
        return;
    }

    set_state(LoginFlowState::SUCCESS);
}

void LoginFlowManager::update(float delta_time) {
    if (state_ == LoginFlowState::CONNECTING || state_ == LoginFlowState::AUTHENTICATING ||
        state_ == LoginFlowState::LOADING_CHARACTERS) {
        state_timer_ += delta_time;
    }

    if (state_ == LoginFlowState::CONNECTING && config_.connect_timeout > 0.0f) {
        if (state_timer_ >= config_.connect_timeout) {
            set_error("连接超时");
            set_state(LoginFlowState::TIMEOUT);
        }
    } else if (state_ == LoginFlowState::AUTHENTICATING && config_.auth_timeout > 0.0f) {
        if (state_timer_ >= config_.auth_timeout) {
            set_error("认证超时");
            set_state(LoginFlowState::TIMEOUT);
        }
    } else if (state_ == LoginFlowState::LOADING_CHARACTERS &&
               state_timer_ >= ESTIMATED_LOAD_TIME * 2.0f) {
        set_error("加载角色列表超时");
        set_state(LoginFlowState::TIMEOUT);
    }
}

bool LoginFlowManager::is_in_progress() const {
    return state_ == LoginFlowState::CONNECTING ||
           state_ == LoginFlowState::AUTHENTICATING ||
           state_ == LoginFlowState::LOADING_CHARACTERS;
}

bool LoginFlowManager::is_terminal() const {
    return state_ == LoginFlowState::SUCCESS ||
           state_ == LoginFlowState::TIMEOUT ||
           state_ == LoginFlowState::FAILED;
}

float LoginFlowManager::get_progress() const {
    auto clamp01 = [](float value) {
        return std::clamp(value, 0.0f, 1.0f);
    };

    switch (state_) {
        case LoginFlowState::IDLE:
            return 0.0f;
        case LoginFlowState::CONNECTING:
            if (config_.connect_timeout <= 0.0f) {
                return 0.0f;
            }
            return clamp01(state_timer_ / config_.connect_timeout) * CONNECT_PROGRESS_WEIGHT;
        case LoginFlowState::AUTHENTICATING:
            if (config_.auth_timeout <= 0.0f) {
                return CONNECT_PROGRESS_WEIGHT;
            }
            return CONNECT_PROGRESS_WEIGHT + clamp01(state_timer_ / config_.auth_timeout) * AUTH_PROGRESS_WEIGHT;
        case LoginFlowState::LOADING_CHARACTERS:
            return (CONNECT_PROGRESS_WEIGHT + AUTH_PROGRESS_WEIGHT) +
                   clamp01(state_timer_ / ESTIMATED_LOAD_TIME) * LOAD_PROGRESS_WEIGHT;
        case LoginFlowState::SUCCESS:
            return 1.0f;
        case LoginFlowState::TIMEOUT:
            return 1.0f;
        case LoginFlowState::FAILED:
            return 1.0f;
    }

    return 0.0f;
}

const char* LoginFlowManager::get_status_text() const {
    switch (state_) {
        case LoginFlowState::IDLE:
            return "等待登录";
        case LoginFlowState::CONNECTING:
            return "连接服务器中...";
        case LoginFlowState::AUTHENTICATING:
            return "认证中...";
        case LoginFlowState::LOADING_CHARACTERS:
            return "加载角色列表中...";
        case LoginFlowState::SUCCESS:
            return "登录成功";
        case LoginFlowState::TIMEOUT:
            return "连接超时";
        case LoginFlowState::FAILED:
            return "登录失败";
    }

    return "";
}

void LoginFlowManager::set_state(LoginFlowState new_state) {
    if (state_ == new_state) {
        return;
    }

    LoginFlowState old_state = state_;
    state_ = new_state;
    state_timer_ = 0.0f;

    if (on_state_change_) {
        on_state_change_(old_state, new_state);
    }
}

void LoginFlowManager::set_error(const std::string& error) {
    if (error.length() > Config::MAX_ERROR_LENGTH) {
        last_error_ = error.substr(0, Config::MAX_ERROR_LENGTH);
    } else {
        last_error_ = error;
    }
}

bool LoginFlowManager::validate_username(const std::string& username) const {
    const size_t length = username.length();
    return length >= Config::USERNAME_MIN_LENGTH &&
           length <= Config::USERNAME_MAX_LENGTH;
}

bool LoginFlowManager::validate_password(const std::string& password) const {
    const size_t length = password.length();
    return length >= Config::PASSWORD_MIN_LENGTH &&
           length <= Config::PASSWORD_MAX_LENGTH;
}

const char* login_flow_state_to_string(LoginFlowState state) {
    switch (state) {
        case LoginFlowState::IDLE:
            return "IDLE";
        case LoginFlowState::CONNECTING:
            return "CONNECTING";
        case LoginFlowState::AUTHENTICATING:
            return "AUTHENTICATING";
        case LoginFlowState::LOADING_CHARACTERS:
            return "LOADING_CHARACTERS";
        case LoginFlowState::SUCCESS:
            return "SUCCESS";
        case LoginFlowState::TIMEOUT:
            return "TIMEOUT";
        case LoginFlowState::FAILED:
            return "ERROR";
    }

    return "UNKNOWN";
}

} // namespace mir2::scene
