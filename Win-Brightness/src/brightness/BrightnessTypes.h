#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <string>
#include <vector>
#include <windows.h>

enum class BrightnessMode {
    Software = 0,
    Hardware = 1
};

inline constexpr int kMinBrightness = 1;
inline constexpr int kMaxBrightness = 100;
inline constexpr int kDefaultBrightness = 72;

inline int ClampBrightness(int percent) {
    return std::clamp(percent, kMinBrightness, kMaxBrightness);
}

struct MonitorInfo {
    std::wstring id;
    std::wstring deviceName;
    std::wstring name;
    RECT bounds{};
    HMONITOR handle = nullptr;
    bool primary = false;
    bool hardwareBrightness = false;
};

struct MonitorSelection {
    bool all = true;
    std::vector<std::wstring> ids;

    bool Contains(const std::wstring& id) const {
        return all || std::find(ids.begin(), ids.end(), id) != ids.end();
    }
};
