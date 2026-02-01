/**
 * @file character_data.h
 * @brief Legend2 角色数据与验证定义
 *
 * 本文件包含角色数据结构和验证逻辑，包括：
 * - 角色数据结构（用于持久化存储）
 * - 角色验证与创建请求结构
 */

#ifndef LEGEND2_CHARACTER_DATA_H
#define LEGEND2_CHARACTER_DATA_H

#include "common/types.h"
#include <cstdint>
#include <string>

namespace mir2::common {

// =============================================================================
// 角色数据结构 (Character Data - 用于持久化)
// =============================================================================

/**
 * @brief 角色数据结构，用于数据库存储
 *
 * 包含角色的所有可持久化数据，支持JSON序列化。
 */
struct CharacterData {
    uint32_t id = 0;                    ///< 角色唯一ID
    std::string account_id;             ///< 所属账号ID
    std::string name;                   ///< 角色名称
    CharacterClass char_class = CharacterClass::WARRIOR;  ///< 职业
    Gender gender = Gender::MALE;       ///< 性别
    CharacterStats stats;               ///< 角色属性
    uint32_t map_id = 1;                ///< 当前地图ID
    Position position = {100, 100};     ///< 当前位置
    std::string equipment_json = "{}";  ///< 装备数据（JSON格式）
    std::string inventory_json = "[]";  ///< 背包数据（JSON格式）
    std::string skills_json = "[]";     ///< 技能数据（JSON格式）
    int64_t created_at = 0;             ///< 创建时间戳
    int64_t last_login = 0;             ///< 最后登录时间戳

    bool operator==(const CharacterData& other) const {
        return id == other.id &&
               account_id == other.account_id &&
               name == other.name &&
               char_class == other.char_class &&
               gender == other.gender &&
               stats == other.stats &&
               map_id == other.map_id &&
               position == other.position &&
               equipment_json == other.equipment_json &&
               inventory_json == other.inventory_json &&
               skills_json == other.skills_json &&
               created_at == other.created_at &&
               last_login == other.last_login;
    }

    /// 序列化为JSON字符串
    std::string serialize() const;

    /// 从JSON字符串反序列化
    static CharacterData deserialize(const std::string& json_str);
};

// CharacterData的JSON序列化函数
void to_json(nlohmann::json& j, const CharacterData& data);
void from_json(const nlohmann::json& j, CharacterData& data);

// =============================================================================
// 角色验证 (Character Validation)
// =============================================================================

/**
 * @brief 角色创建验证结果
 */
struct CharacterValidationResult {
    bool valid = false;                          ///< 是否有效
    ErrorCode error_code = ErrorCode::SUCCESS;   ///< 错误码
    std::string error_message;                   ///< 错误信息

    /// 创建成功结果
    static CharacterValidationResult success() {
        return {true, ErrorCode::SUCCESS, ""};
    }

    /// 创建失败结果
    static CharacterValidationResult failure(ErrorCode code, const std::string& msg) {
        return {false, code, msg};
    }
};

/**
 * @brief 验证角色名称
 *
 * 规则：非空，2-12个字符，仅允许字母数字和中文字符
 * @param name 角色名称
 * @return 验证结果
 */
CharacterValidationResult validate_character_name(const std::string& name);

/**
 * @brief 验证角色职业
 * @param char_class 职业类型
 * @return 验证结果
 */
CharacterValidationResult validate_character_class(CharacterClass char_class);

/**
 * @brief 检查职业值是否有效
 * @param class_value 职业值
 * @return 是否有效
 */
bool is_valid_character_class(uint8_t class_value);

// =============================================================================
// 角色创建 (Character Creation)
// =============================================================================

/**
 * @brief 创建新角色的请求
 */
struct CharacterCreateRequest {
    std::string account_id;                              ///< 账号ID
    std::string name;                                    ///< 角色名称
    CharacterClass char_class = CharacterClass::WARRIOR; ///< 职业
    Gender gender = Gender::MALE;                        ///< 性别
};

/**
 * @brief 角色创建结果
 */
struct CharacterCreateResult {
    bool success = false;                        ///< 是否成功
    ErrorCode error_code = ErrorCode::SUCCESS;   ///< 错误码
    std::string error_message;                   ///< 错误信息
    uint32_t character_id = 0;                   ///< 角色ID（成功时设置）

    /// 创建成功结果
    static CharacterCreateResult ok(uint32_t id) {
        return {true, ErrorCode::SUCCESS, "", id};
    }

    /// 创建失败结果
    static CharacterCreateResult error(ErrorCode code, const std::string& msg) {
        return {false, code, msg, 0};
    }
};

/**
 * @brief 验证角色创建请求（不检查数据库）
 *
 * 验证名称格式和职业类型
 * @param request 创建请求
 * @return 验证结果
 */
CharacterValidationResult validate_character_create_request(const CharacterCreateRequest& request);

} // namespace mir2::common

#endif // LEGEND2_CHARACTER_DATA_H
