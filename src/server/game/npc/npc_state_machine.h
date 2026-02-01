/**
 * @file npc_state_machine.h
 * @brief NPC state machine definition.
 */

#ifndef MIR2_GAME_NPC_NPC_STATE_MACHINE_H_
#define MIR2_GAME_NPC_NPC_STATE_MACHINE_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include <entt/entt.hpp>

namespace mir2::game::npc {

class NpcEntity;

/**
 * @brief NPC state definition.
 */
enum class NpcState : uint8_t {
    Idle = 0,
    Patrol = 1,
    Interact = 2,
    Service = 3,
    Dead = 4
};

/**
 * @brief Patrol point for NPC.
 */
struct NpcPatrolPoint {
    int32_t x = 0;
    int32_t y = 0;
};

/**
 * @brief NPC state machine.
 *
 * Simplified state machine based on MonsterAI:
 * - Idle / Patrol / Interact / Service / Dead
 * - Optional patrol point list
 * - Interaction blocks movement
 */
class NpcStateMachine {
public:
    using StateHandler = std::function<void()>;
    using StateUpdateHandler = std::function<void(float)>;
    using StateCallback = std::function<void(NpcState)>;
    using StateTransitionCallback = std::function<void(NpcState, NpcState)>;
    using InteractionCallback = std::function<void(entt::entity)>;

    explicit NpcStateMachine(NpcEntity& npc);

    /**
     * @brief Update state machine.
     * @param dt Delta time in seconds.
     */
    void Update(float dt);

    /**
     * @brief Handle player interaction.
     */
    void OnPlayerInteract(entt::entity player);

    /**
     * @brief Force transition to a new state.
     */
    void TransitionTo(NpcState new_state);

    /**
     * @brief Current state.
     */
    NpcState GetState() const { return state_; }

    /**
     * @brief Time elapsed in current state.
     */
    float GetStateTimer() const { return state_timer_; }

    /**
     * @brief Whether movement is blocked by current state.
     */
    bool IsMovementBlocked() const { return movement_blocked_; }

    /**
     * @brief Interacting player entity.
     */
    entt::entity GetInteractingPlayer() const { return interacting_player_; }

    /**
     * @brief Set patrol points (copy).
     */
    void SetPatrolPoints(const std::vector<NpcPatrolPoint>& points);

    /**
     * @brief Set patrol points (move).
     */
    void SetPatrolPoints(std::vector<NpcPatrolPoint>&& points);

    /**
     * @brief Add a patrol point.
     */
    void AddPatrolPoint(int32_t x, int32_t y);

    /**
     * @brief Clear patrol points.
     */
    void ClearPatrolPoints();

    /**
     * @brief Get patrol points.
     */
    const std::vector<NpcPatrolPoint>& GetPatrolPoints() const {
        return patrol_points_;
    }

    /**
     * @brief Set idle duration before entering patrol (seconds).
     */
    void SetIdleDuration(float seconds) { idle_duration_ = seconds; }

    /**
     * @brief Set patrol step interval (seconds).
     */
    void SetPatrolInterval(float seconds) { patrol_interval_ = seconds; }

    /**
     * @brief Set interaction timeout (seconds). 0 means no auto-exit.
     */
    void SetInteractDuration(float seconds) { interact_duration_ = seconds; }

    /**
     * @brief Set service timeout (seconds). 0 means no auto-exit.
     */
    void SetServiceDuration(float seconds) { service_duration_ = seconds; }

    /**
     * @brief Set per-state update handler (override default logic).
     */
    void SetStateUpdateHandler(NpcState state, StateUpdateHandler handler);

    /**
     * @brief Set per-state enter handler.
     */
    void SetStateEnterHandler(NpcState state, StateHandler handler);

    /**
     * @brief Set per-state exit handler.
     */
    void SetStateExitHandler(NpcState state, StateHandler handler);

    /**
     * @brief Set state enter callback.
     */
    void SetOnStateEnter(StateCallback callback);

    /**
     * @brief Set state exit callback.
     */
    void SetOnStateExit(StateCallback callback);

    /**
     * @brief Set state transition callback.
     */
    void SetOnStateChanged(StateTransitionCallback callback);

    /**
     * @brief Set interaction callback.
     */
    void SetOnPlayerInteract(InteractionCallback callback);

private:
    struct StateHandlers {
        StateHandler on_enter;
        StateHandler on_exit;
        StateUpdateHandler on_update;
    };

    static constexpr size_t kStateCount = 5;

    static constexpr size_t ToIndex(NpcState state) {
        return static_cast<size_t>(state);
    }

    void EnterState(NpcState new_state);
    void ExitState();

    void update_idle(float dt);
    void update_patrol(float dt);
    void update_interact(float dt);
    void update_service(float dt);
    void update_dead(float dt);

    bool MoveTowards(const NpcPatrolPoint& target);

    NpcEntity& npc_;
    NpcState state_ = NpcState::Idle;
    float state_timer_ = 0.0f;
    float patrol_timer_ = 0.0f;
    float idle_duration_ = 2.0f;
    float patrol_interval_ = 1.0f;
    float interact_duration_ = 0.0f;
    float service_duration_ = 0.0f;

    std::vector<NpcPatrolPoint> patrol_points_;
    size_t patrol_index_ = 0;

    bool movement_blocked_ = false;
    entt::entity interacting_player_ = entt::null;

    std::array<StateHandlers, kStateCount> handlers_{};
    StateCallback on_state_enter_;
    StateCallback on_state_exit_;
    StateTransitionCallback on_state_changed_;
    InteractionCallback on_player_interact_;
};

}  // namespace mir2::game::npc

#endif  // MIR2_GAME_NPC_NPC_STATE_MACHINE_H_
