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

struct HardwareWriteResult {
    std::wstring monitorId;
    size_t physicalIndex = 0;
    DWORD requestedValue = 0;
    DWORD error = ERROR_SUCCESS;
};

class HardwareBrightness {
public:
    HardwareBrightness() = default;
    ~HardwareBrightness();

    HardwareBrightness(const HardwareBrightness&) = delete;
    HardwareBrightness& operator=(const HardwareBrightness&) = delete;

    void RefreshMonitors(std::vector<MonitorInfo>& monitors);
    std::vector<HardwareWriteResult> ApplyBrightness(int percent, const std::vector<std::wstring>& monitorIds);
    void ReleaseMonitors();

private:
    struct CachedPhysicalMonitor {
        PHYSICAL_MONITOR monitor{};
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
