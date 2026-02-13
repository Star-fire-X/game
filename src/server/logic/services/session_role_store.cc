#include "logic/services/session_role_store.h"

#include <algorithm>

namespace mir2::logic {

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

  // Remove old reverse mapping if this client was bound to another role
  auto old_it = client_roles_.find(client_id);
  if (old_it != client_roles_.end() && old_it->second != player_id) {
    role_clients_.erase(old_it->second);
  }

  // Remove old client if this role was bound to another client
  auto old_client_it = role_clients_.find(player_id);
  if (old_client_it != role_clients_.end() && old_client_it->second != client_id) {
    client_roles_.erase(old_client_it->second);
  }

  client_roles_[client_id] = player_id;
  role_clients_[player_id] = client_id;
}

std::optional<uint64_t> RoleStore::GetRoleId(uint64_t client_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = client_roles_.find(client_id);
  if (it == client_roles_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<uint64_t> RoleStore::GetClientIdByRoleId(uint64_t player_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = role_clients_.find(player_id);
  if (it == role_clients_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void RoleStore::UnbindClient(uint64_t client_id) {
  if (client_id == 0) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);

  // Remove reverse mapping
  auto role_it = client_roles_.find(client_id);
  if (role_it != client_roles_.end()) {
    role_clients_.erase(role_it->second);
  }

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

void RoleStore::IncrementRoleNameRefLocked(const std::string& name) {
  if (name.empty()) {
    return;
  }
  ++role_name_ref_count_[name];
}

void RoleStore::DecrementRoleNameRefLocked(const std::string& name) {
  if (name.empty()) {
    return;
  }
  auto it = role_name_ref_count_.find(name);
  if (it == role_name_ref_count_.end()) {
    return;
  }
  if (it->second <= 1) {
    role_name_ref_count_.erase(it);
    return;
  }
  --it->second;
}

void RoleStore::AddRole(uint64_t account_id, const RoleRecord& role) {
  if (account_id == 0 || role.player_id == 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto& roles = account_roles_[account_id];
  auto it = std::find_if(roles.begin(), roles.end(),
                         [&role](const RoleRecord& existing) {
                           return existing.player_id == role.player_id;
                         });
  if (it != roles.end()) {
    if (it->name != role.name) {
      DecrementRoleNameRefLocked(it->name);
      IncrementRoleNameRefLocked(role.name);
    }
    *it = role;
  } else {
    roles.push_back(role);
    IncrementRoleNameRefLocked(role.name);
  }

  uint64_t current = next_player_id_.load(std::memory_order_relaxed);
  const uint64_t candidate = role.player_id + 1;
  while (candidate > current &&
         !next_player_id_.compare_exchange_weak(
             current, candidate, std::memory_order_relaxed)) {
  }
}

bool RoleStore::RoleNameExistsLocked(const std::string& name) const {
  return role_name_ref_count_.find(name) != role_name_ref_count_.end();
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
  IncrementRoleNameRefLocked(record.name);

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
  bool removed = false;
  roles.erase(std::remove_if(roles.begin(), roles.end(),
                             [this, player_id, &removed](const RoleRecord& role) {
                               if (role.player_id != player_id) {
                                 return false;
                               }
                               removed = true;
                               DecrementRoleNameRefLocked(role.name);
                               return true;
                             }),
              roles.end());

  if (roles.empty()) {
    account_roles_.erase(it);
  }

  return removed;
}

std::optional<RoleRecord> RoleStore::FindRole(uint64_t account_id,
                                              uint64_t player_id) const {
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

}  // namespace mir2::logic
