#include "world/role_store.h"

#include <algorithm>
#include <optional>

namespace mir2::world {

RoleStore::RoleStore() = default;

void RoleStore::BindClientAccount(uint64_t client_id, uint64_t account_id) {
    if (client_id == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    client_accounts_[client_id] = account_id;
}

std::optional<uint64_t> RoleStore::GetAccountId(uint64_t client_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = client_accounts_.find(client_id);
    if (it == client_accounts_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void RoleStore::BindClientRole(uint64_t client_id, uint64_t player_id) {
    if (client_id == 0 || player_id == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    client_roles_[client_id] = player_id;
}

std::optional<uint64_t> RoleStore::GetRoleId(uint64_t client_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = client_roles_.find(client_id);
    if (it == client_roles_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void RoleStore::UnbindClient(uint64_t client_id) {
    if (client_id == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    client_accounts_.erase(client_id);
    client_roles_.erase(client_id);
}

std::vector<RoleRecord> RoleStore::GetRoles(uint64_t account_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = account_roles_.find(account_id);
    if (it == account_roles_.end()) {
        return {};
    }
    return it->second;
}

bool RoleStore::RoleNameExists(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return RoleNameExistsLocked(name);
}

bool RoleStore::RoleNameExistsLocked(const std::string& name) const {
    for (const auto& entry : account_roles_) {
        for (const auto& role : entry.second) {
            if (role.name == name) {
                return true;
            }
        }
    }
    return false;
}

mir2::common::ErrorCode RoleStore::CreateRole(uint64_t account_id,
                                              const std::string& name,
                                              uint8_t profession,
                                              uint8_t gender,
                                              RoleRecord* out_role) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (account_id == 0) {
        return mir2::common::ErrorCode::kAccountNotFound;
    }
    if (name.empty()) {
        return mir2::common::ErrorCode::kInvalidAction;
    }
    if (RoleNameExistsLocked(name)) {
        return mir2::common::ErrorCode::kNameExists;
    }

    RoleRecord record;
    record.player_id = next_player_id_.fetch_add(1);
    record.name = name;
    record.profession = profession;
    record.gender = gender;
    account_roles_[account_id].push_back(record);

    if (out_role) {
        *out_role = record;
    }
    return mir2::common::ErrorCode::kOk;
}

bool RoleStore::RemoveRole(uint64_t account_id, uint64_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = account_roles_.find(account_id);
    if (it == account_roles_.end()) {
        return false;
    }

    auto& roles = it->second;
    const auto old_size = roles.size();
    roles.erase(std::remove_if(roles.begin(), roles.end(),
                               [player_id](const RoleRecord& role) {
                                   return role.player_id == player_id;
                               }),
                roles.end());

    if (roles.empty()) {
        account_roles_.erase(it);
    }

    return roles.size() != old_size;
}

std::optional<RoleRecord> RoleStore::FindRole(uint64_t account_id, uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = account_roles_.find(account_id);
    if (it == account_roles_.end()) {
        return std::nullopt;
    }
    for (const auto& role : it->second) {
        if (role.player_id == player_id) {
            return role;
        }
    }
    return std::nullopt;
}

}  // namespace mir2::world
