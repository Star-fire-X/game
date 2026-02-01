#include <gtest/gtest.h>

#include "ui/input_validation.h"

using namespace mir2::ui::screens;

TEST(InputValidation, ValidUTF8ASCII) {
    EXPECT_TRUE(is_valid_utf8("Hello"));
    EXPECT_TRUE(is_valid_utf8("Player123"));
}

TEST(InputValidation, ValidUTF8Chinese) {
    EXPECT_TRUE(is_valid_utf8("世界"));
    EXPECT_TRUE(is_valid_utf8("Hello世界"));
    EXPECT_TRUE(is_valid_utf8("玩家一号"));
}

TEST(InputValidation, InvalidUTF8) {
    EXPECT_FALSE(is_valid_utf8("\xFF\xFE"));
    EXPECT_FALSE(is_valid_utf8("\xC0\x80"));     // Overlong
    EXPECT_FALSE(is_valid_utf8("\xED\xA0\x80")); // Surrogate
}

TEST(InputValidation, UTF8Nullptr) {
    EXPECT_FALSE(is_valid_utf8(nullptr));
}

TEST(InputValidation, CharacterNameValid) {
    auto result = validate_character_name("Player1");
    EXPECT_TRUE(result.valid);

    result = validate_character_name("玩家一号");
    EXPECT_TRUE(result.valid);

    result = validate_character_name("Test_User");
    EXPECT_TRUE(result.valid);
}

TEST(InputValidation, CharacterNameTooShort) {
    auto result = validate_character_name("A");
    EXPECT_FALSE(result.valid);
    EXPECT_FALSE(result.error_message.empty());
}

TEST(InputValidation, CharacterNameTooLong) {
    auto result = validate_character_name("VeryLongName123456");
    EXPECT_FALSE(result.valid);
}

TEST(InputValidation, CharacterNameInvalidChars) {
    auto result = validate_character_name("Player@123");
    EXPECT_FALSE(result.valid);

    result = validate_character_name("Test User"); // Space
    EXPECT_FALSE(result.valid);
}

TEST(InputValidation, CharacterNameStartsWithDigit) {
    auto result = validate_character_name("123Player");
    EXPECT_FALSE(result.valid);
}
