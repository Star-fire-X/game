#include "ui/ui_layout_loader.h"
#include "ui/ui_layout_calculator.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>

namespace mir2::ui {

UILayoutLoader::UILayoutLoader() = default;

bool UILayoutLoader::load_from_file(const std::string& path) {
    std::unordered_map<std::string, ScreenLayout> temp_screens;

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[UILayoutLoader] Unable to open layout file '" << path
                  << "': " << std::strerror(errno) << "\n";
        return false;
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[UILayoutLoader] Failed to parse JSON in '" << path
                  << "': " << e.what() << "\n";
        return false;
    }

    auto screens_it = j.find("screens");
    if (screens_it == j.end()) {
        std::cerr << "[UILayoutLoader] Missing 'screens' object in layout file: " << path << "\n";
        return false;
    }
    if (!screens_it->is_object()) {
        std::cerr << "[UILayoutLoader] Layout file 'screens' is not an object (type="
                  << screens_it->type_name() << "): " << path << "\n";
        return false;
    }

    for (auto& [screen_name, screen_json] : screens_it->items()) {
        if (!screen_json.is_object()) {
            std::cerr << "[UILayoutLoader] Skipping screen '" << screen_name
                      << "' because it is not an object in '" << path << "'\n";
            continue;
        }

        ScreenLayout screen_layout;

        if (auto design_it = screen_json.find("design_resolution"); design_it != screen_json.end() && design_it->is_object()) {
            int w = design_it->value("width", screen_layout.design_width);
            int h = design_it->value("height", screen_layout.design_height);
            if (w > 0 && h > 0) {
                screen_layout.design_width = w;
                screen_layout.design_height = h;
            }
        }

        if (auto bg_it = screen_json.find("background"); bg_it != screen_json.end()) {
            if (auto background = parse_background(*bg_it)) {
                screen_layout.background = *background;
            }
        }

        if (auto controls_it = screen_json.find("controls"); controls_it != screen_json.end() && controls_it->is_object()) {
            for (auto& [control_name, control_json] : controls_it->items()) {
                if (auto control = parse_control(control_json)) {
                    screen_layout.controls.emplace(control_name, std::move(*control));
                }
            }
        }

        temp_screens.emplace(screen_name, std::move(screen_layout));
    }

    if (temp_screens.empty()) {
        std::cerr << "[UILayoutLoader] No valid screens parsed from layout file: " << path << "\n";
        return false;
    }

    screens_ = std::move(temp_screens);
    loaded_ = true;
    return true;
}

const UILayoutLoader::ScreenLayout* UILayoutLoader::get_screen(const std::string& name) const {
    auto it = screens_.find(name);
    if (it == screens_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::optional<Rect> UILayoutLoader::resolve_control_rect(const std::string& screen_name,
                                                         const std::string& control_name,
                                                         int target_w,
                                                         int target_h) const {
    auto screen_it = screens_.find(screen_name);
    if (screen_it == screens_.end()) {
        return std::nullopt;
    }

    const auto& screen = screen_it->second;
    auto control_it = screen.controls.find(control_name);
    if (control_it == screen.controls.end()) {
        return std::nullopt;
    }

    return resolve_bounds(control_it->second.bounds, target_w, target_h, screen.design_width, screen.design_height);
}

UILayoutLoader::LayoutBounds UILayoutLoader::parse_bounds(const nlohmann::json& j) {
    LayoutBounds bounds;
    std::string mode = j.value("mode", "absolute");
    if (mode == "relative") {
        bounds.mode = LayoutBounds::Mode::RELATIVE;
        bounds.relative.x = j.value("x", 0.0);
        bounds.relative.y = j.value("y", 0.0);
        bounds.relative.w = j.value("w", 0.0);
        bounds.relative.h = j.value("h", 0.0);
    } else {
        bounds.mode = LayoutBounds::Mode::ABSOLUTE;
        bounds.absolute.x = j.value("x", 0);
        bounds.absolute.y = j.value("y", 0);
        bounds.absolute.width = j.value("w", 0);
        bounds.absolute.height = j.value("h", 0);
    }
    return bounds;
}

std::optional<UILayoutLoader::Control> UILayoutLoader::parse_control(const nlohmann::json& j) {
    if (!j.is_object()) {
        return std::nullopt;
    }

    auto bounds_it = j.find("bounds");
    if (bounds_it == j.end() || !bounds_it->is_object()) {
        return std::nullopt;
    }

    Control control;
    control.type = j.value("type", "unknown");
    control.bounds = parse_bounds(*bounds_it);

    const bool has_area = (control.bounds.mode == LayoutBounds::Mode::ABSOLUTE)
        ? (control.bounds.absolute.width > 0 && control.bounds.absolute.height > 0)
        : (control.bounds.relative.w > 0.0 && control.bounds.relative.h > 0.0);

    if (!has_area) {
        return std::nullopt;
    }

    return control;
}

std::optional<UILayoutLoader::Background> UILayoutLoader::parse_background(const nlohmann::json& j) {
    if (!j.is_object()) {
        return std::nullopt;
    }

    Background background;
    background.wil_file = j.value("wil_file", "");
    background.frame_index = j.value("frame_index", 0);

    if (background.wil_file.empty()) {
        return std::nullopt;
    }

    return background;
}

Rect UILayoutLoader::scale_from_absolute(const Rect& design_rect, int design_w, int design_h, int target_w, int target_h) {
    UILayoutCalculator calc(design_w, design_h, target_w, target_h);
    return calc.scale_rect(design_rect);
}

Rect UILayoutLoader::scale_from_relative(const LayoutBounds::RelativeBounds& rel, int target_w, int target_h) {
    auto clamp01 = [](double v) -> double {
        return std::max(0.0, std::min(1.0, v));
    };

    Rect r;
    r.x = static_cast<int>(std::lround(clamp01(rel.x) * target_w));
    r.y = static_cast<int>(std::lround(clamp01(rel.y) * target_h));
    r.width = std::max(1, static_cast<int>(std::lround(clamp01(rel.w) * target_w)));
    r.height = std::max(1, static_cast<int>(std::lround(clamp01(rel.h) * target_h)));
    return r;
}

std::optional<Rect> UILayoutLoader::resolve_bounds(const LayoutBounds& bounds, int target_w, int target_h, int design_w, int design_h) const {
    if (target_w <= 0 || target_h <= 0) {
        return std::nullopt;
    }

    const int base_w = design_w > 0 ? design_w : target_w;
    const int base_h = design_h > 0 ? design_h : target_h;

    if (bounds.mode == LayoutBounds::Mode::RELATIVE) {
        return scale_from_relative(bounds.relative, target_w, target_h);
    }

    return scale_from_absolute(bounds.absolute, base_w, base_h, target_w, target_h);
}

} // namespace mir2::ui
