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
        monitor.hardwareBrightness = false;
        monitor.hardwareError = ERROR_SUCCESS;
        monitor.hardwareStatus = HardwareStatus::Unknown;
        DWORD physicalCount = 0;
        if (!GetNumberOfPhysicalMonitorsFromHMONITOR(monitor.handle, &physicalCount)) {
            monitor.hardwareError = GetLastError();
            monitor.hardwareStatus = IsUnsupportedHardwareError(monitor.hardwareError)
                ? HardwareStatus::Unsupported : HardwareStatus::Failed;
            continue;
        }
        if (physicalCount == 0) {
            monitor.hardwareError = ERROR_NOT_SUPPORTED;
            monitor.hardwareStatus = HardwareStatus::Unsupported;
            continue;
        }

        std::vector<PHYSICAL_MONITOR> physicalMonitors(physicalCount);
        if (!GetPhysicalMonitorsFromHMONITOR(monitor.handle, physicalCount, physicalMonitors.data())) {
            monitor.hardwareError = GetLastError();
            monitor.hardwareStatus = IsUnsupportedHardwareError(monitor.hardwareError)
                ? HardwareStatus::Unsupported : HardwareStatus::Failed;
            continue;
        }

        CachedDisplay display;
        display.id = monitor.id;
        for (PHYSICAL_MONITOR& physical : physicalMonitors) {
            DWORD current = 0;
            DWORD maximum = 0;
            MC_VCP_CODE_TYPE type = MC_SET_PARAMETER;
            const BOOL queried = GetVCPFeatureAndVCPFeatureReply(
                physical.hPhysicalMonitor, kBrightnessVcpCode, &type, &current, &maximum);
            const DWORD error = queried ? ERROR_SUCCESS : GetLastError();
            if (queried && type == MC_SET_PARAMETER && maximum > 0 && current <= maximum) {
                display.monitors.push_back({physical, maximum});
                monitor.hardwareBrightness = true;
            } else {
                const DWORD failure = queried ? ERROR_INVALID_DATA : error;
                if (monitor.hardwareError == ERROR_SUCCESS ||
                    (IsUnsupportedHardwareError(monitor.hardwareError) && !IsUnsupportedHardwareError(failure))) {
                    monitor.hardwareError = failure;
                }
                DestroyPhysicalMonitor(physical.hPhysicalMonitor);
                // Keep the rejected physical endpoint's result and index.
                display.monitors.push_back({{}, 0, failure});
            }
        }

        if (!display.monitors.empty()) {
            displays.push_back(std::move(display));
        }
        monitor.hardwareStatus = monitor.hardwareError == ERROR_SUCCESS
            ? HardwareStatus::Available
            : (!monitor.hardwareBrightness && IsUnsupportedHardwareError(monitor.hardwareError)
                ? HardwareStatus::Unsupported : HardwareStatus::Failed);
    }

    std::lock_guard lock(m_mutex);
    ReleaseMonitorsLocked();
    m_displays = std::move(displays);
}

std::vector<HardwareWriteResult> HardwareBrightness::ApplyBrightness(int percent, const std::vector<std::wstring>& monitorIds) {
    const int clamped = ClampBrightness(percent);
    const std::unordered_set<std::wstring> targets(monitorIds.begin(), monitorIds.end());
    std::vector<HardwareWriteResult> results;
    std::unordered_set<std::wstring> found;

    std::lock_guard lock(m_mutex);
    for (const CachedDisplay& display : m_displays) {
        if (!targets.contains(display.id)) {
            continue;
        }

        found.insert(display.id);
        for (size_t i = 0; i < display.monitors.size(); ++i) {
            const CachedPhysicalMonitor& monitor = display.monitors[i];
            if (!monitor.monitor.hPhysicalMonitor) {
                results.push_back({display.id, i, 0, monitor.discoveryError});
                continue;
            }
            const double position = clamped / 100.0;
            const DWORD target = static_cast<DWORD>(monitor.maxBrightness * position + 0.5);
            const BOOL written = SetVCPFeature(monitor.monitor.hPhysicalMonitor, kBrightnessVcpCode, target);
            const DWORD error = written ? ERROR_SUCCESS : GetLastError();
            results.push_back({display.id, i, target, error});
        }
    }

    for (const auto& id : targets) {
        if (!found.contains(id)) {
            results.push_back({id, 0, 0, ERROR_NOT_SUPPORTED});
        }
    }
    return results;
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
            if (monitor.monitor.hPhysicalMonitor) handles.push_back(monitor.monitor);
        }
        if (!handles.empty()) DestroyPhysicalMonitors(static_cast<DWORD>(handles.size()), handles.data());
    }
    m_displays.clear();
}
