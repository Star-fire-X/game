#include "ui/input_validation.h"

#include <cctype>
#include <cstdint>

namespace mir2::ui::screens {

namespace {
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

size_t utf8_length_internal(const char* text) {
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
} // namespace

size_t utf8_length(const char* text) {
    return utf8_length_internal(text);
}

bool is_valid_utf8(const char* text) {
    if (!text) {
        return false;
    }
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
    while (*p) {
        unsigned char lead = *p;
        if (lead <= 0x7F) {
            ++p;
            continue;
        }

        int len = 0;
        if ((lead & 0xE0) == 0xC0) {
            if (lead < 0xC2) {
                return false; // Overlong encoding
            }
            len = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            len = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            if (lead > 0xF4) {
                return false; // > U+10FFFF
            }
            len = 4;
        } else {
            return false;
        }

        for (int i = 1; i < len; ++i) {
            if (p[i] == 0) {
                return false;
            }
            if ((p[i] & 0xC0) != 0x80) {
                return false;
            }
        }

        if (len == 3) {
            const unsigned char b1 = p[0];
            const unsigned char b2 = p[1];
            if (b1 == 0xE0 && b2 < 0xA0) {
                return false; // Overlong encoding
            }
            if (b1 == 0xED && b2 >= 0xA0) {
                return false; // UTF-16 surrogate
            }
        } else if (len == 4) {
            const unsigned char b1 = p[0];
            const unsigned char b2 = p[1];
            if (b1 == 0xF0 && b2 < 0x90) {
                return false; // Overlong encoding
            }
            if (b1 == 0xF4 && b2 > 0x8F) {
                return false; // > U+10FFFF
            }
        }

        p += len;
    }
    return true;
}

ValidationResult validate_character_name(const std::string& name) {
    if (!is_valid_utf8(name.c_str())) {
        return {false, "Character name encoding is invalid"};
    }

    const size_t length = utf8_length(name.c_str());
    if (length < 2 || length > 12) {
        return {false, "Character name must be 2-12 characters"};
    }

    size_t i = 0;
    while (i < name.size()) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if ((c & 0x80) == 0) {
            if (i == 0 && std::isdigit(c)) {
                return {false, "Character name cannot start with a digit"};
            }
            if (!(std::isalnum(c) || c == '_')) {
                return {false, "Character name can only contain letters, numbers, underscore, and Chinese characters"};
            }
            i += 1;
            continue;
        }

        if ((c & 0xE0) == 0xC0) {
            return {false, "Character name can only contain letters, numbers, underscore, and Chinese characters"};
        }
        if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= name.size()) {
                return {false, "Character name contains invalid characters"};
            }
            const unsigned char c1 = static_cast<unsigned char>(name[i]);
            const unsigned char c2 = static_cast<unsigned char>(name[i + 1]);
            const unsigned char c3 = static_cast<unsigned char>(name[i + 2]);

            const uint32_t codepoint = ((c1 & 0x0F) << 12)
                | ((c2 & 0x3F) << 6)
                | (c3 & 0x3F);
            if (codepoint < 0x4E00 || codepoint > 0x9FFF) {
                return {false, "Character name can only contain letters, numbers, underscore, and Chinese characters"};
            }
            i += 3;
            continue;
        }

        return {false, "Character name can only contain letters, numbers, underscore, and Chinese characters"};
    }

    return {true, ""};
}

} // namespace mir2::ui::screens
