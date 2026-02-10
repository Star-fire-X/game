/**
 * @file crypto_utils.h
 * @brief 加密和验证工具函数
 */

#ifndef MIR2_SERVER_COMMON_CRYPTO_UTILS_H
#define MIR2_SERVER_COMMON_CRYPTO_UTILS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace mir2::common {

/**
 * @brief 将二进制数据编码为十六进制字符串
 */
std::string HexEncode(const uint8_t* data, size_t size);

/**
 * @brief 生成安全的随机令牌（64字节十六进制）
 */
std::string GenerateSecureToken();

/**
 * @brief 常量时间字符串比较（防止时序攻击）
 */
bool ConstantTimeEquals(std::string_view left, std::string_view right);

/**
 * @brief 验证密码哈希（支持bcrypt和SHA256格式）
 * @param password 明文密码
 * @param stored_hash 存储的哈希值（$2a$/$2b$ 前缀为bcrypt，否则SHA256）
 * @return 验证是否通过
 */
bool VerifyPassword(const std::string& password, const std::string& stored_hash);

/**
 * @brief 使用bcrypt对密码进行哈希（用于新账号注册）
 * @param password 明文密码
 * @param cost bcrypt cost factor（默认12，范围4-31）
 * @return bcrypt哈希字符串（$2a$...格式），失败返回空字符串
 */
std::string HashPassword(const std::string& password, int cost = 12);

}  // namespace mir2::common

#endif  // MIR2_SERVER_COMMON_CRYPTO_UTILS_H
