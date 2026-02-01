/**
 * @file role_store.h
 * @brief 角色存储
 */

#ifndef MIR2_WORLD_ROLE_STORE_H
#define MIR2_WORLD_ROLE_STORE_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "server/common/error_codes.h"
#include "world/role_record.h"

namespace mir2::world {

/**
 * @brief 角色数据存储
 */
class RoleStore {
public:
    RoleStore();

    void BindClientAccount(uint64_t client_id, uint64_t account_id);
    std::optional<uint64_t> GetAccountId(uint64_t client_id) const;
    void BindClientRole(uint64_t client_id, uint64_t player_id);
    std::optional<uint64_t> GetRoleId(uint64_t client_id) const;
    void UnbindClient(uint64_t client_id);

    std::vector<RoleRecord> GetRoles(uint64_t account_id) const;
    bool RoleNameExists(const std::string& name) const;

    mir2::common::ErrorCode CreateRole(uint64_t account_id,
                                       const std::string& name,
                                       uint8_t profession,
                                       uint8_t gender,
                                       RoleRecord* out_role);

    bool RemoveRole(uint64_t account_id, uint64_t player_id);

    std::optional<RoleRecord> FindRole(uint64_t account_id, uint64_t player_id) const;

private:
    bool RoleNameExistsLocked(const std::string& name) const;

    std::atomic<uint64_t> next_player_id_{1000};
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, uint64_t> client_accounts_;
    std::unordered_map<uint64_t, uint64_t> client_roles_;
    std::unordered_map<uint64_t, std::vector<RoleRecord>> account_roles_;
};

}  // namespace mir2::world

#endif  // MIR2_WORLD_ROLE_STORE_H
