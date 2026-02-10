/**
 * @file entity_version_component.h
 * @brief ECS entity version component.
 */

#ifndef MIR2_SERVER_ECS_ENTITY_VERSION_COMPONENT_H_
#define MIR2_SERVER_ECS_ENTITY_VERSION_COMPONENT_H_

#include <cstdint>

namespace mir2::ecs {

struct EntityVersionComponent {
    uint32_t version = 0;
};

}  // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_ENTITY_VERSION_COMPONENT_H_
