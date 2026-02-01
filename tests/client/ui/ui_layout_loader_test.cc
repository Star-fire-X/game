#include <gtest/gtest.h>

#include "ui/ui_layout_loader.h"

#include <filesystem>
#include <fstream>
#include <system_error>

using namespace mir2::ui;

class UILayoutLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_file_ = std::filesystem::temp_directory_path() / "test_layout.json";
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(test_file_, ec);
    }

    std::filesystem::path test_file_;
};

TEST_F(UILayoutLoaderTest, LoadValidJSON) {
    std::ofstream out(test_file_);
    out << R"({
        "screens": {
            "test": {
                "design_resolution": {"width": 800, "height": 600},
                "controls": {
                    "button1": {
                        "type": "button",
                        "bounds": {"mode": "absolute", "x": 100, "y": 100, "w": 200, "h": 50}
                    }
                }
            }
        }
    })";
    out.close();

    UILayoutLoader loader;
    EXPECT_TRUE(loader.load_from_file(test_file_.string()));
    EXPECT_TRUE(loader.loaded());

    const auto* screen = loader.get_screen("test");
    ASSERT_NE(screen, nullptr);
    EXPECT_EQ(screen->design_width, 800);
    EXPECT_EQ(screen->design_height, 600);
}

TEST_F(UILayoutLoaderTest, LoadFailurePreservesData) {
    // 先加载有效数据
    std::ofstream out(test_file_);
    out << R"({"screens": {"test": {"controls": {}}}})";
    out.close();

    UILayoutLoader loader;
    EXPECT_TRUE(loader.load_from_file(test_file_.string()));

    // 尝试加载无效文件
    EXPECT_FALSE(loader.load_from_file("/nonexistent/file.json"));

    // 验证原数据仍存在
    EXPECT_TRUE(loader.loaded());
    EXPECT_NE(loader.get_screen("test"), nullptr);
}

TEST_F(UILayoutLoaderTest, InvalidJSONFormat) {
    std::ofstream out(test_file_);
    out << "invalid json {{{";
    out.close();

    UILayoutLoader loader;
    EXPECT_FALSE(loader.load_from_file(test_file_.string()));
    EXPECT_FALSE(loader.loaded());
}
