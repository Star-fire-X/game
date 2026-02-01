#ifndef LEGEND2_UI_LAYOUT_LOADER_H
#define LEGEND2_UI_LAYOUT_LOADER_H

#include "common/types.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>

namespace mir2::ui {

// 引入公共类型定义
using namespace mir2::common;

/// Parses UI layout JSON and resolves bounds to the current window size.
class UILayoutLoader {
public:
    struct LayoutBounds {
        enum class Mode { ABSOLUTE, RELATIVE };
        Mode mode = Mode::ABSOLUTE;
        Rect absolute;
        struct RelativeBounds {
            double x = 0.0;
            double y = 0.0;
            double w = 0.0;
            double h = 0.0;
        } relative;
    };

    struct Background {
        std::string wil_file;
        int frame_index = 0;
    };

    struct Control {
        std::string type;
        LayoutBounds bounds;
    };

    struct ScreenLayout {
        int design_width = 800;
        int design_height = 600;
        std::optional<Background> background;
        std::unordered_map<std::string, Control> controls;
    };

    UILayoutLoader();

    /// Load and parse a layout JSON file. On parse errors, keeps existing data unchanged.
    bool load_from_file(const std::string& path);
    bool loaded() const { return loaded_; }

    const ScreenLayout* get_screen(const std::string& name) const;
    std::optional<Rect> resolve_control_rect(const std::string& screen_name,
                                             const std::string& control_name,
                                             int target_w,
                                             int target_h) const;

private:
    std::unordered_map<std::string, ScreenLayout> screens_;
    bool loaded_ = false;

    static LayoutBounds parse_bounds(const nlohmann::json& j);
    static std::optional<Control> parse_control(const nlohmann::json& j);
    static std::optional<Background> parse_background(const nlohmann::json& j);
    static Rect scale_from_absolute(const Rect& design_rect, int design_w, int design_h, int target_w, int target_h);
    static Rect scale_from_relative(const LayoutBounds::RelativeBounds& rel, int target_w, int target_h);
    std::optional<Rect> resolve_bounds(const LayoutBounds& bounds, int target_w, int target_h, int design_w, int design_h) const;
};

} // namespace mir2::ui

#endif // LEGEND2_UI_LAYOUT_LOADER_H
