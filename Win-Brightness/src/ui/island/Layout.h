#pragma once

#include "Geometry.h"
#include <cstddef>
#include <vector>

namespace island {
inline constexpr float kCompactWidth = 344.0f;
inline constexpr float kCompactHeight = 64.0f;
inline constexpr float kExpandedWidth = 420.0f;
inline constexpr float kPadding = 15.0f;
inline constexpr float kRowHeight = 62.0f;
inline constexpr float kRowGap = 15.0f;
inline constexpr float kRowStride = kRowHeight + kRowGap;
inline constexpr float kShadowMargin = 16.0f;
inline constexpr size_t kVisibleMonitors = 5;

inline float FooterHeight(float availableWidth) { return availableWidth < 400.0f ? 83.0f : 34.0f; }
inline float ExpandedHeight(size_t monitorCount, float availableHeight, float availableWidth = kExpandedWidth) {
    const size_t rows = std::clamp(monitorCount, size_t{1}, kVisibleMonitors);
    return std::min(30.0f + FooterHeight(availableWidth) + static_cast<float>(rows) * kRowStride, availableHeight);
}

enum class Control { None, Slider, Monitor, Software, Hardware, Power, Scope, Expand };
struct Target {
    Control control = Control::None;
    size_t monitor = 0;
    bool operator==(const Target&) const = default;
};
struct Row { Rect label; Rect slider; size_t index; };
struct Layout {
    Rect shell;
    Rect viewport;
    Rect software, hardware, power, scope, expand;
    std::vector<Row> rows;
    bool expanded = false;

    static Layout Build(float width, float height, bool isExpanded, size_t monitorCount,
                        size_t primaryIndex, float scroll, float availableWidth = kExpandedWidth) {
        Layout result;
        result.expanded = isExpanded;
        result.shell = {0.0f, 0.0f, width, height};
        if (!isExpanded) {
            result.power = {15.0f, 16.0f, 47.0f, 48.0f};
            result.expand = {width - 39.0f, 16.0f, width - 15.0f, 48.0f};
            result.viewport = result.shell;
            result.rows.push_back({{62.0f, 15.0f, width - 102.0f, 33.0f},
                                   {62.0f, 43.0f, width - 102.0f, 49.0f}, primaryIndex});
            return result;
        }
        const bool narrow = availableWidth < 400.0f;
        const float footerTop = height - kPadding - FooterHeight(availableWidth);
        result.viewport = {kPadding, kPadding, width - kPadding, footerTop - 15.0f};
        float top = kPadding - scroll;
        result.rows.reserve(monitorCount);
        for (size_t i = 0; i < monitorCount; ++i) {
            result.rows.push_back({{kPadding, top, width - kPadding, top + 20.0f},
                                  {kPadding, top + 30.0f, width - kPadding, top + kRowHeight}, i});
            top += kRowStride;
        }
        const float modeWidth = narrow ? width - 30.0f : std::max(130.0f, (width - 30.0f) * 0.452f);
        result.software = {kPadding, footerTop, kPadding + modeWidth * 0.5f, footerTop + 34.0f};
        result.hardware = {result.software.right, footerTop, kPadding + modeWidth, footerTop + 34.0f};
        const float actionsTop = footerTop + (narrow ? 49.0f : 0.0f);
        const float scopeLeft = narrow ? kPadding : result.hardware.right + 15.0f;
        result.scope = {scopeLeft, actionsTop, scopeLeft + 67.0f, actionsTop + 34.0f};
        result.expand = {width - kPadding - 24.0f, actionsTop, width - kPadding, actionsTop + 34.0f};
        result.power = {result.scope.right + 15.0f, actionsTop, result.expand.left - 15.0f, actionsTop + 34.0f};
        return result;
    }

    float MaximumScroll() const {
        return expanded ? std::max(0.0f, static_cast<float>(rows.size()) * kRowStride
            - kRowGap - viewport.Height()) : 0.0f;
    }
    Target Hit(Point point) const {
        if (power.Contains(point)) return {Control::Power};
        if (expand.Contains(point)) return {Control::Expand};
        if (expanded) {
            if (software.Contains(point)) return {Control::Software};
            if (hardware.Contains(point)) return {Control::Hardware};
            if (scope.Contains(point)) return {Control::Scope};
        }
        if (viewport.Contains(point)) {
            for (const auto& row : rows) {
                Rect hit = row.slider;
                if (!expanded) hit = {hit.left, hit.top - 10.0f, hit.right, hit.bottom + 10.0f};
                if (hit.Contains(point)) return {Control::Slider, row.index};
                if (expanded && row.label.Contains(point)) return {Control::Monitor, row.index};
            }
        }
        return {};
    }
};
} // namespace island
