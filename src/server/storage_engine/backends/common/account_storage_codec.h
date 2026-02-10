#ifndef MIR2_STORAGE_ENGINE_BACKENDS_COMMON_ACCOUNT_STORAGE_CODEC_H_
#define MIR2_STORAGE_ENGINE_BACKENDS_COMMON_ACCOUNT_STORAGE_CODEC_H_

#include <optional>
#include <string>
#include <vector>

#include "common/types/database_types.h"

namespace mir2::db {

inline constexpr char kAccountKeyPrefix[] = "account:username:";

std::string BuildAccountStorageKey(const std::string& username);

std::optional<std::string> ParseAccountStorageKey(const std::string& key);

std::vector<uint8_t> EncodeAccountData(const AccountData& account);

std::optional<AccountData> DecodeAccountData(const std::vector<uint8_t>& data);

}  // namespace mir2::db

#endif  // MIR2_STORAGE_ENGINE_BACKENDS_COMMON_ACCOUNT_STORAGE_CODEC_H_
