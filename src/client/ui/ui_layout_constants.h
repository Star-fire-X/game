// =============================================================================
// UI Layout Constants
//
// Centralized design-space rectangles for UI layouts.
// =============================================================================

#ifndef LEGEND2_UI_LAYOUT_CONSTANTS_H
#define LEGEND2_UI_LAYOUT_CONSTANTS_H

#include "common/types.h"

namespace mir2::ui::layout {

using mir2::common::Rect;

// 登录界面布局常量（基于800x600设计分辨率）
namespace login {
constexpr int DESIGN_WIDTH = 800;
constexpr int DESIGN_HEIGHT = 600;

constexpr Rect USERNAME_FIELD = {349, 258, 139, 18};
constexpr Rect PASSWORD_FIELD = {349, 290, 140, 18};
constexpr Rect CONFIRM_BUTTON = {392, 330, 136, 51};
} // namespace login

// 角色创建界面布局常量（基于800x600设计分辨率）
namespace character_create {
constexpr int DESIGN_WIDTH = 800;
constexpr int DESIGN_HEIGHT = 600;

constexpr Rect CREATE_BUTTON = {344, 484, 120, 21};
constexpr Rect CLASS_PANEL = {428, 5, 300, 417};

constexpr Rect CLASS_ICONS[5] = {
    {CLASS_PANEL.x + 47,  CLASS_PANEL.y + 156, 44, 36},
    {CLASS_PANEL.x + 92,  CLASS_PANEL.y + 156, 44, 36},
    {CLASS_PANEL.x + 137, CLASS_PANEL.y + 156, 44, 36},
    {CLASS_PANEL.x + 92,  CLASS_PANEL.y + 230, 44, 35},
    {CLASS_PANEL.x + 137, CLASS_PANEL.y + 230, 44, 35},
};

constexpr Rect PREVIEW_AREA = {97, 66, 251, 322};
constexpr Rect NAME_FIELD = {CLASS_PANEL.x + 68, CLASS_PANEL.y + 103, 140, 19};
constexpr Rect CONFIRM_BUTTON = {CLASS_PANEL.x + 99, CLASS_PANEL.y + 347, 84, 55};
constexpr Rect CANCEL_BUTTON = {CLASS_PANEL.x + 245, CLASS_PANEL.y + 28, 20, 20};
constexpr Rect START_GAME = {380, 453, 44, 21};

constexpr Rect CREATED_NAME = {105, 489, 102, 20};
constexpr Rect CREATED_LEVEL = {106, 518, 55, 16};
constexpr Rect CREATED_CLASS = {105, 546, 102, 20};
} // namespace character_create

} // namespace mir2::ui::layout

#endif // LEGEND2_UI_LAYOUT_CONSTANTS_H
