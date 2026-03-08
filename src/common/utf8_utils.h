/**
 * @file utf8_utils.h
 * @brief UTF-8 utility helpers.
 */

#ifndef MIR2_COMMON_UTF8_UTILS_H
#define MIR2_COMMON_UTF8_UTILS_H

#include <cstddef>
#include <string>

namespace mir2::common {

int utf8_sequence_length(unsigned char lead);
size_t utf8_length(const char* text);

int clamp_cursor_to_boundary(const std::string& text, int pos);
int previous_utf8_boundary(const std::string& text, int pos);
int next_utf8_boundary(const std::string& text, int pos);
std::string utf8_prefix(const char* text, size_t max_chars);

}  // namespace mir2::common

#endif  // MIR2_COMMON_UTF8_UTILS_H
