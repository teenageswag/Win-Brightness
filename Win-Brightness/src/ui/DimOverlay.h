#pragma once
#include <vector>
#include <windows.h>

class DimOverlay {
public:
    DimOverlay() = default;
    ~DimOverlay();

    DimOverlay(const DimOverlay&) = delete;
    DimOverlay& operator=(const DimOverlay&) = delete;

    void Apply(int percent);
    void Destroy();

private:
    bool RegisterWindowClass();
    BYTE AlphaFromPercent(int percent) const;

    std::vector<HWND> m_windows;
};
