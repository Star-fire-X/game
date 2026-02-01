#include "ui/ui_layout_calculator.h"

#include <algorithm>
#include <cstdint>

namespace mir2::ui {

UILayoutCalculator::UILayoutCalculator(int design_w, int design_h, int target_w, int target_h)
    : design_w_(design_w)
    , design_h_(design_h)
    , target_w_(target_w)
    , target_h_(target_h) {
    if (design_w_ > 0 && target_w_ > 0) {
        scale_x_ = static_cast<double>(target_w_) / static_cast<double>(design_w_);
    }
    if (design_h_ > 0 && target_h_ > 0) {
        scale_y_ = static_cast<double>(target_h_) / static_cast<double>(design_h_);
    }
}

Rect UILayoutCalculator::scale_rect(const Rect& design_rect) const {
    return {
        scale_x(design_rect.x),
        scale_y(design_rect.y),
        scale_w(design_rect.width),
        scale_h(design_rect.height)
    };
}

int UILayoutCalculator::scale_x(int x) const {
    if (design_w_ <= 0 || target_w_ <= 0) {
        return x;
    }
    return static_cast<int>((static_cast<int64_t>(x) * target_w_ + design_w_ / 2) / design_w_);
}

int UILayoutCalculator::scale_y(int y) const {
    if (design_h_ <= 0 || target_h_ <= 0) {
        return y;
    }
    return static_cast<int>((static_cast<int64_t>(y) * target_h_ + design_h_ / 2) / design_h_);
}

int UILayoutCalculator::scale_w(int w) const {
    return std::max(1, scale_x(w));
}

int UILayoutCalculator::scale_h(int h) const {
    return std::max(1, scale_y(h));
}

} // namespace mir2::ui
