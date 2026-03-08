/**
 * @file session_role_store.h
 * @brief Session/account/role bindings used by logic handlers.
 */

#ifndef MIR2_LOGIC_SERVICES_SESSION_ROLE_STORE_H_
#define MIR2_LOGIC_SERVICES_SESSION_ROLE_STORE_H_

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "logic/services/role_record.h"
#include "server/common/error_codes.h"

namespace mir2::logic {

class RoleStore {
 public:
  RoleStore();

  void BindClientAccount(uint64_t client_id, uint64_t account_id);
  std::optional<uint64_t> GetAccountId(uint64_t client_id) const;
  std::optional<uint64_t> BindClientRole(uint64_t client_id, uint64_t player_id);
  std::optional<uint64_t> GetRoleId(uint64_t client_id) const;
  std::optional<uint64_t> GetClientIdByRoleId(uint64_t player_id) const;
  void UnbindClient(uint64_t client_id);

  std::vector<RoleRecord> GetRoles(uint64_t account_id) const;
  bool RoleNameExists(const std::string& name) const;
  void AddRole(uint64_t account_id, const RoleRecord& role);

  mir2::common::ErrorCode CreateRole(uint64_t account_id,
                                     const std::string& name,
                                     uint8_t profession,
                                     uint8_t gender,
                                     RoleRecord* out_role);

  bool RemoveRole(uint64_t account_id, uint64_t player_id);
  std::optional<RoleRecord> FindRole(uint64_t account_id, uint64_t player_id) const;

 private:
  bool RoleNameExistsLocked(const std::string& name) const;
  void IncrementRoleNameRefLocked(const std::string& name);
  void DecrementRoleNameRefLocked(const std::string& name);

  std::atomic<uint64_t> next_player_id_{1000};
  mutable std::mutex mutex_;
  std::unordered_map<uint64_t, uint64_t> client_accounts_;
  std::unordered_map<uint64_t, uint64_t> client_roles_;
  std::unordered_map<uint64_t, uint64_t> role_clients_;  // reverse mapping: role_id -> client_id
  std::unordered_map<uint64_t, std::vector<RoleRecord>> account_roles_;
  std::unordered_map<std::string, size_t> role_name_ref_count_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_SERVICES_SESSION_ROLE_STORE_H_
