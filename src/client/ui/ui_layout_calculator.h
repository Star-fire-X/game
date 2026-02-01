// =============================================================================
// UI Layout Calculator
//
// Provides design-to-target scaling helpers for UI layouts.
// =============================================================================

#ifndef LEGEND2_UI_LAYOUT_CALCULATOR_H
#define LEGEND2_UI_LAYOUT_CALCULATOR_H

#include "common/types.h"

namespace mir2::ui {

// 引入公共类型定义
using mir2::common::Rect;

/// Calculates scaled UI bounds from a design resolution to a target resolution.
class UILayoutCalculator {
public:
    /// @brief 构造函数
    /// @param design_w 设计分辨率宽度
    /// @param design_h 设计分辨率高度
    /// @param target_w 目标分辨率宽度
    /// @param target_h 目标分辨率高度
    UILayoutCalculator(int design_w, int design_h, int target_w, int target_h);

    /// @brief 缩放矩形
    Rect scale_rect(const Rect& design_rect) const;

    /// @brief 缩放X坐标
    int scale_x(int x) const;

    /// @brief 缩放Y坐标
    int scale_y(int y) const;

    /// @brief 缩放宽度
    int scale_w(int w) const;

    /// @brief 缩放高度
    int scale_h(int h) const;

    /// @brief 获取缩放比例
    double get_scale_x() const { return scale_x_; }
    double get_scale_y() const { return scale_y_; }

private:
    double scale_x_ = 1.0;
    double scale_y_ = 1.0;
    int design_w_ = 0;
    int design_h_ = 0;
    int target_w_ = 0;
    int target_h_ = 0;
};

} // namespace mir2::ui

#endif // LEGEND2_UI_LAYOUT_CALCULATOR_H
