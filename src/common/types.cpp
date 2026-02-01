/**
 * @file types.cpp
 * @brief Legend2 通用类型实现
 * 
 * 本文件提供通用类型的实现，包括：
 * - 版本信息获取函数
 * - 未来可能需要的非内联函数实现
 * 
 * 注意：大部分类型定义在 types.h 中作为头文件实现。
 */

#include "types.h"
#include "version.h"

namespace mir2::common {

// 大部分类型是仅头文件实现，但此文件存在的原因：
// 1. 确保库至少有一个编译单元
// 2. 未来可能需要在 .cpp 文件中实现的函数

/**
 * @brief 获取版本号
 * @return 版本字符串
 */
const char* get_version() {
    return MIR2_VERSION;
}

/**
 * @brief 获取构建日期
 * @return 构建日期字符串
 */
const char* get_build_date() {
    return __DATE__;
}

} // namespace mir2::common
