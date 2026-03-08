#include "common/utf8_utils.h"

#include <algorithm>

namespace mir2::common {

int utf8_sequence_length(unsigned char lead) {
    if (lead <= 0x7F) {
        return 1;
    }
    if ((lead & 0xE0) == 0xC0) {
        return 2;
    }
    if ((lead & 0xF0) == 0xE0) {
        return 3;
    }
    if ((lead & 0xF8) == 0xF0) {
        return 4;
    }
    return 0;
}

size_t utf8_length(const char* text) {
    if (!text) {
        return 0;
    }
    size_t count = 0;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
    while (*p) {
        const int len = utf8_sequence_length(*p);
        if (len <= 0) {
            return 0;
        }
        p += len;
        ++count;
    }
    return count;
}

int clamp_cursor_to_boundary(const std::string& text, int pos) {
    const int size = static_cast<int>(text.size());
    if (pos < 0) {
        pos = 0;
    }
    if (pos > size) {
        pos = size;
    }
    while (pos > 0 && pos < size &&
           (static_cast<unsigned char>(text[static_cast<size_t>(pos)]) & 0xC0) == 0x80) {
        --pos;
    }
    return pos;
}

int previous_utf8_boundary(const std::string& text, int pos) {
    pos = clamp_cursor_to_boundary(text, pos);
    if (pos <= 0) {
        return 0;
    }
    --pos;
    while (pos > 0 &&
           (static_cast<unsigned char>(text[static_cast<size_t>(pos)]) & 0xC0) == 0x80) {
        --pos;
    }
    return pos;
}

int next_utf8_boundary(const std::string& text, int pos) {
    pos = clamp_cursor_to_boundary(text, pos);
    const int size = static_cast<int>(text.size());
    if (pos >= size) {
        return size;
    }
    const unsigned char lead = static_cast<unsigned char>(text[static_cast<size_t>(pos)]);
    int len = utf8_sequence_length(lead);
    if (len <= 0) {
        return std::min(size, pos + 1);
    }
    if (pos + len > size) {
        return size;
    }
    return pos + len;
}

std::string utf8_prefix(const char* text, size_t max_chars) {
    std::string result;
    if (!text || max_chars == 0) {
        return result;
    }
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
    size_t count = 0;
    while (*p && count < max_chars) {
        const int len = utf8_sequence_length(*p);
        if (len <= 0) {
            break;
        }
        result.append(reinterpret_cast<const char*>(p), static_cast<size_t>(len));
        p += len;
        ++count;
    }
    return result;
}

}  // namespace mir2::common
