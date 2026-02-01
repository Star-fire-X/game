#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "scene/login_scene.h"
#include "scene/scene_manager.h"

namespace {

using mir2::scene::LoginFlowManager;
using mir2::scene::LoginFlowState;
using mir2::scene::SceneState;
using mir2::scene::SceneStateMachine;
using mir2::scene::StateConfig;
using mir2::scene::scene_state_to_string;

class SceneIntegrationTest : public ::testing::Test {
protected:
    SceneStateMachine state_machine_;
    LoginFlowManager login_flow_;
    std::vector<std::string> state_log_;

    void SetUp() override {
        for (auto state : {SceneState::LOGIN, SceneState::CHARACTER_SELECT, SceneState::PLAYING,
                           SceneState::DISCONNECTED}) {
            StateConfig config;
            config.on_enter = [this, state]() {
                state_log_.push_back(std::string("enter:") + scene_state_to_string(state));
            };
            state_machine_.register_state(state, config);
        }
    }
};

} // namespace

TEST_F(SceneIntegrationTest, CompleteLoginFlow) {
    state_machine_.set_state(SceneState::LOGIN);
    login_flow_.start_login("user", "pass");

    login_flow_.on_connect_success();
    EXPECT_EQ(login_flow_.get_state(), LoginFlowState::AUTHENTICATING);

    login_flow_.on_auth_success();
    EXPECT_EQ(login_flow_.get_state(), LoginFlowState::LOADING_CHARACTERS);

    login_flow_.on_characters_loaded();
    EXPECT_EQ(login_flow_.get_state(), LoginFlowState::SUCCESS);

    state_machine_.set_state(SceneState::CHARACTER_SELECT);
    ASSERT_FALSE(state_log_.empty());
    EXPECT_EQ(state_log_.back(), "enter:CHARACTER_SELECT");

    state_machine_.set_state(SceneState::PLAYING);
    EXPECT_EQ(state_log_.back(), "enter:PLAYING");
}

TEST_F(SceneIntegrationTest, LoginFailureRetry) {
    state_machine_.set_state(SceneState::LOGIN);
    login_flow_.start_login("user", "wrong_pass");

    login_flow_.on_connect_success();
    login_flow_.on_auth_failed("Invalid password");

    EXPECT_EQ(login_flow_.get_state(), LoginFlowState::FAILED);
    EXPECT_TRUE(login_flow_.get_error().has_value());

    login_flow_.reset();
    EXPECT_EQ(login_flow_.get_state(), LoginFlowState::IDLE);

    login_flow_.start_login("user", "correct_pass");
    EXPECT_EQ(login_flow_.get_state(), LoginFlowState::CONNECTING);
}

TEST_F(SceneIntegrationTest, DisconnectFlowReturnsToLogin) {
    state_machine_.set_state(SceneState::PLAYING);
    state_machine_.set_state(SceneState::DISCONNECTED);
    state_machine_.set_state(SceneState::LOGIN);

    ASSERT_GE(state_log_.size(), 3u);
    EXPECT_EQ(state_log_[0], "enter:PLAYING");
    EXPECT_EQ(state_log_[1], "enter:DISCONNECTED");
    EXPECT_EQ(state_log_[2], "enter:LOGIN");
}
