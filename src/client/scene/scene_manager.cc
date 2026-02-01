#include "scene/scene_manager.h"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace mir2::scene {

void SceneStateMachine::register_state(SceneState state, StateConfig config) {
    if (config.name.empty()) {
        config.name = scene_state_to_string(state);
    }
    state_configs_[state] = std::move(config);
}

void SceneStateMachine::set_state(SceneState new_state) {
    if (current_state_ == new_state) {
        return;
    }

    auto new_state_it = state_configs_.find(new_state);
    if (new_state_it == state_configs_.end()) {
#ifndef NDEBUG
        throw std::runtime_error(
            std::string("Unregistered state: ") + scene_state_to_string(new_state));
#else
        std::cerr << "Warning: Unregistered state: " << scene_state_to_string(new_state)
                  << std::endl;
        return;
#endif
    }

    auto old_state_it = state_configs_.find(current_state_);
    if (old_state_it != state_configs_.end() && old_state_it->second.on_exit) {
        old_state_it->second.on_exit();
    }

    current_state_ = new_state;

    if (new_state_it->second.on_enter) {
        new_state_it->second.on_enter();
    }
}

void SceneStateMachine::update(float delta_time) {
    auto state_it = state_configs_.find(current_state_);
    if (state_it != state_configs_.end() && state_it->second.update) {
        state_it->second.update(delta_time);
    }
}

void SceneStateMachine::render() {
    auto state_it = state_configs_.find(current_state_);
    if (state_it != state_configs_.end() && state_it->second.render) {
        state_it->second.render();
    }
}

void SceneStateMachine::process_input() {
    auto state_it = state_configs_.find(current_state_);
    if (state_it != state_configs_.end() && state_it->second.process_input) {
        state_it->second.process_input();
    }
}

bool SceneStateMachine::is_in_login_flow() const {
    return current_state_ == SceneState::LOGIN ||
           current_state_ == SceneState::CHARACTER_SELECT ||
           current_state_ == SceneState::CHARACTER_CREATE;
}

const std::string& SceneStateMachine::get_state_name() const {
    auto state_it = state_configs_.find(current_state_);
    if (state_it != state_configs_.end() && !state_it->second.name.empty()) {
        return state_it->second.name;
    }

    static const std::string kUnknown = "UNKNOWN";
    return kUnknown;
}

const char* scene_state_to_string(SceneState state) {
    switch (state) {
        case SceneState::INITIALIZING:
            return "INITIALIZING";
        case SceneState::LOGIN:
            return "LOGIN";
        case SceneState::CHARACTER_SELECT:
            return "CHARACTER_SELECT";
        case SceneState::CHARACTER_CREATE:
            return "CHARACTER_CREATE";
        case SceneState::LOADING:
            return "LOADING";
        case SceneState::PLAYING:
            return "PLAYING";
        case SceneState::DISCONNECTED:
            return "DISCONNECTED";
        case SceneState::EXITING:
            return "EXITING";
        default:
            return "UNKNOWN";
    }
}

} // namespace mir2::scene
