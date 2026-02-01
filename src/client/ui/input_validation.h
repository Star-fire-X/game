#ifndef LEGEND2_UI_INPUT_VALIDATION_H
#define LEGEND2_UI_INPUT_VALIDATION_H

#include <cstddef>
#include <string>

namespace mir2::ui::screens {

/// @brief 输入验证结果
struct ValidationResult {
    bool valid = false;
    std::string error_message;
};

/// @brief 验证角色名称
/// @param name 角色名称
/// @return 验证结果
ValidationResult validate_character_name(const std::string& name);

/// @brief 验证UTF-8字符串有效性
/// @param text UTF-8字符串
/// @return 是否为有效UTF-8编码
bool is_valid_utf8(const char* text);

/// @brief 获取UTF-8字符串的字符长度
/// @param text UTF-8字符串
/// @return 字符数量（无效编码返回0）
size_t utf8_length(const char* text);

} // namespace mir2::ui::screens

#endif // LEGEND2_UI_INPUT_VALIDATION_H
