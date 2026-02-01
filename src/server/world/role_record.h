/**
 * @file role_record.h
 * @brief 角色记录
 */

#ifndef MIR2_WORLD_ROLE_RECORD_H
#define MIR2_WORLD_ROLE_RECORD_H

#include <cstdint>
#include <string>

namespace mir2::world {

/**
 * @brief 角色记录
 */
struct RoleRecord {
    uint64_t player_id = 0;
    std::string name;
    uint8_t profession = 0;
    uint8_t gender = 0;
    uint16_t level = 1;
    uint32_t map_id = 1;
    int x = 100;
    int y = 100;
    uint64_t gold = 0;
};

}  // namespace mir2::world

#endif  // MIR2_WORLD_ROLE_RECORD_H
