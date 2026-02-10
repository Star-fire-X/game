#include "storage_engine/backends/common/account_storage_codec.h"

#include <string_view>

#include <nlohmann/json.hpp>

namespace mir2::db {

namespace {
constexpr std::string_view kAccountKeyPrefixView{kAccountKeyPrefix};
}

std::string BuildAccountStorageKey(const std::string& username) {
  return std::string(kAccountKeyPrefix) + username;
}

std::optional<std::string> ParseAccountStorageKey(const std::string& key) {
  std::string_view view(key);
  if (!view.starts_with(kAccountKeyPrefixView)) {
    return std::nullopt;
  }

  auto suffix = view.substr(kAccountKeyPrefixView.size());
  if (suffix.empty()) {
    return std::nullopt;
  }

  return std::string(suffix);
}

std::vector<uint8_t> EncodeAccountData(const AccountData& account) {
  nlohmann::json payload = {
      {"id", account.id},
      {"username", account.username},
      {"password_hash", account.password_hash},
      {"email", account.email},
      {"created_at", account.created_at},
      {"last_login", account.last_login},
      {"banned", account.banned}};

  const std::string serialized = payload.dump();
  return std::vector<uint8_t>(serialized.begin(), serialized.end());
}

std::optional<AccountData> DecodeAccountData(const std::vector<uint8_t>& data) {
  if (data.empty()) {
    return std::nullopt;
  }

  try {
    std::string serialized(data.begin(), data.end());
    const auto payload = nlohmann::json::parse(serialized);

    AccountData account;
    account.id = payload.value("id", 0ULL);
    account.username = payload.value("username", std::string{});
    account.password_hash = payload.value("password_hash", std::string{});
    account.email = payload.value("email", std::string{});
    account.created_at = payload.value("created_at", 0LL);
    account.last_login = payload.value("last_login", 0LL);
    account.banned = payload.value("banned", false);

    return account;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

}  // namespace mir2::db
