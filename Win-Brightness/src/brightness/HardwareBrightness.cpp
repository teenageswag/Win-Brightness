#include "HardwareBrightness.h"
#include "BrightnessTypes.h"
#include <highlevelmonitorconfigurationapi.h>
#pragma comment(lib, "dxva2.lib")

HardwareBrightness::~HardwareBrightness() {
    ReleaseMonitors();
}

void HardwareBrightness::RefreshMonitors() {
    std::vector<CachedPhysicalMonitor> monitors;

    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR hMonitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto* cachedMonitors = reinterpret_cast<std::vector<CachedPhysicalMonitor>*>(data);

            DWORD monitorCount = 0;
            if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, &monitorCount) || monitorCount == 0) {
                return TRUE;
            }

            std::vector<PHYSICAL_MONITOR> physicalMonitors(monitorCount);
            if (!GetPhysicalMonitorsFromHMONITOR(hMonitor, monitorCount, physicalMonitors.data())) {
                return TRUE;
            }

            for (DWORD i = 0; i < monitorCount; ++i) {
                DWORD minBrightness = 0;
                DWORD currentBrightness = 0;
                DWORD maxBrightness = 0;
                if (GetMonitorBrightness(physicalMonitors[i].hPhysicalMonitor, &minBrightness, &currentBrightness, &maxBrightness) &&
                    maxBrightness > minBrightness) {
                    CachedPhysicalMonitor cachedMonitor;
                    cachedMonitor.monitor = physicalMonitors[i];
                    cachedMonitor.minBrightness = minBrightness;
                    cachedMonitor.maxBrightness = maxBrightness;
                    cachedMonitors->push_back(cachedMonitor);
                } else {
                    DestroyPhysicalMonitor(physicalMonitors[i].hPhysicalMonitor);
                }
            }

            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors));

    std::lock_guard<std::mutex> lock(m_mutex);
    ReleaseMonitorsLocked();
    m_monitors = std::move(monitors);
}

bool HardwareBrightness::IsAvailable() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_monitors.empty();
}

int HardwareBrightness::ReadBrightness() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const CachedPhysicalMonitor& monitor : m_monitors) {
        DWORD minBrightness = 0;
        DWORD currentBrightness = 0;
        DWORD maxBrightness = 0;
        if (GetMonitorBrightness(monitor.monitor.hPhysicalMonitor, &minBrightness, &currentBrightness, &maxBrightness) && maxBrightness > minBrightness) {
            const int percent = static_cast<int>((currentBrightness - minBrightness) * 100.0 / (maxBrightness - minBrightness) + 0.5);
            return ClampBrightness(percent);
        }
    }

    return -1;
}

bool HardwareBrightness::ApplyBrightness(int percent) {
    bool applied = false;
    const int clamped = ClampBrightness(percent);
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const CachedPhysicalMonitor& monitor : m_monitors) {
        const DWORD target = monitor.minBrightness + static_cast<DWORD>((monitor.maxBrightness - monitor.minBrightness) * (clamped / 100.0) + 0.5);
        if (SetMonitorBrightness(monitor.monitor.hPhysicalMonitor, target)) {
            applied = true;
        }
    }

    return applied;
}

void HardwareBrightness::ReleaseMonitors() {
    std::lock_guard<std::mutex> lock(m_mutex);
    ReleaseMonitorsLocked();
}

void HardwareBrightness::ReleaseMonitorsLocked() {
    if (m_monitors.empty()) {
        return;
    }

    std::vector<PHYSICAL_MONITOR> monitors;
    monitors.reserve(m_monitors.size());
    for (const CachedPhysicalMonitor& monitor : m_monitors) {
        monitors.push_back(monitor.monitor);
    }

    DestroyPhysicalMonitors(static_cast<DWORD>(monitors.size()), monitors.data());
    m_monitors.clear();
}
