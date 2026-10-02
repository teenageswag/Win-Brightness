#include "DimOverlay.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace {
    constexpr const wchar_t* kDimOverlayClassName = L"TrenchesDimOverlay";
    constexpr double kOverlayCurve = 0.82;
    constexpr int kMaxOverlayAlpha = 244;

    LRESULT CALLBACK DimOverlayWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_ERASEBKGND: {
            RECT client{};
            GetClientRect(hWnd, &client);
            FillRect(reinterpret_cast<HDC>(wParam), &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC hdc = BeginPaint(hWnd, &paint);
            RECT client{};
            GetClientRect(hWnd, &client);
            FillRect(hdc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            EndPaint(hWnd, &paint);
            return 0;
        }
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        }

        return DefWindowProc(hWnd, message, wParam, lParam);
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

    WNDCLASSEX windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = DimOverlayWndProc;
    windowClass.hInstance = GetModuleHandle(nullptr);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = kDimOverlayClassName;
    registered = RegisterClassEx(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    return registered;
}

BYTE DimOverlay::AlphaFromPercent(int percent) const {
    if (percent >= kMaxBrightness) {
        return 0;
    }

    const double dimAmount = (kMaxBrightness - ClampBrightness(percent)) / 99.0;
    const double shapedAmount = std::pow(dimAmount, kOverlayCurve);
    return static_cast<BYTE>(std::clamp(static_cast<int>(kMaxOverlayAlpha * shapedAmount + 0.5), 0, kMaxOverlayAlpha));
}

void DimOverlay::Apply(int percent, const std::vector<MonitorInfo>& monitors) {
    const BYTE alpha = AlphaFromPercent(percent);
    if (alpha == 0 || monitors.empty()) {
        Destroy();
        return;
    }
    if (!RegisterWindowClass()) {
        return;
    }

    std::unordered_set<std::wstring> targetIds;
    for (const MonitorInfo& monitor : monitors) {
        targetIds.insert(monitor.id);
    }

    std::erase_if(m_windows, [&targetIds](const OverlayWindow& window) {
        if (targetIds.contains(window.monitorId)) {
            return false;
        }
        if (IsWindow(window.handle)) {
            DestroyWindow(window.handle);
        }
        return true;
    });

    const HINSTANCE instance = GetModuleHandle(nullptr);
    for (const MonitorInfo& monitor : monitors) {
        auto existing = std::ranges::find(m_windows, monitor.id, &OverlayWindow::monitorId);
        HWND window = existing != m_windows.end() ? existing->handle : nullptr;
        if (!IsWindow(window)) {
            window = CreateWindowEx(
                WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                kDimOverlayClassName, L"", WS_POPUP,
                monitor.bounds.left, monitor.bounds.top,
                monitor.bounds.right - monitor.bounds.left, monitor.bounds.bottom - monitor.bounds.top,
                nullptr, nullptr, instance, nullptr);

            if (existing != m_windows.end()) {
                existing->handle = window;
                existing->bounds = {};
                existing->alpha = 0;
            } else {
                m_windows.push_back({monitor.id, window});
                existing = std::prev(m_windows.end());
            }
        }

        if (!window) {
            continue;
        }

        if (existing->alpha != alpha) {
            if (!SetLayeredWindowAttributes(window, 0, alpha, LWA_ALPHA)) continue;
            existing->alpha = alpha;
        }
        if (!EqualRect(&existing->bounds, &monitor.bounds) || !IsWindowVisible(window)) {
            if (SetWindowPos(window, HWND_TOPMOST,
                             monitor.bounds.left, monitor.bounds.top,
                             monitor.bounds.right - monitor.bounds.left, monitor.bounds.bottom - monitor.bounds.top,
                             SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
                existing->bounds = monitor.bounds;
                RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            }
        }
    }
}

void DimOverlay::Destroy() {
    for (const OverlayWindow& window : m_windows) {
        if (IsWindow(window.handle)) {
            DestroyWindow(window.handle);
        }
    }
    m_windows.clear();
}
