#pragma once

#include "Geometry.h"
#include <cstddef>
#include <vector>

namespace island {
inline constexpr float kMinimumHeight = 64.0f;
inline constexpr float kWidth = 420.0f;
inline constexpr float kPadding = 15.0f;
inline constexpr float kRowHeight = 62.0f;
inline constexpr float kRowGap = 15.0f;
inline constexpr float kRowStride = kRowHeight + kRowGap;
inline constexpr float kShadowMargin = 16.0f;
inline constexpr size_t kVisibleMonitors = 5;

inline float FooterHeight(float availableWidth) { return availableWidth < 350.0f ? 83.0f : 34.0f; }
inline float PanelHeight(size_t monitorCount, float availableHeight, float availableWidth = kWidth) {
    const size_t rows = std::clamp(monitorCount, size_t{1}, kVisibleMonitors);
    return std::min(30.0f + FooterHeight(availableWidth) + static_cast<float>(rows) * kRowStride, availableHeight);
}

enum class Control { None, Slider, Monitor, Software, Hardware, Power };
struct Target {
    Control control = Control::None;
    size_t monitor = 0;
    bool operator==(const Target&) const = default;
};
struct Row { Rect label; Rect slider; size_t index; };
struct Layout {
    Rect shell;
    Rect viewport;
    Rect software, hardware, power;
    std::vector<Row> rows;

    static Layout Build(float width, float height, size_t monitorCount, float scroll,
                        float availableWidth = kWidth) {
        Layout result;
        result.shell = {0.0f, 0.0f, width, height};
        const bool narrow = availableWidth < 350.0f;
        const float footerTop = height - kPadding - FooterHeight(availableWidth);
        result.viewport = {kPadding, kPadding, width - kPadding, footerTop - 15.0f};
        float top = kPadding - scroll;
        result.rows.reserve(monitorCount);
        for (size_t i = 0; i < monitorCount; ++i) {
            result.rows.push_back({{kPadding, top, width - kPadding, top + 20.0f},
                                  {kPadding, top + 30.0f, width - kPadding, top + kRowHeight}, i});
            top += kRowStride;
        }
        const float modeWidth = narrow ? width - 30.0f : width - 30.0f - 15.0f - 85.0f;
        result.software = {kPadding, footerTop, kPadding + modeWidth * 0.5f, footerTop + 34.0f};
        result.hardware = {result.software.right, footerTop, kPadding + modeWidth, footerTop + 34.0f};
        const float actionsTop = footerTop + (narrow ? 49.0f : 0.0f);
        result.power = {narrow ? kPadding : result.hardware.right + 15.0f,
                        actionsTop, width - kPadding, actionsTop + 34.0f};
        return result;
    }

    float MaximumScroll() const {
        return std::max(0.0f, static_cast<float>(rows.size()) * kRowStride
            - kRowGap - viewport.Height());
    }
    Target Hit(Point point) const {
        if (power.Contains(point)) return {Control::Power};
        {
            if (software.Contains(point)) return {Control::Software};
            if (hardware.Contains(point)) return {Control::Hardware};
        }
        if (viewport.Contains(point)) {
            for (const auto& row : rows) {
                Rect hit = row.slider;
                if (hit.Contains(point)) return {Control::Slider, row.index};
                if (row.label.Contains(point)) return {Control::Monitor, row.index};
            }
        }
        return {};
    }
};
} // namespace island
