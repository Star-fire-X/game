/**
 * @file character_data.cpp
 * @brief Legend2 角色数据与验证实现
 */

#include "common/character_data.h"

#include <cctype>

namespace mir2::common {

// =============================================================================
// CharacterData 实现 - 用于数据库持久化的角色数据结构
// =============================================================================

/**
 * @brief 将角色数据序列化为JSON字符串
 *
 * 用于将角色数据保存到数据库或通过网络传输。
 *
 * @return JSON格式的字符串
 */
std::string CharacterData::serialize() const {
    nlohmann::json j;
    to_json(j, *this);
    return j.dump();
}

/**
 * @brief 从JSON字符串反序列化角色数据
 *
 * 用于从数据库加载角色数据或解析网络消息。
 *
 * @param json_str JSON格式的字符串
 * @return 解析后的CharacterData对象
 * @throws nlohmann::json::exception 如果JSON格式无效
 */
CharacterData CharacterData::deserialize(const std::string& json_str) {
    CharacterData data;
    nlohmann::json j = nlohmann::json::parse(json_str);
    from_json(j, data);
    return data;
}

/**
 * @brief CharacterData的JSON序列化函数
 *
 * 将CharacterData对象转换为nlohmann::json对象。
 * 枚举类型转换为uint8_t以确保跨平台兼容性。
 *
 * @param j 输出的JSON对象
 * @param data 要序列化的角色数据
 */
void to_json(nlohmann::json& j, const CharacterData& data) {
    j = nlohmann::json{
        {"id", data.id},
        {"account_id", data.account_id},
        {"name", data.name},
        {"char_class", static_cast<uint8_t>(data.char_class)},
        {"gender", static_cast<uint8_t>(data.gender)},
        {"stats", data.stats},
        {"map_id", data.map_id},
        {"position", data.position},
        {"equipment_json", data.equipment_json},
        {"inventory_json", data.inventory_json},
        {"skills_json", data.skills_json},
        {"created_at", data.created_at},
        {"last_login", data.last_login}
    };
}

/**
 * @brief CharacterData的JSON反序列化函数
 *
 * 从nlohmann::json对象解析CharacterData。
 *
 * @param j 输入的JSON对象
 * @param data 输出的角色数据
 * @throws nlohmann::json::exception 如果缺少必需字段
 */
void from_json(const nlohmann::json& j, CharacterData& data) {
    j.at("id").get_to(data.id);
    j.at("account_id").get_to(data.account_id);
    j.at("name").get_to(data.name);
    data.char_class = static_cast<CharacterClass>(j.at("char_class").get<uint8_t>());
    data.gender = static_cast<Gender>(j.at("gender").get<uint8_t>());
    j.at("stats").get_to(data.stats);
    j.at("map_id").get_to(data.map_id);
    j.at("position").get_to(data.position);
    j.at("equipment_json").get_to(data.equipment_json);
    j.at("inventory_json").get_to(data.inventory_json);
    j.at("skills_json").get_to(data.skills_json);
    j.at("created_at").get_to(data.created_at);
    j.at("last_login").get_to(data.last_login);
}

// =============================================================================
// 角色验证函数 - 用于验证角色创建请求
// =============================================================================

/**
 * @brief 验证角色名称是否合法
 *
 * 验证规则：
 * 1. 名称不能为空
 * 2. 长度必须在2-12个字符之间（支持中文）
 * 3. 只允许字母、数字和中文字符（CJK统一汉字 U+4E00-U+9FFF）
 *
 * @param name 要验证的角色名称
 * @return 验证结果，包含是否有效和错误信息
 */
CharacterValidationResult validate_character_name(const std::string& name) {
    // 检查是否为空
    if (name.empty()) {
        return CharacterValidationResult::failure(
            ErrorCode::INVALID_CHARACTER_NAME,
            "Character name cannot be empty"
        );
    }

    // 计算实际字符数（UTF-8编码下中文字符占3字节）
    size_t char_count = 0;
    for (size_t i = 0; i < name.size(); ) {
        unsigned char c = name[i];
        if ((c & 0x80) == 0) {
            // ASCII字符（单字节）
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            // 2字节UTF-8
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            // 3字节UTF-8（中文字符）
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            // 4字节UTF-8
            i += 4;
        } else {
            // 无效的UTF-8编码
            return CharacterValidationResult::failure(
                ErrorCode::INVALID_CHARACTER_NAME,
                "Character name contains invalid characters"
            );
        }
        char_count++;
    }

    // 检查长度下限
    if (char_count < 2) {
        return CharacterValidationResult::failure(
            ErrorCode::INVALID_CHARACTER_NAME,
            "Character name must be at least 2 characters"
        );
    }

    // 检查长度上限
    if (char_count > 12) {
        return CharacterValidationResult::failure(
            ErrorCode::INVALID_CHARACTER_NAME,
            "Character name cannot exceed 12 characters"
        );
    }

    // 检查字符合法性：只允许字母、数字和中文
    for (size_t i = 0; i < name.size(); ) {
        unsigned char c = name[i];

        if ((c & 0x80) == 0) {
            // ASCII字符 - 必须是字母或数字
            if (!std::isalnum(c)) {
                return CharacterValidationResult::failure(
                    ErrorCode::INVALID_CHARACTER_NAME,
                    "Character name can only contain letters, numbers, and Chinese characters"
                );
            }
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            // 2字节UTF-8 - 不是中文，拒绝
            return CharacterValidationResult::failure(
                ErrorCode::INVALID_CHARACTER_NAME,
                "Character name can only contain letters, numbers, and Chinese characters"
            );
        } else if ((c & 0xF0) == 0xE0) {
            // 3字节UTF-8 - 可能是中文
            // 中文字符范围：U+4E00-U+9FFF
            // UTF-8编码：E4 B8 80 到 E9 BF BF
            if (i + 2 >= name.size()) {
                return CharacterValidationResult::failure(
                    ErrorCode::INVALID_CHARACTER_NAME,
                    "Character name contains invalid characters"
                );
            }

            unsigned char c1 = name[i];
            unsigned char c2 = name[i + 1];
            unsigned char c3 = name[i + 2];

            // 解码UTF-8为Unicode码点
            uint32_t codepoint = ((c1 & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);

            // 检查是否在CJK统一汉字范围内
            if (codepoint < 0x4E00 || codepoint > 0x9FFF) {
                return CharacterValidationResult::failure(
                    ErrorCode::INVALID_CHARACTER_NAME,
                    "Character name can only contain letters, numbers, and Chinese characters"
                );
            }

            i += 3;
        } else {
            // 4字节UTF-8或无效编码 - 拒绝
            return CharacterValidationResult::failure(
                ErrorCode::INVALID_CHARACTER_NAME,
                "Character name can only contain letters, numbers, and Chinese characters"
            );
        }
    }

    return CharacterValidationResult::success();
}

/**
 * @brief 验证角色职业是否有效
 *
 * @param char_class 要验证的职业类型
 * @return 验证结果
 */
CharacterValidationResult validate_character_class(CharacterClass char_class) {
    if (!is_valid_character_class(static_cast<uint8_t>(char_class))) {
        return CharacterValidationResult::failure(
            ErrorCode::INVALID_CHARACTER_CLASS,
            "Invalid character class"
        );
    }
    return CharacterValidationResult::success();
}

/**
 * @brief 检查职业值是否在有效范围内
 *
 * @param class_value 职业的数值表示
 * @return true如果有效（0=战士, 1=法师, 2=道士）
 */
bool is_valid_character_class(uint8_t class_value) {
    return class_value <= static_cast<uint8_t>(CharacterClass::TAOIST);
}

// =============================================================================
// 角色创建函数
// =============================================================================

/**
 * @brief 验证角色创建请求
 *
 * 验证请求中的所有字段是否合法，但不检查数据库中的重复性。
 *
 * @param request 角色创建请求
 * @return 验证结果
 */
CharacterValidationResult validate_character_create_request(const CharacterCreateRequest& request) {
    // 验证角色名称
    auto name_result = validate_character_name(request.name);
    if (!name_result.valid) {
        return name_result;
    }

    // 验证职业
    auto class_result = validate_character_class(request.char_class);
    if (!class_result.valid) {
        return class_result;
    }

    // 验证账号ID不为空
    if (request.account_id.empty()) {
        return CharacterValidationResult::failure(
            ErrorCode::INVALID_CREDENTIALS,
            "Account ID cannot be empty"
        );
    }

    return CharacterValidationResult::success();
}

} // namespace mir2::common
