#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <physicalmonitorenumerationapi.h>
#include <windows.h>
#include <mutex>
#include <vector>

class HardwareBrightness {
public:
    HardwareBrightness() = default;
    ~HardwareBrightness();

    HardwareBrightness(const HardwareBrightness&) = delete;
    HardwareBrightness& operator=(const HardwareBrightness&) = delete;

    void RefreshMonitors();
    bool IsAvailable() const;
    int ReadBrightness();
    bool ApplyBrightness(int percent);
    void ReleaseMonitors();

private:
    struct CachedPhysicalMonitor {
        PHYSICAL_MONITOR monitor{};
        DWORD minBrightness = 0;
        DWORD maxBrightness = 100;
    };

    mutable std::mutex m_mutex;
    std::vector<CachedPhysicalMonitor> m_monitors;

    void ReleaseMonitorsLocked();
};
