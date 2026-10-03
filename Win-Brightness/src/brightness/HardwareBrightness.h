#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "BrightnessTypes.h"
#include <atomic>
#include <memory>
#include <optional>
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
    void ReleaseMonitors() noexcept;

private:
    struct PhysicalMonitorDeleter {
        void operator()(void* handle) const noexcept;
    };
    using UniquePhysicalMonitor = std::unique_ptr<void, PhysicalMonitorDeleter>;

    struct CachedPhysicalMonitor {
        UniquePhysicalMonitor handle;
        DWORD maxBrightness = 100;
        DWORD discoveryError = ERROR_SUCCESS;
        std::optional<DWORD> lastWritten;
    };

    struct CachedDisplay {
        std::wstring id;
        std::vector<CachedPhysicalMonitor> monitors;
    };

    // Owned by the controller's I/O thread; destruction follows its join.
    std::vector<CachedDisplay> m_displays;
};
