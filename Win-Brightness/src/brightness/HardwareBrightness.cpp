#include "HardwareBrightness.h"
#include <lowlevelmonitorconfigurationapi.h>
#include <unordered_set>
#include <utility>
#include <cwchar>

#pragma comment(lib, "dxva2.lib")

namespace {
    constexpr BYTE kBrightnessVcpCode = 0x10;

    struct PhysicalBatch {
        std::vector<PHYSICAL_MONITOR> monitors;
        explicit PhysicalBatch(DWORD count) : monitors(count) {}
        ~PhysicalBatch() {
            for (const auto& monitor : monitors) {
                if (monitor.hPhysicalMonitor) DestroyPhysicalMonitor(monitor.hPhysicalMonitor);
            }
        }
        PhysicalBatch(const PhysicalBatch&) = delete;
        PhysicalBatch& operator=(const PhysicalBatch&) = delete;
    };
}

void HardwareBrightness::PhysicalMonitorDeleter::operator()(void* handle) const noexcept {
    if (!DestroyPhysicalMonitor(handle)) {
        const DWORD error = GetLastError();
        wchar_t message[96]{};
        swprintf_s(message, L"trenches: DestroyPhysicalMonitor failed (%lu)\n", error);
        OutputDebugStringW(message);
    }
}

HardwareBrightness::~HardwareBrightness() {
    ReleaseMonitors();
}

void HardwareBrightness::RefreshMonitors(std::vector<MonitorInfo>& monitors, const std::atomic_bool* cancelled) {
    std::vector<CachedDisplay> displays;

    for (MonitorInfo& monitor : monitors) {
        if (cancelled && cancelled->load(std::memory_order_relaxed)) break;
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

        PhysicalBatch batch(physicalCount);
        if (!GetPhysicalMonitorsFromHMONITOR(monitor.handle, physicalCount, batch.monitors.data())) {
            monitor.hardwareError = GetLastError();
            monitor.hardwareStatus = IsUnsupportedHardwareError(monitor.hardwareError)
                ? HardwareStatus::Unsupported : HardwareStatus::Failed;
            continue;
        }

        CachedDisplay display;
        display.id = monitor.id;
        for (PHYSICAL_MONITOR& physical : batch.monitors) {
            UniquePhysicalMonitor owned(std::exchange(physical.hPhysicalMonitor, nullptr));
            if (cancelled && cancelled->load(std::memory_order_relaxed)) {
                continue;
            }
            DWORD current = 0;
            DWORD maximum = 0;
            MC_VCP_CODE_TYPE type = MC_SET_PARAMETER;
            const BOOL queried = GetVCPFeatureAndVCPFeatureReply(
                owned.get(), kBrightnessVcpCode, &type, &current, &maximum);
            const DWORD error = queried ? ERROR_SUCCESS : GetLastError();
            if (queried && type == MC_SET_PARAMETER && maximum > 0 && current <= maximum) {
                display.monitors.push_back({std::move(owned), maximum});
                monitor.hardwareBrightness = true;
            } else {
                const DWORD failure = queried ? ERROR_INVALID_DATA : error;
                if (monitor.hardwareError == ERROR_SUCCESS ||
                    (IsUnsupportedHardwareError(monitor.hardwareError) && !IsUnsupportedHardwareError(failure))) {
                    monitor.hardwareError = failure;
                }
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

    m_displays = std::move(displays);
}

std::vector<HardwareWriteResult> HardwareBrightness::ApplyBrightness(
    int percent, const std::vector<std::wstring>& monitorIds, const std::atomic_bool* cancelled) {
    const int clamped = ClampBrightness(percent);
    const std::unordered_set<std::wstring> targets(monitorIds.begin(), monitorIds.end());
    std::vector<HardwareWriteResult> results;
    std::unordered_set<std::wstring> found;

    for (const CachedDisplay& display : m_displays) {
        if (cancelled && cancelled->load(std::memory_order_relaxed)) break;
        if (!targets.contains(display.id)) {
            continue;
        }

        found.insert(display.id);
        for (size_t i = 0; i < display.monitors.size(); ++i) {
            if (cancelled && cancelled->load(std::memory_order_relaxed)) break;
            const CachedPhysicalMonitor& monitor = display.monitors[i];
            if (!monitor.handle) {
                results.push_back({display.id, i, 0, monitor.discoveryError});
                continue;
            }
            const double position = clamped / 100.0;
            const DWORD target = static_cast<DWORD>(monitor.maxBrightness * position + 0.5);
            const BOOL written = SetVCPFeature(monitor.handle.get(), kBrightnessVcpCode, target);
            const DWORD error = written ? ERROR_SUCCESS : GetLastError();
            results.push_back({display.id, i, target, error});
        }
    }

    for (const auto& id : targets) {
        if (cancelled && cancelled->load(std::memory_order_relaxed)) break;
        if (!found.contains(id)) {
            results.push_back({id, 0, 0, ERROR_NOT_SUPPORTED});
        }
    }
    return results;
}

void HardwareBrightness::ReleaseMonitors() noexcept {
    m_displays.clear();
}
