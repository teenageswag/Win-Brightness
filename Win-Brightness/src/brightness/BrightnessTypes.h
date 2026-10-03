#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <string>
#include <map>
#include <vector>
#include <windows.h>

enum class BrightnessMode {
    Software = 0,
    Hardware = 1
};

enum class HardwareStatus { Unknown, Available, Unsupported, Failed };

inline bool IsUnsupportedHardwareError(DWORD error) {
    return error == ERROR_NOT_SUPPORTED ||
           error == static_cast<DWORD>(ERROR_GRAPHICS_DDCCI_VCP_NOT_SUPPORTED) ||
           error == static_cast<DWORD>(ERROR_GRAPHICS_I2C_NOT_SUPPORTED);
}

inline constexpr int kMinBrightness = 1;
inline constexpr int kMaxBrightness = 100;
inline constexpr int kDefaultBrightness = 72;

inline int ClampBrightness(int percent) {
    return std::clamp(percent, kMinBrightness, kMaxBrightness);
}
using MonitorBrightnessValues = std::map<std::wstring, int>;

struct MonitorInfo {
    std::wstring id;
    std::wstring deviceName;
    std::wstring name;
    RECT bounds{};
    HMONITOR handle = nullptr;
    bool primary = false;
    bool hardwareBrightness = false;
    DWORD hardwareError = ERROR_SUCCESS;
    HardwareStatus hardwareStatus = HardwareStatus::Unknown;
    bool hardwareActive = false;
    int brightness = kDefaultBrightness;
};

struct MonitorSelection {
    bool all = true;
    std::vector<std::wstring> ids;

    bool Contains(const std::wstring& id) const {
        return all || std::find(ids.begin(), ids.end(), id) != ids.end();
    }
};
