#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace island {
struct Point { float x; float y; };
struct Rect {
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
    float Width() const { return right - left; }
    float Height() const { return bottom - top; }
    bool Contains(Point p) const {
        return p.x >= left && p.x < right && p.y >= top && p.y < bottom;
    }
    Rect Offset(float x, float y) const { return {left + x, top + y, right + x, bottom + y}; }
};

inline constexpr float kExponent = 4.5f;
inline constexpr size_t kCornerSegments = 64;
using SquirclePoints = std::array<Point, 4 * (kCornerSegments + 1)>;

inline SquirclePoints Squircle(float width, float height, float radius, bool squareTop = false) {
    SquirclePoints points{};
    const double r = std::clamp(static_cast<double>(radius), 0.0,
                              static_cast<double>(std::min(width, height)) * 0.5);
    const std::array<Point, 4> centers{{
        {width - static_cast<float>(r), static_cast<float>(r)},
        {width - static_cast<float>(r), height - static_cast<float>(r)},
        {static_cast<float>(r), height - static_cast<float>(r)},
        {static_cast<float>(r), static_cast<float>(r)}
    }};
    for (size_t corner = 0; corner < 4; ++corner) {
        for (size_t step = 0; step <= kCornerSegments; ++step) {
            const double t = (-1.0 + static_cast<double>(corner) +
                static_cast<double>(step) / kCornerSegments) * std::numbers::pi / 2.0;
            auto axis = [](double value) {
                if (std::abs(value) < 1e-12) return 0.0;
                return std::copysign(std::pow(std::abs(value), 2.0 / kExponent), value);
            };
            // Four local superellipse quadrants join the straight edges with
            // zero endpoint curvature. Points are rasterized as an AA vector path.
            points[corner * (kCornerSegments + 1) + step] = {
                centers[corner].x + static_cast<float>(r * axis(std::cos(t))),
                centers[corner].y + static_cast<float>(r * axis(std::sin(t)))
            };
            if (squareTop && corner == 0) points[corner * (kCornerSegments + 1) + step] = {width, 0};
            if (squareTop && corner == 3) points[corner * (kCornerSegments + 1) + step] = {0, 0};
        }
    }
    return points;
}

inline bool InsideSquircle(Point point, Rect bounds, float radius, bool squareTop = false) {
    if (!bounds.Contains(point)) return false;
    const float r = std::clamp(radius, 0.0f, std::min(bounds.Width(), bounds.Height()) * 0.5f);
    if (r == 0.0f) return true;
    if (squareTop && point.y <= bounds.top + r) return true;
    const float x = std::max(std::abs(point.x - (bounds.left + bounds.right) * 0.5f)
                             - (bounds.Width() * 0.5f - r), 0.0f) / r;
    const float y = std::max(std::abs(point.y - (bounds.top + bounds.bottom) * 0.5f)
                             - (bounds.Height() * 0.5f - r), 0.0f) / r;
    return std::pow(x, kExponent) + std::pow(y, kExponent) <= 1.0f;
}
} // namespace island
