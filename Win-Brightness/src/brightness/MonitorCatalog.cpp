#include "MonitorCatalog.h"
#include <algorithm>
#include <cwchar>
#include <unordered_set>

namespace {
    struct EnumerationContext {
        std::vector<MonitorInfo> monitors;
        std::unordered_set<std::wstring> ids;
    };

    std::wstring ReadMonitorId(const MONITORINFOEX& info, DISPLAY_DEVICE& device) {
        device.cb = sizeof(device);
        if (!EnumDisplayDevices(info.szDevice, 0, &device, EDD_GET_DEVICE_INTERFACE_NAME)) {
            return info.szDevice;
        }

        return device.DeviceID[0] != L'\0' ? device.DeviceID : info.szDevice;
    }

    BOOL CALLBACK EnumerateMonitor(HMONITOR handle, HDC, LPRECT, LPARAM data) {
        auto& context = *reinterpret_cast<EnumerationContext*>(data);

        MONITORINFOEX info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfo(handle, &info)) {
            return TRUE;
        }

        DISPLAY_DEVICE device{};
        std::wstring id = ReadMonitorId(info, device);
        if (!context.ids.insert(id).second) {
            id += L"|";
            id += info.szDevice;
            context.ids.insert(id);
        }

        MonitorInfo monitor;
        monitor.id = std::move(id);
        monitor.deviceName = info.szDevice;
        monitor.name = device.DeviceString[0] != L'\0' ? device.DeviceString : L"Monitor";
        monitor.bounds = info.rcMonitor;
        monitor.handle = handle;
        monitor.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        context.monitors.push_back(std::move(monitor));
        return TRUE;
    }
} // namespace

std::vector<MonitorInfo> MonitorCatalog::Enumerate() {
    EnumerationContext context;
    EnumDisplayMonitors(nullptr, nullptr, EnumerateMonitor, reinterpret_cast<LPARAM>(&context));

    std::ranges::sort(context.monitors, [](const MonitorInfo& left, const MonitorInfo& right) {
        if (left.primary != right.primary) {
            return left.primary;
        }
        if (left.bounds.left != right.bounds.left) {
            return left.bounds.left < right.bounds.left;
        }
        return left.bounds.top < right.bounds.top;
    });

    return context.monitors;
}
