#include "Win32Helpers.h"

ScreenDc::ScreenDc() : m_hdc(GetDC(nullptr)) {}

ScreenDc::~ScreenDc() {
    if (m_hdc) {
        ReleaseDC(nullptr, m_hdc);
    }
}

RegistryKey::RegistryKey(HKEY key) : m_key(key) {}

RegistryKey::~RegistryKey() {
    Reset();
}

RegistryKey::RegistryKey(RegistryKey&& other) noexcept : m_key(other.m_key) {
    other.m_key = nullptr;
}

RegistryKey& RegistryKey::operator=(RegistryKey&& other) noexcept {
    if (this != &other) {
        Reset(other.m_key);
        other.m_key = nullptr;
    }
    return *this;
}

HKEY* RegistryKey::Put() {
    Reset();
    return &m_key;
}

void RegistryKey::Reset(HKEY key) {
    if (m_key) {
        RegCloseKey(m_key);
    }
    m_key = key;
}

MemoryPaintDc::MemoryPaintDc(HDC targetDc, int width, int height)
    : m_memoryDc(CreateCompatibleDC(targetDc)), m_bitmap(CreateCompatibleBitmap(targetDc, width, height)) {
    if (m_memoryDc && m_bitmap) {
        m_oldBitmap = SelectObject(m_memoryDc, m_bitmap);
    }
}

MemoryPaintDc::~MemoryPaintDc() {
    if (m_memoryDc && m_oldBitmap) {
        SelectObject(m_memoryDc, m_oldBitmap);
    }
    if (m_bitmap) {
        DeleteObject(m_bitmap);
    }
    if (m_memoryDc) {
        DeleteDC(m_memoryDc);
    }
}

std::vector<RECT> GetMonitorRects() {
    std::vector<RECT> rects;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR hMonitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto* monitorRects = reinterpret_cast<std::vector<RECT>*>(data);
            MONITORINFO monitorInfo = {sizeof(monitorInfo)};
            if (GetMonitorInfo(hMonitor, &monitorInfo)) {
                monitorRects->push_back(monitorInfo.rcMonitor);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&rects));
    return rects;
}

int GetDpiForPoint(POINT pt) {
    HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    UINT dpiX = 96;
    UINT dpiY = 96;

    HMODULE shcore = LoadLibrary(L"shcore.dll");
    if (shcore) {
        auto getDpiForMonitor = reinterpret_cast<HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*)>(GetProcAddress(shcore, "GetDpiForMonitor"));
        if (getDpiForMonitor) {
            getDpiForMonitor(hMonitor, 0, &dpiX, &dpiY);
        }
        FreeLibrary(shcore);
    }

    return static_cast<int>(dpiX);
}

int GetDpiForHwnd(HWND hWnd) {
    auto getDpiForWindow = reinterpret_cast<UINT(WINAPI*)(HWND)>(GetProcAddress(GetModuleHandle(L"user32.dll"), "GetDpiForWindow"));
    return (getDpiForWindow && hWnd) ? static_cast<int>(getDpiForWindow(hWnd)) : 96;
}

int ScaleByDpi(int value, int dpi) {
    return MulDiv(value, dpi, 96);
}
