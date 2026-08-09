#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "BrightnessTypes.h"
#include <mutex>
#include <physicalmonitorenumerationapi.h>
#include <string>
#include <vector>
#include <windows.h>

class HardwareBrightness {
public:
    HardwareBrightness() = default;
    ~HardwareBrightness();

    HardwareBrightness(const HardwareBrightness&) = delete;
    HardwareBrightness& operator=(const HardwareBrightness&) = delete;

    void RefreshMonitors(std::vector<MonitorInfo>& monitors);
    int ReadBrightness(const std::vector<std::wstring>& monitorIds);
    bool ApplyBrightness(int percent, const std::vector<std::wstring>& monitorIds);
    void ReleaseMonitors();

private:
    struct CachedPhysicalMonitor {
        PHYSICAL_MONITOR monitor{};
        DWORD minBrightness = 0;
        DWORD maxBrightness = 100;
    };

    struct CachedDisplay {
        std::wstring id;
        std::vector<CachedPhysicalMonitor> monitors;
    };

    mutable std::mutex m_mutex;
    std::vector<CachedDisplay> m_displays;

    void ReleaseMonitorsLocked();
};
