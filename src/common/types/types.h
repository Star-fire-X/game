// =============================================================================
// Legend2 基础类型定义 (Basic Types)
//
// 功能说明:
//   - 基础数据结构（位置、颜色、矩形、尺寸）
//   - JSON序列化支持
// =============================================================================

#ifndef LEGEND2_COMMON_TYPES_TYPES_H
#define LEGEND2_COMMON_TYPES_TYPES_H

#include <cstdint>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace mir2::common {

// =============================================================================
// 基础类型 (Basic Types)
// =============================================================================

/// 2D位置结构（游戏世界中的瓦片坐标）
struct Position {
    int x = 0;
    int y = 0;

    bool operator==(const Position& other) const {
        return x == other.x && y == other.y;
    }

    bool operator!=(const Position& other) const {
        return !(*this == other);
    }

    /// 计算到另一个位置的曼哈顿距离
    int manhattan_distance(const Position& other) const {
        return std::abs(x - other.x) + std::abs(y - other.y);
    }

    /// 计算到另一个位置的欧几里得距离平方（避免开方运算）
    int distance_squared(const Position& other) const {
        int dx = x - other.x;
        int dy = y - other.y;
        return dx * dx + dy * dy;
    }
};

/// RGBA颜色结构
struct Color {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;

    bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }

    static Color white() { return {255, 255, 255, 255}; }
    static Color black() { return {0, 0, 0, 255}; }
    static Color red() { return {255, 0, 0, 255}; }
    static Color green() { return {0, 255, 0, 255}; }
    static Color blue() { return {0, 0, 255, 255}; }
    static Color transparent() { return {0, 0, 0, 0}; }
};

/// 矩形结构（用于UI和碰撞检测）
struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool contains(int px, int py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }

    bool contains(const Position& pos) const {
        return contains(pos.x, pos.y);
    }

    bool intersects(const Rect& other) const {
        return x < other.x + other.width && x + width > other.x &&
               y < other.y + other.height && y + height > other.y;
    }
};

/// 尺寸结构（宽度和高度）
struct Size {
    int width = 0;
    int height = 0;
};

// =============================================================================
// JSON序列化支持
// =============================================================================

inline void to_json(nlohmann::json& j, const Position& p) {
    j = nlohmann::json{{"x", p.x}, {"y", p.y}};
}

inline void from_json(const nlohmann::json& j, Position& p) {
    j.at("x").get_to(p.x);
    j.at("y").get_to(p.y);
}

inline void to_json(nlohmann::json& j, const Color& c) {
    j = nlohmann::json{{"r", c.r}, {"g", c.g}, {"b", c.b}, {"a", c.a}};
}

inline void from_json(const nlohmann::json& j, Color& c) {
    j.at("r").get_to(c.r);
    j.at("g").get_to(c.g);
    j.at("b").get_to(c.b);
    j.at("a").get_to(c.a);
}

inline void to_json(nlohmann::json& j, const Rect& r) {
    j = nlohmann::json{{"x", r.x}, {"y", r.y}, {"width", r.width}, {"height", r.height}};
}

inline void from_json(const nlohmann::json& j, Rect& r) {
    j.at("x").get_to(r.x);
    j.at("y").get_to(r.y);
    j.at("width").get_to(r.width);
    j.at("height").get_to(r.height);
}

inline void to_json(nlohmann::json& j, const Size& s) {
    j = nlohmann::json{{"width", s.width}, {"height", s.height}};
}

inline void from_json(const nlohmann::json& j, Size& s) {
    j.at("width").get_to(s.width);
    j.at("height").get_to(s.height);
}

} // namespace mir2::common

#endif // LEGEND2_COMMON_TYPES_TYPES_H
