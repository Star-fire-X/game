/**
 * @file handler_context.h
 * @brief Handler context for logic layer.
 */

#ifndef MIR2_LOGIC_HANDLER_CONTEXT_H_
#define MIR2_LOGIC_HANDLER_CONTEXT_H_

#include <cstdint>

#include <entt/entt.hpp>

namespace mir2::ecs {
class World;
}  // namespace mir2::ecs

namespace mir2::logic {

/**
 * @brief Handler context with optional registry/world cache.
 *
 * The cache fields (registry, world, map_id) are populated during logic-stage
 * context building to avoid repeated lookups. After a co_await in a coroutine
 * handler, always call ValidateCache() before using cached pointers, as the
 * player may have changed maps during the suspension.
 */
struct HandlerContext {
  uint64_t client_id = 0;
  uint16_t msg_id = 0;
  uint64_t trace_id = 0;
  uint64_t coroutine_id = 0;
  entt::entity entity = entt::null;
  uint32_t entity_version = 0;

  // Cache fields (optional; may be filled by the logic dispatch path)
  entt::registry* registry = nullptr;
  mir2::ecs::World* world = nullptr;
  uint32_t map_id = 0;

  bool IsValid() const { return entity != entt::null && entity_version != 0; }

  /// Check if cache is populated
  bool HasCache() const { return registry && world && map_id != 0; }

  /// Invalidate cache (call after map change or when cache may be stale)
  void InvalidateCache() {
    registry = nullptr;
    world = nullptr;
    map_id = 0;
  }

  /**
   * @brief Validate that cached registry/entity are still valid.
   *
   * IMPORTANT: Call this after any co_await in coroutine handlers!
   * The player may have changed maps during suspension, making the
   * cached registry pointer dangling.
   *
   * EnTT's valid() checks both slot occupancy and version matching,
   * which is sufficient for single-process scenarios.
   *
   * @return true if cache is valid and entity still exists
   */
  bool ValidateCache() const {
    if (!HasCache()) {
      return false;
    }
    return registry->valid(entity);
  }
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLER_CONTEXT_H_
