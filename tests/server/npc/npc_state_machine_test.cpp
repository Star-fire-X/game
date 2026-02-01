#include <gtest/gtest.h>

#include <vector>

#include <entt/entt.hpp>

#include "game/npc/npc_entity.h"
#include "game/npc/npc_state_machine.h"
#include "game/npc/npc_types.h"

namespace {

using mir2::game::npc::kMaxCoordinate;
using mir2::game::npc::kMinCoordinate;
using mir2::game::npc::NpcEntity;
using mir2::game::npc::NpcPatrolPoint;
using mir2::game::npc::NpcState;
using mir2::game::npc::NpcStateMachine;

TEST(NpcStateMachineTest, IdlePatrolIdleTransition) {
    NpcEntity npc(1);
    NpcStateMachine state_machine(npc);

    state_machine.AddPatrolPoint(5, 5);
    state_machine.SetIdleDuration(0.1f);

    state_machine.Update(1.0f);
    EXPECT_EQ(state_machine.GetState(), NpcState::Patrol);

    state_machine.ClearPatrolPoints();
    EXPECT_EQ(state_machine.GetState(), NpcState::Idle);
}

TEST(NpcStateMachineTest, PlayerInteractionTransitionsToInteract) {
    NpcEntity npc(2);
    NpcStateMachine state_machine(npc);

    entt::registry registry;
    auto player = registry.create();

    state_machine.OnPlayerInteract(player);

    EXPECT_EQ(state_machine.GetState(), NpcState::Interact);
    EXPECT_EQ(state_machine.GetInteractingPlayer(), player);
    EXPECT_TRUE(state_machine.IsMovementBlocked());
}

TEST(NpcStateMachineTest, StateTimerAccumulatesAndResetsOnTransition) {
    NpcEntity npc(3);
    NpcStateMachine state_machine(npc);

    state_machine.Update(0.5f);
    EXPECT_NEAR(state_machine.GetStateTimer(), 0.5f, 1e-4f);

    state_machine.AddPatrolPoint(2, 2);
    state_machine.SetPatrolInterval(100.0f);
    state_machine.TransitionTo(NpcState::Patrol);

    EXPECT_FLOAT_EQ(state_machine.GetStateTimer(), 0.0f);

    state_machine.Update(0.25f);
    EXPECT_EQ(state_machine.GetState(), NpcState::Patrol);
    EXPECT_NEAR(state_machine.GetStateTimer(), 0.25f, 1e-4f);
}

TEST(NpcStateMachineTest, PatrolPointsRespectCoordinateBounds) {
    {
        NpcEntity npc(4);
        npc.SetPosition(kMinCoordinate, kMinCoordinate);
        NpcStateMachine state_machine(npc);
        state_machine.SetPatrolInterval(0.0f);
        state_machine.SetPatrolPoints({NpcPatrolPoint{kMinCoordinate - 1, kMinCoordinate - 1}});
        state_machine.TransitionTo(NpcState::Patrol);

        state_machine.Update(1.0f);

        EXPECT_EQ(npc.GetX(), kMinCoordinate);
        EXPECT_EQ(npc.GetY(), kMinCoordinate);
    }

    {
        NpcEntity npc(5);
        npc.SetPosition(kMaxCoordinate, kMaxCoordinate);
        NpcStateMachine state_machine(npc);
        state_machine.SetPatrolInterval(0.0f);
        state_machine.SetPatrolPoints({NpcPatrolPoint{kMaxCoordinate + 1, kMaxCoordinate + 1}});
        state_machine.TransitionTo(NpcState::Patrol);

        state_machine.Update(1.0f);

        EXPECT_EQ(npc.GetX(), kMaxCoordinate);
        EXPECT_EQ(npc.GetY(), kMaxCoordinate);
    }
}

TEST(NpcStateMachineTest, ServiceStateBlocksAndIgnoresInteractTransition) {
    NpcEntity npc(6);
    NpcStateMachine state_machine(npc);

    state_machine.TransitionTo(NpcState::Service);

    EXPECT_EQ(state_machine.GetState(), NpcState::Service);
    EXPECT_TRUE(state_machine.IsMovementBlocked());

    entt::registry registry;
    auto player = registry.create();
    state_machine.OnPlayerInteract(player);

    EXPECT_EQ(state_machine.GetState(), NpcState::Service);
    EXPECT_TRUE(state_machine.IsMovementBlocked());
    EXPECT_EQ(state_machine.GetInteractingPlayer(), player);

    state_machine.SetServiceDuration(0.1f);
    state_machine.Update(0.1f);

    EXPECT_EQ(state_machine.GetState(), NpcState::Idle);
    EXPECT_FALSE(state_machine.IsMovementBlocked());
}

}  // namespace
