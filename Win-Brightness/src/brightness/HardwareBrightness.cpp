#include "HardwareBrightness.h"
#include <lowlevelmonitorconfigurationapi.h>
#include <unordered_set>

#pragma comment(lib, "dxva2.lib")

namespace {
    constexpr BYTE kBrightnessVcpCode = 0x10;
}

HardwareBrightness::~HardwareBrightness() {
    ReleaseMonitors();
}

void HardwareBrightness::RefreshMonitors(std::vector<MonitorInfo>& monitors) {
    std::vector<CachedDisplay> displays;

    for (MonitorInfo& monitor : monitors) {
        DWORD physicalCount = 0;
        if (!GetNumberOfPhysicalMonitorsFromHMONITOR(monitor.handle, &physicalCount) || physicalCount == 0) {
            continue;
        }

        std::vector<PHYSICAL_MONITOR> physicalMonitors(physicalCount);
        if (!GetPhysicalMonitorsFromHMONITOR(monitor.handle, physicalCount, physicalMonitors.data())) {
            continue;
        }

        CachedDisplay display;
        display.id = monitor.id;
        for (PHYSICAL_MONITOR& physical : physicalMonitors) {
            DWORD current = 0;
            DWORD maximum = 0;
            if (GetVCPFeatureAndVCPFeatureReply(
                    physical.hPhysicalMonitor, kBrightnessVcpCode, nullptr, &current, &maximum) &&
                maximum > 0) {
                display.monitors.push_back({physical, maximum});
            } else {
                DestroyPhysicalMonitor(physical.hPhysicalMonitor);
            }
        }

        if (!display.monitors.empty()) {
            monitor.hardwareBrightness = true;
            displays.push_back(std::move(display));
        }
    }

    std::lock_guard lock(m_mutex);
    ReleaseMonitorsLocked();
    m_displays = std::move(displays);
}

bool HardwareBrightness::ApplyBrightness(int percent, const std::vector<std::wstring>& monitorIds) {
    const int clamped = ClampBrightness(percent);
    const std::unordered_set<std::wstring> targets(monitorIds.begin(), monitorIds.end());
    bool applied = false;

    std::lock_guard lock(m_mutex);
    for (const CachedDisplay& display : m_displays) {
        if (!targets.contains(display.id)) {
            continue;
        }

        for (const CachedPhysicalMonitor& monitor : display.monitors) {
            const double position = clamped / 100.0;
            const DWORD target = static_cast<DWORD>(monitor.maxBrightness * position + 0.5);
            applied = SetVCPFeature(monitor.monitor.hPhysicalMonitor, kBrightnessVcpCode, target) != FALSE || applied;
        }
    }

    return applied;
}

void HardwareBrightness::ReleaseMonitors() {
    std::lock_guard lock(m_mutex);
    ReleaseMonitorsLocked();
}

void HardwareBrightness::ReleaseMonitorsLocked() {
    for (const CachedDisplay& display : m_displays) {
        if (display.monitors.empty()) {
            continue;
        }

        std::vector<PHYSICAL_MONITOR> handles;
        handles.reserve(display.monitors.size());
        for (const CachedPhysicalMonitor& monitor : display.monitors) {
            handles.push_back(monitor.monitor);
        }
        DestroyPhysicalMonitors(static_cast<DWORD>(handles.size()), handles.data());
    }
    m_displays.clear();
}
