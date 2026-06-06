#define NOMINMAX
#include "PopupView.h"
#include "../platform/Win32Helpers.h"
#include "../resources/resources.h"
#include <algorithm>
#include <cwchar>
#include <windowsx.h>

#pragma comment(lib, "dwmapi.lib")

namespace {
    constexpr COLORREF kColorBackground = RGB(0x12, 0x12, 0x12);
    constexpr COLORREF kColorMidBorder = RGB(0x33, 0x33, 0x33);
    constexpr COLORREF kColorText = RGB(0xD9, 0xD9, 0xD9);
    constexpr COLORREF kColorDisabled = RGB(0x55, 0x55, 0x55);
    constexpr COLORREF kColorToggleOn = RGB(0xD9, 0xD9, 0xD9);
    constexpr COLORREF kColorToggleOff = RGB(0x55, 0x55, 0x55);
    constexpr COLORREF kColorToggleThumb = RGB(0x12, 0x12, 0x12);

    Gdiplus::Color ToGdiColor(COLORREF color) { return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)); }

    LRESULT CALLBACK PopupWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        PopupView* popupView = nullptr;
        if (msg == WM_NCCREATE) {
            auto* createStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
            popupView = static_cast<PopupView*>(createStruct->lpCreateParams);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(popupView));
        } else {
            popupView = reinterpret_cast<PopupView*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
        }

        return popupView ? popupView->HandleMessage(hWnd, msg, wParam, lParam) : DefWindowProc(hWnd, msg, wParam, lParam);
    }
} // namespace

PopupView::PopupView(HINSTANCE hInstance, BrightnessController& controller) : m_hInstance(hInstance), m_controller(controller) {
    m_displayBrightness = m_controller.GetBrightness();
}

PopupView::~PopupView() {
    if (m_hWnd) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
}

bool PopupView::Register() {
    WNDCLASSEX wcex = {sizeof(wcex)};
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    wcex.lpfnWndProc = PopupWndProc;
    wcex.hInstance = m_hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = L"BrightnessPopup";
    return RegisterClassEx(&wcex) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool PopupView::Create() {
    m_hWnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"BrightnessPopup", L"",
        WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT,
        kBaseWidth, kBaseHeight,
        nullptr, nullptr, m_hInstance, this
    );
    return m_hWnd != nullptr;
}

void PopupView::Toggle(POINT cursorPt) {
    if (!m_hWnd) {
        return;
    }

    if (IsWindowVisible(m_hWnd)) {
        Hide();
        return;
    }

    m_displayBrightness = m_controller.GetBrightness();

    const int dpi = GetDpiForPoint(cursorPt);
    const int width = ScaleByDpi(kBaseWidth, dpi);
    const int height = ScaleByDpi(kBaseHeight, dpi);

    HMONITOR hMonitor = MonitorFromPoint(cursorPt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = {sizeof(monitorInfo)};
    GetMonitorInfo(hMonitor, &monitorInfo);
    const RECT& workArea = monitorInfo.rcWork;

    int x = cursorPt.x - width / 2;
    int y = workArea.bottom - height - ScaleByDpi(kPadding, dpi);
    x = std::clamp(x, static_cast<int>(workArea.left) + ScaleByDpi(8, dpi), static_cast<int>(workArea.right) - width - ScaleByDpi(8, dpi));
    y = std::clamp(y, static_cast<int>(workArea.top) + ScaleByDpi(8, dpi), static_cast<int>(workArea.bottom) - height - ScaleByDpi(8, dpi));

    SetWindowRgn(m_hWnd, nullptr, FALSE);

    m_showTime = GetTickCount64();
    SetWindowPos(m_hWnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    SetForegroundWindow(m_hWnd);
    SetFocus(m_hWnd);
    ResetAutoHideTimer();
}

void PopupView::Hide() {
    if (!m_hWnd) {
        return;
    }

    if (m_hasPendingBrightness) {
        CommitPendingBrightness();
    }
    KillTimer(m_hWnd, kAutoHideTimerId);
    KillTimer(m_hWnd, kDebounceTimerId);
    ShowWindow(m_hWnd, SW_HIDE);
}

bool PopupView::IsVisible() const { return m_hWnd && IsWindowVisible(m_hWnd); }

void PopupView::SetEnabled(bool enabled) {
    if (m_isEnabled == enabled) {
        return;
    }

    m_isEnabled = enabled;

    if (!enabled) {
        m_savedBrightness = m_displayBrightness;
        SetDisplayedBrightness(kMaxBrightness);
        CommitPendingBrightness();
        m_controller.SetBrightness(kMaxBrightness);
        NotifyOwnerBrightnessChanged(kMaxBrightness);
    } else {
        SetDisplayedBrightness(m_savedBrightness);
        CommitPendingBrightness();
        m_controller.SetBrightness(m_savedBrightness);
        NotifyOwnerBrightnessChanged(m_savedBrightness);
    }

    NotifyOwnerEnabledChanged(enabled);

    if (m_hWnd) {
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void PopupView::UpdateFromController() {
    m_displayBrightness = m_controller.GetBrightness();
    if (m_hWnd) {
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void PopupView::ResetAutoHideTimer() {
    if (m_hWnd && IsWindowVisible(m_hWnd)) {
        SetTimer(m_hWnd, kAutoHideTimerId, kAutoHideDelayMs, nullptr);
    }
}

void PopupView::ResetDebounceTimer() {
    if (m_hWnd) {
        m_hasPendingBrightness = true;
        KillTimer(m_hWnd, kDebounceTimerId);
        SetTimer(m_hWnd, kDebounceTimerId, kDebounceDelayMs, nullptr);
    }
}

void PopupView::CommitPendingBrightness() {
    if (!m_hasPendingBrightness) {
        return;
    }

    m_hasPendingBrightness = false;
    KillTimer(m_hWnd, kDebounceTimerId);
    m_controller.SetBrightness(m_displayBrightness);
    NotifyOwnerBrightnessChanged(m_displayBrightness);
}

void PopupView::SetDisplayedBrightness(int percent) {
    const int clamped = ClampBrightness(percent);
    if (clamped == m_displayBrightness) {
        return;
    }

    m_displayBrightness = clamped;
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void PopupView::NotifyOwnerBrightnessChanged(int percent) {
    if (HWND owner = GetWindow(m_hWnd, GW_OWNER)) {
        PostMessage(owner, WM_USER_BRIGHTNESS_CHANGED, static_cast<WPARAM>(percent), 0);
    }
}

void PopupView::NotifyOwnerEnabledChanged(bool enabled) {
    if (HWND owner = GetWindow(m_hWnd, GW_OWNER)) {
        PostMessage(owner, WM_USER_ENABLED_CHANGED, static_cast<WPARAM>(enabled ? 1 : 0), 0);
    }
}

void PopupView::GetToggleBounds(int dpi, RECT& rect) const {
    RECT clientRect;
    GetClientRect(m_hWnd, &clientRect);

    const int toggleW = ScaleByDpi(kToggleWidth, dpi);
    const int toggleH = ScaleByDpi(kToggleHeight, dpi);
    const int centerY = (clientRect.bottom - clientRect.top) / 2;
    const int left = ScaleByDpi(kToggleLeftMargin, dpi);

    rect.left = left;
    rect.top = centerY - toggleH / 2;
    rect.right = left + toggleW;
    rect.bottom = centerY + toggleH / 2;
}

void PopupView::GetTrackBounds(int dpi, int& left, int& right, int& centerY) const {
    RECT clientRect;
    GetClientRect(m_hWnd, &clientRect);

    // Track starts after toggle + gap
    left = ScaleByDpi(kToggleLeftMargin + kToggleWidth + kToggleTrackGap, dpi);
    right = clientRect.right - ScaleByDpi(kPadding + kPercentWidth + kTrackPercentGap, dpi);
    centerY = (clientRect.bottom - clientRect.top) / 2;
}

int PopupView::XToPercent(int x, int dpi) const {
    int left = 0;
    int right = 0;
    int centerY = 0;
    GetTrackBounds(dpi, left, right, centerY);
    const double ratio = static_cast<double>(x - left) / static_cast<double>(right - left);
    return ClampBrightness(static_cast<int>(std::clamp(ratio, 0.0, 1.0) * 99.0 + 1.0));
}

LRESULT PopupView::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT paintStruct;
        HDC paintDc = BeginPaint(hWnd, &paintStruct);
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        const int width = clientRect.right - clientRect.left;
        const int height = clientRect.bottom - clientRect.top;

        MemoryPaintDc memoryDc(paintDc, width, height);
        if (memoryDc.Get()) {
            using namespace Gdiplus;

            Graphics graphics(memoryDc.Get());
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);
            graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

            // Background
            SolidBrush backgroundBrush(ToGdiColor(kColorBackground));
            graphics.FillRectangle(&backgroundBrush, 0, 0, width, height);

            // Border
            graphics.SetSmoothingMode(SmoothingModeNone);
            Pen borderPen(ToGdiColor(kColorMidBorder), 1.0f);
            graphics.DrawRectangle(&borderPen, 0, 0, width - 1, height - 1);

            const int dpi = GetDpiForHwnd(hWnd);

            // --- Toggle switch ---
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);
            RECT toggleRect;
            GetToggleBounds(dpi, toggleRect);

            const int toggleW = toggleRect.right - toggleRect.left;
            const int toggleH = toggleRect.bottom - toggleRect.top;
            const int toggleRadius = toggleH / 2;

            const COLORREF toggleBgColor = m_isEnabled ? kColorToggleOn : kColorToggleOff;
            SolidBrush toggleBgBrush(ToGdiColor(toggleBgColor));

            // Draw toggle track
            graphics.SetSmoothingMode(SmoothingModeNone);
            graphics.FillRectangle(&toggleBgBrush, toggleRect.left, toggleRect.top, toggleW, toggleH);

            // thumb
            const int thumbSize = ScaleByDpi(kToggleThumbSize, dpi);
            const int thumbPad = (toggleH - thumbSize) / 2;
            const int thumbX = m_isEnabled ? (toggleRect.right - thumbSize - thumbPad) : (toggleRect.left + thumbPad);
            const int thumbY = toggleRect.top + thumbPad;

            SolidBrush thumbBrush(ToGdiColor(kColorToggleThumb));
            graphics.FillRectangle(&thumbBrush, thumbX, thumbY, thumbSize, thumbSize);
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);

            // --- Slider track ---
            graphics.SetSmoothingMode(SmoothingModeNone);
            int trackLeft = 0;
            int trackRight = 0;
            int trackCenterY = 0;
            GetTrackBounds(dpi, trackLeft, trackRight, trackCenterY);
            const int trackHeight = ScaleByDpi(kTrackHeight, dpi);

            const COLORREF trackActiveColor = m_isEnabled ? kColorText : kColorDisabled;
            const COLORREF trackInactiveColor = kColorMidBorder;

            const double ratio = m_displayBrightness / 100.0;
            const int thumbTrackX = trackLeft + static_cast<int>((trackRight - trackLeft) * ratio);

            Pen inactiveTrack(ToGdiColor(trackInactiveColor), static_cast<REAL>(trackHeight));
            graphics.DrawLine(&inactiveTrack, static_cast<REAL>(trackLeft), static_cast<REAL>(trackCenterY), static_cast<REAL>(trackRight),
                              static_cast<REAL>(trackCenterY));

            Pen activeTrack(ToGdiColor(trackActiveColor), static_cast<REAL>(trackHeight));
            graphics.DrawLine(&activeTrack, static_cast<REAL>(trackLeft), static_cast<REAL>(trackCenterY), static_cast<REAL>(thumbTrackX),
                              static_cast<REAL>(trackCenterY));

            // --- Percent text ---
            wchar_t percentText[8];
            swprintf_s(percentText, L"%d%%", m_displayBrightness);
            FontFamily percentFamily(L"Geist Mono");
            Font percentFont(&percentFamily, 9.0f, FontStyleRegular, UnitPoint);

            const COLORREF textColor = m_isEnabled ? kColorText : kColorDisabled;
            SolidBrush textBrush(ToGdiColor(textColor));
            StringFormat percentFormat;
            percentFormat.SetAlignment(StringAlignmentNear);
            percentFormat.SetLineAlignment(StringAlignmentCenter);

            // fixme: костыльно выравниваем текст, чтобы он был ровно между краем окна и слайдером. лучше бы делать это динамически
            int customGap = ScaleByDpi(19, dpi);

            RectF percentRect(static_cast<REAL>(trackRight + customGap), static_cast<REAL>(trackCenterY - ScaleByDpi(15, dpi)),
                              static_cast<REAL>(ScaleByDpi(kPercentWidth, dpi)), static_cast<REAL>(ScaleByDpi(30, dpi)));
            graphics.DrawString(percentText, -1, &percentFont, percentRect, &percentFormat, &textBrush);

            BitBlt(paintDc, 0, 0, width, height, memoryDc.Get(), 0, 0, SRCCOPY);
        }

        EndPaint(hWnd, &paintStruct);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        const int dpi = GetDpiForHwnd(hWnd);
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

        // Check if click is on toggle
        RECT toggleRect;
        GetToggleBounds(dpi, toggleRect);
        // Expand hit area a bit for easier clicking
        const int hitPad = ScaleByDpi(4, dpi);
        RECT toggleHit = {toggleRect.left - hitPad, toggleRect.top - hitPad, toggleRect.right + hitPad, toggleRect.bottom + hitPad};
        if (PtInRect(&toggleHit, point)) {
            SetEnabled(!m_isEnabled);
            ResetAutoHideTimer();
            return 0;
        }

        // Check if click is on slider track (only if enabled)
        if (m_isEnabled) {
            int trackLeft = 0;
            int trackRight = 0;
            int trackCenterY = 0;
            GetTrackBounds(dpi, trackLeft, trackRight, trackCenterY);
            const int pad = ScaleByDpi(kThumbRadius + 4, dpi);
            if (point.x >= trackLeft - pad && point.x <= trackRight + pad && point.y >= trackCenterY - ScaleByDpi(16, dpi) &&
                point.y <= trackCenterY + ScaleByDpi(16, dpi)) {
                m_isDragging = true;
                SetCapture(hWnd);
                SetDisplayedBrightness(XToPercent(point.x, dpi));
                ResetDebounceTimer();
                ResetAutoHideTimer();
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!m_isDragging) {
            return 0;
        }

        const int dpi = GetDpiForHwnd(hWnd);
        SetDisplayedBrightness(XToPercent(GET_X_LPARAM(lParam), dpi));
        ResetDebounceTimer();
        ResetAutoHideTimer();
        return 0;
    }

    case WM_LBUTTONUP:
        if (m_isDragging) {
            m_isDragging = false;
            ReleaseCapture();
            ResetDebounceTimer();
        }
        return 0;

    case WM_MOUSEWHEEL: {
        if (!m_isEnabled) {
            return 0;
        }
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1;
        SetDisplayedBrightness(m_displayBrightness + delta);
        ResetDebounceTimer();
        ResetAutoHideTimer();
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            Hide();
            return 0;
        }
        if (m_isEnabled) {
            if (wParam == VK_LEFT || wParam == VK_DOWN) {
                SetDisplayedBrightness(m_displayBrightness - 1);
                ResetDebounceTimer();
                ResetAutoHideTimer();
                return 0;
            }
            if (wParam == VK_RIGHT || wParam == VK_UP) {
                SetDisplayedBrightness(m_displayBrightness + 1);
                ResetDebounceTimer();
                ResetAutoHideTimer();
                return 0;
            }
        }
        break;

    case WM_TIMER:
        if (wParam == kAutoHideTimerId) {
            Hide();
            return 0;
        }
        if (wParam == kDebounceTimerId) {
            CommitPendingBrightness();
            return 0;
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && GetTickCount64() - m_showTime > 500) {
            Hide();
        }
        return 0;

    case WM_KILLFOCUS:
        if (GetTickCount64() - m_showTime > 500) {
            Hide();
        }
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
        if (hWnd == m_hWnd) {
            m_hWnd = nullptr;
        }
        return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}
