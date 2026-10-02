#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "BrightnessTypes.h"
#include <atomic>
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

    void RefreshMonitors(std::vector<MonitorInfo>& monitors, const std::atomic_bool* cancelled = nullptr);
    std::vector<HardwareWriteResult> ApplyBrightness(int percent, const std::vector<std::wstring>& monitorIds,
                                                   const std::atomic_bool* cancelled = nullptr);
    void ReleaseMonitors();

private:
    struct CachedPhysicalMonitor {
        PHYSICAL_MONITOR monitor{};
        DWORD maxBrightness = 100;
        DWORD discoveryError = ERROR_SUCCESS;
    };

    struct CachedDisplay {
        std::wstring id;
        std::vector<CachedPhysicalMonitor> monitors;
    };

    mutable std::mutex m_mutex;
    std::vector<CachedDisplay> m_displays;

    void ReleaseMonitorsLocked();
};
