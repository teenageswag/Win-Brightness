#include "DimOverlay.h"
#include "../brightness/BrightnessTypes.h"
#include "../platform/Win32Helpers.h"
#include <cmath>

namespace {
    constexpr const wchar_t* kDimOverlayClassName = L"BrightnessDimOverlay";
    constexpr double kOverlayCurve = 0.85;
    constexpr int kMaxOverlayAlpha = 245;
    
    LRESULT CALLBACK DimOverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(reinterpret_cast<HDC>(wParam), &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        }
    
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
} // namespace

DimOverlay::~DimOverlay() {
    Destroy();
}

bool DimOverlay::RegisterWindowClass() {
    static bool registered = false;
    if (registered) {
        return true;
    }

    WNDCLASSEX wcex = {sizeof(wcex)};
    wcex.lpfnWndProc = DimOverlayWndProc;
    wcex.hInstance = GetModuleHandle(nullptr);
    wcex.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wcex.lpszClassName = kDimOverlayClassName;
    registered = RegisterClassEx(&wcex) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    return registered;
}

BYTE DimOverlay::AlphaFromPercent(int percent) const {
    if (percent >= kMaxBrightness) {
        return 0;
    }

    const int clamped = ClampBrightness(percent);
    const double dim = (kMaxBrightness - clamped) / 99.0;
    const double shaped = std::pow(dim, kOverlayCurve);
    const int alpha = static_cast<int>(kMaxOverlayAlpha * shaped + 0.5);
    return static_cast<BYTE>(std::clamp(alpha, 0, kMaxOverlayAlpha));
}

void DimOverlay::Apply(int percent) {
    const BYTE alpha = AlphaFromPercent(percent);
    if (alpha == 0) {
        Destroy();
        return;
    }

    if (!RegisterWindowClass()) {
        return;
    }

    const std::vector<RECT> monitorRects = GetMonitorRects();
    if (monitorRects.empty()) {
        return;
    }

    while (m_windows.size() > monitorRects.size()) {
        HWND hWnd = m_windows.back();
        m_windows.pop_back();
        if (IsWindow(hWnd)) {
            DestroyWindow(hWnd);
        }
    }

    HINSTANCE hInstance = GetModuleHandle(nullptr);
    for (size_t i = 0; i < monitorRects.size(); ++i) {
        const RECT& rect = monitorRects[i];
        HWND hWnd = (i < m_windows.size()) ? m_windows[i] : nullptr;

        if (!IsWindow(hWnd)) {
            hWnd = CreateWindowEx(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kDimOverlayClassName, L"", WS_POPUP, rect.left,
                                  rect.top, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hInstance, nullptr);

            if (i < m_windows.size()) {
                m_windows[i] = hWnd;
            } else {
                m_windows.push_back(hWnd);
            }
        }

        if (!hWnd) {
            continue;
        }

        SetLayeredWindowAttributes(hWnd, 0, alpha, LWA_ALPHA);
        SetWindowPos(hWnd, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    }
}

void DimOverlay::Destroy() {
    for (HWND hWnd : m_windows) {
        if (IsWindow(hWnd)) {
            DestroyWindow(hWnd);
        }
    }
    m_windows.clear();

    while (HWND hWnd = FindWindow(kDimOverlayClassName, nullptr)) {
        if (!DestroyWindow(hWnd)) {
            break;
        }
    }
}
