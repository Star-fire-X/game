#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "scene/scene_manager.h"

namespace {

using mir2::scene::SceneState;
using mir2::scene::SceneStateMachine;
using mir2::scene::StateConfig;

class SceneStateMachineTest : public ::testing::Test {
protected:
    SceneStateMachine state_machine_;
};

class MockStateCallbacks {
public:
    MOCK_METHOD(void, OnEnter, (), ());
    MOCK_METHOD(void, OnExit, (), ());
    MOCK_METHOD(void, OnUpdate, (float), ());
    MOCK_METHOD(void, OnRender, (), ());
    MOCK_METHOD(void, OnProcessInput, (), ());
};

} // namespace

TEST_F(SceneStateMachineTest, BasicStateTransition) {
    StateConfig config;
    state_machine_.register_state(SceneState::LOGIN, config);

    EXPECT_NO_THROW(state_machine_.set_state(SceneState::LOGIN));
    EXPECT_EQ(state_machine_.get_state(), SceneState::LOGIN);
}

#ifndef NDEBUG
TEST_F(SceneStateMachineTest, ThrowsOnUnregisteredState) {
    EXPECT_THROW(state_machine_.set_state(SceneState::PLAYING), std::runtime_error);
}
#endif

TEST_F(SceneStateMachineTest, LifecycleCallbacksOrder) {
    std::vector<std::string> calls;

    StateConfig login_config;
    login_config.on_enter = [&calls]() { calls.push_back("login_enter"); };
    login_config.on_exit = [&calls]() { calls.push_back("login_exit"); };

    StateConfig select_config;
    select_config.on_enter = [&calls]() { calls.push_back("select_enter"); };

    state_machine_.register_state(SceneState::LOGIN, login_config);
    state_machine_.register_state(SceneState::CHARACTER_SELECT, select_config);

    state_machine_.set_state(SceneState::LOGIN);
    state_machine_.set_state(SceneState::CHARACTER_SELECT);

    ASSERT_EQ(calls.size(), 3u);
    EXPECT_EQ(calls[0], "login_enter");
    EXPECT_EQ(calls[1], "login_exit");
    EXPECT_EQ(calls[2], "select_enter");
}

TEST_F(SceneStateMachineTest, SameStateNoCallback) {
    int enter_count = 0;
    int exit_count = 0;

    StateConfig config;
    config.on_enter = [&enter_count]() { ++enter_count; };
    config.on_exit = [&exit_count]() { ++exit_count; };

    state_machine_.register_state(SceneState::LOGIN, config);

    state_machine_.set_state(SceneState::LOGIN);
    EXPECT_EQ(enter_count, 1);
    EXPECT_EQ(exit_count, 0);

    state_machine_.set_state(SceneState::LOGIN);
    EXPECT_EQ(enter_count, 1);
    EXPECT_EQ(exit_count, 0);
}

TEST_F(SceneStateMachineTest, PerFrameCallbacks) {
    ::testing::StrictMock<MockStateCallbacks> callbacks;

    StateConfig config;
    config.update = [&callbacks](float dt) { callbacks.OnUpdate(dt); };
    config.render = [&callbacks]() { callbacks.OnRender(); };
    config.process_input = [&callbacks]() { callbacks.OnProcessInput(); };

    state_machine_.register_state(SceneState::PLAYING, config);
    state_machine_.set_state(SceneState::PLAYING);

    EXPECT_CALL(callbacks, OnUpdate(::testing::Gt(0.0f))).Times(1);
    EXPECT_CALL(callbacks, OnRender()).Times(1);
    EXPECT_CALL(callbacks, OnProcessInput()).Times(1);

    state_machine_.update(0.016f);
    state_machine_.render();
    state_machine_.process_input();
}

TEST_F(SceneStateMachineTest, StateQueryHelpers) {
    state_machine_.register_state(SceneState::LOGIN, {});
    state_machine_.register_state(SceneState::PLAYING, {});

    state_machine_.set_state(SceneState::LOGIN);
    EXPECT_TRUE(state_machine_.is_in_login_flow());
    EXPECT_FALSE(state_machine_.is_playing());

    state_machine_.set_state(SceneState::PLAYING);
    EXPECT_FALSE(state_machine_.is_in_login_flow());
    EXPECT_TRUE(state_machine_.is_playing());
}

TEST_F(SceneStateMachineTest, DebugStateName) {
    EXPECT_EQ(state_machine_.get_state_name(), std::string("UNKNOWN"));

    state_machine_.register_state(SceneState::LOGIN, {});
    state_machine_.set_state(SceneState::LOGIN);

    EXPECT_EQ(state_machine_.get_state_name(), std::string("LOGIN"));
}

TEST_F(SceneStateMachineTest, SameStateDoesNotTriggerExitEnterCallbacks) {
    ::testing::StrictMock<MockStateCallbacks> callbacks;

    StateConfig config;
    config.on_enter = [&callbacks]() { callbacks.OnEnter(); };
    config.on_exit = [&callbacks]() { callbacks.OnExit(); };

    state_machine_.register_state(SceneState::LOGIN, config);

    EXPECT_CALL(callbacks, OnEnter()).Times(1);
    state_machine_.set_state(SceneState::LOGIN);

    state_machine_.set_state(SceneState::LOGIN);
}
