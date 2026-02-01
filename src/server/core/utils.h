/**
 * @file utils.h
 * @brief 核心工具函数
 */

#ifndef MIR2_CORE_UTILS_H
#define MIR2_CORE_UTILS_H

#include <cstdint>
#include <string>

namespace mir2::core {

/**
 * @brief 获取当前时间戳（毫秒）
 * @return 当前时间戳（ms）
 */
int64_t GetCurrentTimestampMs();

/**
 * @brief 转为小写字符串
 * @param input 输入字符串
 * @return 小写结果
 */
std::string ToLower(const std::string& input);

}  // namespace mir2::core

#endif  // MIR2_CORE_UTILS_H
