/**
 * @file component_utils.h
 * @brief ECS component utility functions for defensive programming.
 */

#ifndef MIR2_ECS_COMPONENT_UTILS_H_
#define MIR2_ECS_COMPONENT_UTILS_H_

#include <entt/entt.hpp>
#include <typeinfo>

#include "log/logger.h"

namespace mir2::ecs {

/**
 * @brief Safely get a component with explicit error logging when missing.
 *
 * Use this instead of try_get when the component is expected to exist
 * and its absence indicates a bug or configuration error.
 *
 * @tparam T Component type
 * @param registry The EnTT registry
 * @param entity The entity to query
 * @param context Optional context string for error messages (e.g., function name)
 * @return Pointer to component, or nullptr if missing (with error logged)
 *
 * @example
 * auto* state = require_component<CharacterStateComponent>(registry, entity, "SummonSystem");
 * if (!state) {
 *   return;  // Error already logged
 * }
 */
template <typename T>
T* require_component(entt::registry& registry,
                     entt::entity entity,
                     const char* context = nullptr) {
  T* component = registry.try_get<T>(entity);
  if (!component) {
    SYSLOG_ERROR("{}: entity {} missing {}",
                 context ? context : "Unknown",
                 entt::to_integral(entity),
                 typeid(T).name());
  }
  return component;
}

/**
 * @brief Const version of require_component.
 */
template <typename T>
const T* require_component(const entt::registry& registry,
                           entt::entity entity,
                           const char* context = nullptr) {
  const T* component = registry.try_get<T>(entity);
  if (!component) {
    SYSLOG_ERROR("{}: entity {} missing {}",
                 context ? context : "Unknown",
                 entt::to_integral(entity),
                 typeid(T).name());
  }
  return component;
}

/**
 * @brief Check if entity has all required components, logging errors for missing ones.
 *
 * @tparam Components Component types to check
 * @param registry The EnTT registry
 * @param entity The entity to query
 * @param context Optional context string for error messages
 * @return true if all components exist, false otherwise
 */
template <typename... Components>
bool has_required_components(const entt::registry& registry,
                             entt::entity entity,
                             const char* context = nullptr) {
  bool all_present = true;
  (
      [&]() {
        if (!registry.try_get<Components>(entity)) {
          SYSLOG_ERROR("{}: entity {} missing {}",
                       context ? context : "Unknown",
                       entt::to_integral(entity),
                       typeid(Components).name());
          all_present = false;
        }
      }(),
      ...);
  return all_present;
}

}  // namespace mir2::ecs

#endif  // MIR2_ECS_COMPONENT_UTILS_H_
