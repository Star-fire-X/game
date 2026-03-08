/**
 * @file crypto_utils.cc
 * @brief 加密和验证工具函数实现
 *
 * 支持bcrypt（$2a$/$2b$前缀）和SHA-256密码验证。
 * 新密码统一使用bcrypt哈希。
 */

#include "common/crypto_utils.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <random>
#include <string_view>

#ifdef HAVE_OPENSSL
#include <openssl/rand.h>
#include <openssl/sha.h>
#endif

#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <crypt.h>
#define HAVE_SYSTEM_CRYPT 1
#endif

namespace mir2::common {

namespace {
constexpr size_t kTokenBytes = 32;
constexpr char kHexChars[] = "0123456789abcdef";

// bcrypt uses a custom base64 alphabet (different from standard base64)
constexpr char kBcryptBase64[] =
    "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

std::string ToLowerHex(std::string value) {
  for (char& c : value) {
    if (c >= 'A' && c <= 'F') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return value;
}

// Encode raw bytes to bcrypt's custom base64 (22 chars from 16 bytes)
std::string BcryptBase64Encode(const uint8_t* data, size_t len) {
  std::string result;
  result.reserve((len * 4 + 2) / 3);

  size_t i = 0;
  while (i < len) {
    uint32_t c1 = data[i++];
    result.push_back(kBcryptBase64[(c1 >> 2) & 0x3F]);

    uint32_t c2 = (i < len) ? data[i++] : 0;
    result.push_back(kBcryptBase64[((c1 & 0x03) << 4) | ((c2 >> 4) & 0x0F)]);

    if (i > len) break;

    uint32_t c3 = (i < len) ? data[i++] : 0;
    result.push_back(kBcryptBase64[((c2 & 0x0F) << 2) | ((c3 >> 6) & 0x03)]);

    if (i > len) break;

    result.push_back(kBcryptBase64[c3 & 0x3F]);
  }

  return result;
}

#ifdef HAVE_OPENSSL
std::string Sha256Hex(const std::string& input) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(),
         hash);
  return HexEncode(hash, SHA256_DIGEST_LENGTH);
}
#endif

bool VerifyPasswordSha256(const std::string& password,
                          const std::string& stored_hash) {
#ifdef HAVE_OPENSSL
  if (stored_hash.empty()) {
    return false;
  }

  std::string salt;
  std::string expected;

  if (stored_hash.rfind("sha256$", 0) == 0) {
    const std::string rest = stored_hash.substr(7);
    const size_t sep = rest.find('$');
    if (sep == std::string::npos) {
      return false;
    }
    salt = rest.substr(0, sep);
    expected = rest.substr(sep + 1);
  } else {
    size_t sep = stored_hash.find('$');
    if (sep == std::string::npos) {
      sep = stored_hash.find(':');
    }
    if (sep != std::string::npos) {
      salt = stored_hash.substr(0, sep);
      expected = stored_hash.substr(sep + 1);
    } else {
      expected = stored_hash;
    }
  }

  if (expected.empty()) {
    return false;
  }

  const std::string computed = Sha256Hex(salt + password);
  return ConstantTimeEquals(ToLowerHex(computed), ToLowerHex(expected));
#else
  (void)password;
  (void)stored_hash;
  return false;
#endif
}

bool VerifyPasswordBcrypt(const std::string& password,
                          const std::string& stored_hash) {
#ifdef HAVE_SYSTEM_CRYPT
  struct crypt_data data;
  std::memset(&data, 0, sizeof(data));

  const char* result =
      crypt_r(password.c_str(), stored_hash.c_str(), &data);
  if (!result) {
    return false;
  }

  // Use constant-time comparison to prevent timing attacks
  return ConstantTimeEquals(result, stored_hash);
#else
  (void)password;
  (void)stored_hash;
  return false;
#endif
}

}  // namespace

std::string HexEncode(const uint8_t* data, size_t size) {
  std::string result;
  result.reserve(size * 2);
  for (size_t i = 0; i < size; ++i) {
    result.push_back(kHexChars[data[i] >> 4]);
    result.push_back(kHexChars[data[i] & 0x0F]);
  }
  return result;
}

std::string GenerateSecureToken() {
  std::array<uint8_t, kTokenBytes> bytes{};
#ifdef HAVE_OPENSSL
  if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
    // RAND_bytes failed; fall back to std::random_device
    std::random_device rd;
    for (auto& b : bytes) {
      b = static_cast<uint8_t>(rd() & 0xFF);
    }
  }
#else
  std::random_device rd;
  for (auto& b : bytes) {
    b = static_cast<uint8_t>(rd() & 0xFF);
  }
#endif
  return HexEncode(bytes.data(), bytes.size());
}

bool ConstantTimeEquals(std::string_view left, std::string_view right) {
  const size_t max_len = std::max(left.size(), right.size());
  unsigned char diff =
      static_cast<unsigned char>(left.size() ^ right.size());
  for (size_t i = 0; i < max_len; ++i) {
    const unsigned char left_ch =
        i < left.size() ? static_cast<unsigned char>(left[i]) : 0U;
    const unsigned char right_ch =
        i < right.size() ? static_cast<unsigned char>(right[i]) : 0U;
    diff |= left_ch ^ right_ch;
  }
  return diff == 0;
}

bool VerifyPassword(const std::string& password,
                    const std::string& stored_hash) {
  if (stored_hash.empty()) {
    return false;
  }

  // bcrypt hashes start with $2a$, $2b$, or $2y$
  if (stored_hash.rfind("$2a$", 0) == 0 ||
      stored_hash.rfind("$2b$", 0) == 0 ||
      stored_hash.rfind("$2y$", 0) == 0) {
    return VerifyPasswordBcrypt(password, stored_hash);
  }

  // Legacy SHA-256 hashes
  return VerifyPasswordSha256(password, stored_hash);
}

std::string HashPassword(const std::string& password, int cost) {
  if (password.empty() || cost < 4 || cost > 31) {
    return {};
  }

#if defined(HAVE_OPENSSL) && defined(HAVE_SYSTEM_CRYPT)
  // Generate 16 random bytes for the salt
  uint8_t salt_bytes[16];
  if (RAND_bytes(salt_bytes, sizeof(salt_bytes)) != 1) {
    return {};
  }

  // Encode salt to bcrypt base64 (22 characters from 16 bytes)
  std::string salt_b64 = BcryptBase64Encode(salt_bytes, sizeof(salt_bytes));
  if (salt_b64.size() > 22) {
    salt_b64.resize(22);
  }

  // Build the bcrypt salt string: $2b$CC$<22 chars>
  char salt_str[32];
  std::snprintf(salt_str, sizeof(salt_str), "$2b$%02d$%s", cost,
                salt_b64.c_str());

  struct crypt_data data;
  std::memset(&data, 0, sizeof(data));

  const char* result = crypt_r(password.c_str(), salt_str, &data);
  if (!result || result[0] == '*') {
    return {};
  }

  return std::string(result);
#else
  (void)password;
  (void)cost;
  return {};
#endif
}

}  // namespace mir2::common
