/**
 * @file types.h
 * @brief Legend2 通用类型定义 (聚合头文件)
 *
 * 本文件包含客户端和服务器共享的核心类型定义。
 * 实际定义已拆分到子模块，此文件用于统一包含。
 *
 * 子模块:
 *   - types/types.h          基础数据结构（Position, Color, Rect, Size）
 *   - enums.h                公共枚举类型
 *   - types/error_codes.h    错误码定义
 *   - types/constants.h      游戏常量
 *   - types/character_stats.h 角色属性结构
 *   - protocol/message_types.h 网络消息类型
 */

#ifndef LEGEND2_COMMON_TYPES_H
#define LEGEND2_COMMON_TYPES_H

// 基础类型和JSON序列化
#include "types/types.h"

// 游戏枚举
#include "enums.h"

// 错误码
#include "types/error_codes.h"

// 常量
#include "types/constants.h"

// 角色属性
#include "types/character_stats.h"

// 网络消息类型
#include "protocol/message_types.h"

// 技能结果
#include "protocol/skill_result.h"

// 保留原有的 include 依赖
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#endif // LEGEND2_COMMON_TYPES_H
