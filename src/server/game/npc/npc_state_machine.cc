/**
 * @file npc_state_machine.cc
 * @brief NPC state machine implementation.
 */

#include "game/npc/npc_state_machine.h"

#include <utility>

#include "game/npc/npc_entity.h"

namespace mir2::game::npc {

namespace {
constexpr float kEpsilon = 0.0001f;
}

NpcStateMachine::NpcStateMachine(NpcEntity& npc)
    : npc_(npc) {
    handlers_[ToIndex(NpcState::Idle)].on_update = [this](float dt) {
        update_idle(dt);
    };
    handlers_[ToIndex(NpcState::Patrol)].on_update = [this](float dt) {
        update_patrol(dt);
    };
    handlers_[ToIndex(NpcState::Interact)].on_update = [this](float dt) {
        update_interact(dt);
    };
    handlers_[ToIndex(NpcState::Service)].on_update = [this](float dt) {
        update_service(dt);
    };
    handlers_[ToIndex(NpcState::Dead)].on_update = [this](float dt) {
        update_dead(dt);
    };
}

void NpcStateMachine::Update(float dt) {
    if (dt <= 0.0f) {
        return;
    }

    const auto& handler = handlers_[ToIndex(state_)].on_update;
    if (handler) {
        handler(dt);
        return;
    }

    // Fallback in case handler is cleared.
    switch (state_) {
        case NpcState::Idle:
            update_idle(dt);
            break;
        case NpcState::Patrol:
            update_patrol(dt);
            break;
        case NpcState::Interact:
            update_interact(dt);
            break;
        case NpcState::Service:
            update_service(dt);
            break;
        case NpcState::Dead:
            update_dead(dt);
            break;
    }
}

void NpcStateMachine::OnPlayerInteract(entt::entity player) {
    if (state_ == NpcState::Dead) {
        return;
    }

    if (state_ != NpcState::Interact && state_ != NpcState::Service) {
        TransitionTo(NpcState::Interact);
    }

    interacting_player_ = player;

    if (on_player_interact_) {
        on_player_interact_(player);
    }
}

void NpcStateMachine::TransitionTo(NpcState new_state) {
    if (state_ == new_state) {
        return;
    }

    const NpcState old_state = state_;
    ExitState();

    state_ = new_state;
    state_timer_ = 0.0f;
    patrol_timer_ = 0.0f;

    if (new_state != NpcState::Interact && new_state != NpcState::Service) {
        interacting_player_ = entt::null;
    }

    EnterState(new_state);

    if (on_state_changed_) {
        on_state_changed_(old_state, new_state);
    }
}

void NpcStateMachine::SetPatrolPoints(const std::vector<NpcPatrolPoint>& points) {
    patrol_points_ = points;
    patrol_index_ = 0;

    if (patrol_points_.empty() && state_ == NpcState::Patrol) {
        TransitionTo(NpcState::Idle);
    }
}

void NpcStateMachine::SetPatrolPoints(std::vector<NpcPatrolPoint>&& points) {
    patrol_points_ = std::move(points);
    patrol_index_ = 0;

    if (patrol_points_.empty() && state_ == NpcState::Patrol) {
        TransitionTo(NpcState::Idle);
    }
}

void NpcStateMachine::AddPatrolPoint(int32_t x, int32_t y) {
    patrol_points_.push_back({x, y});
    if (patrol_points_.size() == 1) {
        patrol_index_ = 0;
    }
}

void NpcStateMachine::ClearPatrolPoints() {
    patrol_points_.clear();
    patrol_index_ = 0;

    if (state_ == NpcState::Patrol) {
        TransitionTo(NpcState::Idle);
    }
}

void NpcStateMachine::SetStateUpdateHandler(NpcState state, StateUpdateHandler handler) {
    handlers_[ToIndex(state)].on_update = std::move(handler);
}

void NpcStateMachine::SetStateEnterHandler(NpcState state, StateHandler handler) {
    handlers_[ToIndex(state)].on_enter = std::move(handler);
}

void NpcStateMachine::SetStateExitHandler(NpcState state, StateHandler handler) {
    handlers_[ToIndex(state)].on_exit = std::move(handler);
}

void NpcStateMachine::SetOnStateEnter(StateCallback callback) {
    on_state_enter_ = std::move(callback);
}

void NpcStateMachine::SetOnStateExit(StateCallback callback) {
    on_state_exit_ = std::move(callback);
}

void NpcStateMachine::SetOnStateChanged(StateTransitionCallback callback) {
    on_state_changed_ = std::move(callback);
}

void NpcStateMachine::SetOnPlayerInteract(InteractionCallback callback) {
    on_player_interact_ = std::move(callback);
}

void NpcStateMachine::EnterState(NpcState new_state) {
    movement_blocked_ = (new_state == NpcState::Interact ||
                         new_state == NpcState::Service ||
                         new_state == NpcState::Dead);

    if (new_state == NpcState::Patrol && patrol_index_ >= patrol_points_.size()) {
        patrol_index_ = 0;
    }

    if (new_state == NpcState::Dead) {
        interacting_player_ = entt::null;
    }

    const auto& handler = handlers_[ToIndex(new_state)].on_enter;
    if (handler) {
        handler();
    }

    if (on_state_enter_) {
        on_state_enter_(new_state);
    }
}

void NpcStateMachine::ExitState() {
    const auto& handler = handlers_[ToIndex(state_)].on_exit;
    if (handler) {
        handler();
    }

    if (on_state_exit_) {
        on_state_exit_(state_);
    }
}

void NpcStateMachine::update_idle(float dt) {
    state_timer_ += dt;

    if (!patrol_points_.empty() && idle_duration_ >= 0.0f &&
        state_timer_ + kEpsilon >= idle_duration_) {
        TransitionTo(NpcState::Patrol);
    }
}

void NpcStateMachine::update_patrol(float dt) {
    state_timer_ += dt;

    if (patrol_points_.empty()) {
        TransitionTo(NpcState::Idle);
        return;
    }

    if (movement_blocked_) {
        return;
    }

    patrol_timer_ += dt;
    if (patrol_interval_ < 0.0f || patrol_timer_ + kEpsilon < patrol_interval_) {
        return;
    }

    patrol_timer_ = 0.0f;
    if (patrol_index_ >= patrol_points_.size()) {
        patrol_index_ = 0;
    }

    const NpcPatrolPoint& target = patrol_points_[patrol_index_];
    if (MoveTowards(target)) {
        patrol_index_ = (patrol_index_ + 1) % patrol_points_.size();
    }
}

void NpcStateMachine::update_interact(float dt) {
    state_timer_ += dt;

    if (interact_duration_ > 0.0f && state_timer_ + kEpsilon >= interact_duration_) {
        TransitionTo(NpcState::Idle);
    }
}

void NpcStateMachine::update_service(float dt) {
    state_timer_ += dt;

    if (service_duration_ > 0.0f && state_timer_ + kEpsilon >= service_duration_) {
        TransitionTo(NpcState::Idle);
    }
}

void NpcStateMachine::update_dead(float dt) {
    state_timer_ += dt;
}

bool NpcStateMachine::MoveTowards(const NpcPatrolPoint& target) {
    const int32_t x = npc_.GetX();
    const int32_t y = npc_.GetY();
    if (x == target.x && y == target.y) {
        return true;
    }

    const int32_t step_x = (target.x == x) ? 0 : (target.x > x ? 1 : -1);
    const int32_t step_y = (target.y == y) ? 0 : (target.y > y ? 1 : -1);

    npc_.SetPosition(x + step_x, y + step_y);

    return npc_.GetX() == target.x && npc_.GetY() == target.y;
}

}  // namespace mir2::game::npc
