#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "scene/login_scene.h"

namespace {

using mir2::scene::LoginFlowManager;
using mir2::scene::LoginFlowState;

class LoginFlowManagerTest : public ::testing::Test {
protected:
    LoginFlowManager::Config config_;
    std::unique_ptr<LoginFlowManager> flow_;

    void SetUp() override {
        config_.connect_timeout = 1.0f;
        config_.auth_timeout = 2.0f;
        flow_ = std::make_unique<LoginFlowManager>(config_);
    }
};

} // namespace

TEST_F(LoginFlowManagerTest, InitialStateIsIdle) {
    EXPECT_EQ(flow_->get_state(), LoginFlowState::IDLE);
    EXPECT_TRUE(flow_->is_idle());
    EXPECT_FALSE(flow_->is_in_progress());
    EXPECT_FALSE(flow_->is_terminal());
}

TEST_F(LoginFlowManagerTest, StartLoginChangesStateAndCredentials) {
    flow_->start_login("test_user", "test_pass");

    EXPECT_EQ(flow_->get_state(), LoginFlowState::CONNECTING);
    EXPECT_EQ(flow_->get_pending_username(), "test_user");
    EXPECT_EQ(flow_->get_pending_password(), "test_pass");
    EXPECT_TRUE(flow_->is_in_progress());
}

TEST_F(LoginFlowManagerTest, StartLoginIgnoredWhenNotIdle) {
    flow_->start_login("first", "pass");
    flow_->start_login("second", "other");

    EXPECT_EQ(flow_->get_state(), LoginFlowState::CONNECTING);
    EXPECT_EQ(flow_->get_pending_username(), "first");
    EXPECT_EQ(flow_->get_pending_password(), "pass");
}

TEST_F(LoginFlowManagerTest, StateChangeCallbackFiresOnTransition) {
    std::optional<LoginFlowState> observed_old;
    std::optional<LoginFlowState> observed_new;

    flow_->set_on_state_change([&](LoginFlowState old_state, LoginFlowState new_state) {
        observed_old = old_state;
        observed_new = new_state;
    });

    flow_->start_login("user", "pass");

    ASSERT_TRUE(observed_old.has_value());
    ASSERT_TRUE(observed_new.has_value());
    EXPECT_EQ(*observed_old, LoginFlowState::IDLE);
    EXPECT_EQ(*observed_new, LoginFlowState::CONNECTING);
}

TEST_F(LoginFlowManagerTest, ConnectSuccessTriggersAuthAndSendLogin) {
    bool login_sent = false;
    flow_->set_on_ready_to_send_login([&](const std::string& user, const std::string& pass) {
        login_sent = true;
        EXPECT_EQ(user, "user");
        EXPECT_EQ(pass, "pass");
    });

    flow_->start_login("user", "pass");
    flow_->on_connect_success();

    EXPECT_EQ(flow_->get_state(), LoginFlowState::AUTHENTICATING);
    EXPECT_TRUE(login_sent);
}

TEST_F(LoginFlowManagerTest, ConnectFailureSetsError) {
    flow_->start_login("user", "pass");
    flow_->on_connect_failed("connection failed");

    EXPECT_EQ(flow_->get_state(), LoginFlowState::FAILED);
    EXPECT_TRUE(flow_->is_terminal());
    ASSERT_TRUE(flow_->get_error().has_value());
    EXPECT_EQ(*flow_->get_error(), "connection failed");
}

TEST_F(LoginFlowManagerTest, AuthFailureSetsError) {
    flow_->start_login("user", "pass");
    flow_->on_connect_success();
    flow_->on_auth_failed("auth failed");

    EXPECT_EQ(flow_->get_state(), LoginFlowState::FAILED);
    EXPECT_TRUE(flow_->is_terminal());
    ASSERT_TRUE(flow_->get_error().has_value());
    EXPECT_EQ(*flow_->get_error(), "auth failed");
}

TEST_F(LoginFlowManagerTest, AuthSuccessMovesToLoadingAndSuccess) {
    flow_->start_login("user", "pass");
    flow_->on_connect_success();

    flow_->on_auth_success();
    EXPECT_EQ(flow_->get_state(), LoginFlowState::LOADING_CHARACTERS);

    flow_->on_characters_loaded();
    EXPECT_EQ(flow_->get_state(), LoginFlowState::SUCCESS);
    EXPECT_TRUE(flow_->is_terminal());
}

TEST_F(LoginFlowManagerTest, ConnectTimeoutTransitionsToTimeout) {
    flow_->start_login("user", "pass");

    flow_->update(1.1f);

    EXPECT_EQ(flow_->get_state(), LoginFlowState::TIMEOUT);
    EXPECT_TRUE(flow_->is_terminal());
    EXPECT_TRUE(flow_->get_error().has_value());
}

TEST_F(LoginFlowManagerTest, AuthTimeoutTransitionsToTimeout) {
    flow_->start_login("user", "pass");
    flow_->on_connect_success();

    flow_->update(2.1f);

    EXPECT_EQ(flow_->get_state(), LoginFlowState::TIMEOUT);
    EXPECT_TRUE(flow_->is_terminal());
    EXPECT_TRUE(flow_->get_error().has_value());
}

TEST_F(LoginFlowManagerTest, ProgressWeightsSumToOne) {
    flow_->start_login("user", "pass");
    flow_->on_connect_success();
    flow_->on_auth_success();

    flow_->update(3.0f);
    float progress = flow_->get_progress();

    EXPECT_NEAR(progress, 1.0f, 0.01f);
}

TEST_F(LoginFlowManagerTest, CancelResetsStateAndCredentials) {
    flow_->start_login("user", "pass");
    flow_->cancel();

    EXPECT_EQ(flow_->get_state(), LoginFlowState::IDLE);
    EXPECT_TRUE(flow_->get_pending_username().empty());
    EXPECT_TRUE(flow_->get_pending_password().empty());
    EXPECT_FALSE(flow_->get_error().has_value());
}
