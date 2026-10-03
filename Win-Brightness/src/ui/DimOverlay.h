#pragma once

#include "../brightness/BrightnessTypes.h"
#include <string>
#include <vector>
#include <windows.h>

class DimOverlay {
public:
    DimOverlay() = default;
    ~DimOverlay();

    DimOverlay(const DimOverlay&) = delete;
    DimOverlay& operator=(const DimOverlay&) = delete;

    void Apply(int percent, const std::vector<MonitorInfo>& monitors);
    void Apply(const std::vector<MonitorInfo>& monitors);
    void Destroy();

private:
    struct OverlayWindow {
        std::wstring monitorId;
        HWND handle = nullptr;
        RECT bounds{};
        BYTE alpha = 0;
    };

    bool RegisterWindowClass();
    BYTE AlphaFromPercent(int percent) const;

    std::vector<OverlayWindow> m_windows;
};
