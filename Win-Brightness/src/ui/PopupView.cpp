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

    Gdiplus::Color ToGdiColor(COLORREF c) {
        return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
    }

    LRESULT CALLBACK PopupWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        PopupView* view = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<LPCREATESTRUCT>(lParam);
            view = static_cast<PopupView*>(cs->lpCreateParams);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
        }
        else {
            view = reinterpret_cast<PopupView*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
        }
        return view ? view->HandleMessage(hWnd, msg, wParam, lParam) : DefWindowProc(hWnd, msg, wParam, lParam);
    }
} // namespace

// =============================================================================
// Layout::Compute
// =============================================================================
void PopupView::Layout::Compute(const RECT& client, int dpi) {
    const int W = client.right - client.left;
    const int H = client.bottom - client.top;

    centerY = H / 2;

    const int padH = ScaleByDpi(kBasePaddingH, dpi);
    const int toggleW = ScaleByDpi(kBaseToggleW, dpi);
    const int toggleH = ScaleByDpi(kBaseToggleH, dpi);
    const int toggleGap = ScaleByDpi(kBaseToggleTrackGap, dpi);
    const int trackLabelGap = ScaleByDpi(kBaseTrackLabelGap, dpi);
    const int labelW = ScaleByDpi(kBaseLabelW, dpi);
    const int labelH = H; // full height — we'll center text inside

    trackH = ScaleByDpi(kBaseTrackH, dpi);

    // Toggle: left edge at padH, vertically centered
    toggle.left = padH;
    toggle.right = padH + toggleW;
    toggle.top = centerY - toggleH / 2;
    toggle.bottom = centerY + toggleH / 2;

    // Track: starts right after toggle+gap, ends at (right edge - padH - labelW - labelGap)
    trackLeft = toggle.right + toggleGap;
    trackRight = W - padH - labelW - trackLabelGap;
    trackCenterY = centerY;

    // Label rect: right-aligned, vertically centered
    const float lx = static_cast<float>(W - padH - labelW);
    const float ly = 0.0f;
    labelRect = Gdiplus::RectF(lx, ly, static_cast<float>(labelW), static_cast<float>(labelH));
}

// =============================================================================
// PopupView
// =============================================================================
PopupView::PopupView(HINSTANCE hInstance, BrightnessController& controller)
    : m_hInstance(hInstance), m_controller(controller) {
    m_displayBrightness = m_controller.GetBrightness();
}

PopupView::~PopupView() {
    if (m_hWnd) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
}

bool PopupView::Register() {
    WNDCLASSEX wcex = { sizeof(wcex) };
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
    if (!m_hWnd) return;

    if (IsWindowVisible(m_hWnd)) {
        Hide();
        return;
    }

    m_displayBrightness = m_controller.GetBrightness();

    const int dpi = GetDpiForPoint(cursorPt);
    const int width = ScaleByDpi(kBaseWidth, dpi);
    const int height = ScaleByDpi(kBaseHeight, dpi);

    HMONITOR hMonitor = MonitorFromPoint(cursorPt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMonitor, &mi);
    const RECT& work = mi.rcWork;

    const int margin = ScaleByDpi(8, dpi);
    int x = cursorPt.x - width / 2;
    int y = work.bottom - height - ScaleByDpi(kBasePaddingH, dpi);
    x = std::clamp(x, static_cast<int>(work.left) + margin, static_cast<int>(work.right) - width - margin);
    y = std::clamp(y, static_cast<int>(work.top) + margin, static_cast<int>(work.bottom) - height - margin);

    SetWindowRgn(m_hWnd, nullptr, FALSE);
    m_showTime = GetTickCount64();
    SetWindowPos(m_hWnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    SetForegroundWindow(m_hWnd);
    SetFocus(m_hWnd);
    ResetAutoHideTimer();
}

void PopupView::Hide() {
    if (!m_hWnd) return;
    if (m_hasPendingBrightness) CommitPendingBrightness();
    KillTimer(m_hWnd, kAutoHideTimerId);
    KillTimer(m_hWnd, kDebounceTimerId);
    ShowWindow(m_hWnd, SW_HIDE);
}

bool PopupView::IsVisible() const { return m_hWnd && IsWindowVisible(m_hWnd); }

void PopupView::SetEnabled(bool enabled) {
    if (m_isEnabled == enabled) return;

    m_isEnabled = enabled;
    if (!enabled) {
        m_savedBrightness = m_displayBrightness;
        SetDisplayedBrightness(kMaxBrightness);
        CommitPendingBrightness();
        m_controller.SetBrightness(kMaxBrightness);
        NotifyOwnerBrightnessChanged(kMaxBrightness);
    }
    else {
        SetDisplayedBrightness(m_savedBrightness);
        CommitPendingBrightness();
        m_controller.SetBrightness(m_savedBrightness);
        NotifyOwnerBrightnessChanged(m_savedBrightness);
    }

    NotifyOwnerEnabledChanged(enabled);
    if (m_hWnd) InvalidateRect(m_hWnd, nullptr, FALSE);
}

void PopupView::UpdateFromController() {
    m_displayBrightness = m_controller.GetBrightness();
    if (m_hWnd) InvalidateRect(m_hWnd, nullptr, FALSE);
}

void PopupView::ResetAutoHideTimer() {
    if (m_hWnd && IsWindowVisible(m_hWnd))
        SetTimer(m_hWnd, kAutoHideTimerId, kAutoHideDelayMs, nullptr);
}

void PopupView::ResetDebounceTimer() {
    if (m_hWnd) {
        m_hasPendingBrightness = true;
        KillTimer(m_hWnd, kDebounceTimerId);
        SetTimer(m_hWnd, kDebounceTimerId, kDebounceDelayMs, nullptr);
    }
}

void PopupView::CommitPendingBrightness() {
    if (!m_hasPendingBrightness) return;
    m_hasPendingBrightness = false;
    KillTimer(m_hWnd, kDebounceTimerId);
    m_controller.SetBrightness(m_displayBrightness);
    NotifyOwnerBrightnessChanged(m_displayBrightness);
}

void PopupView::SetDisplayedBrightness(int percent) {
    const int clamped = ClampBrightness(percent);
    if (clamped == m_displayBrightness) return;
    m_displayBrightness = clamped;
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void PopupView::NotifyOwnerBrightnessChanged(int percent) {
    if (HWND owner = GetWindow(m_hWnd, GW_OWNER))
        PostMessage(owner, WM_USER_BRIGHTNESS_CHANGED, static_cast<WPARAM>(percent), 0);
}

void PopupView::NotifyOwnerEnabledChanged(bool enabled) {
    if (HWND owner = GetWindow(m_hWnd, GW_OWNER))
        PostMessage(owner, WM_USER_ENABLED_CHANGED, static_cast<WPARAM>(enabled ? 1 : 0), 0);
}

PopupView::Layout PopupView::BuildLayout(int dpi) const {
    RECT client;
    GetClientRect(m_hWnd, &client);
    Layout layout;
    layout.Compute(client, dpi);
    return layout;
}

int PopupView::XToPercent(int x, const Layout& layout) const {
    const double ratio = static_cast<double>(x - layout.trackLeft) / static_cast<double>(layout.trackRight - layout.trackLeft);
    return ClampBrightness(static_cast<int>(std::clamp(ratio, 0.0, 1.0) * 99.0 + 1.0));
}

// =============================================================================
// WndProc
// =============================================================================
LRESULT PopupView::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT client;
        GetClientRect(hWnd, &client);
        const int W = client.right - client.left;
        const int H = client.bottom - client.top;

        MemoryPaintDc memDc(hdc, W, H);
        if (memDc.Get()) {
            using namespace Gdiplus;

            Graphics g(memDc.Get());
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

            // Background
            SolidBrush bgBrush(ToGdiColor(kColorBackground));
            g.FillRectangle(&bgBrush, 0, 0, W, H);

            // Border
            g.SetSmoothingMode(SmoothingModeNone);
            Pen borderPen(ToGdiColor(kColorMidBorder), 1.0f);
            g.DrawRectangle(&borderPen, 0, 0, W - 1, H - 1);

            const int dpi = GetDpiForHwnd(hWnd);
            const Layout lay = BuildLayout(dpi);

            // -----------------------------------------------------------------
            // Toggle switch
            // -----------------------------------------------------------------
            g.SetSmoothingMode(SmoothingModeNone);

            const int toggleW = lay.toggle.right - lay.toggle.left;
            const int toggleH = lay.toggle.bottom - lay.toggle.top;
            const int thumbSize = ScaleByDpi(kBaseToggleThumb, dpi);
            const int thumbPad = (toggleH - thumbSize) / 2;

            SolidBrush toggleBg(ToGdiColor(m_isEnabled ? kColorToggleOn : kColorToggleOff));
            g.FillRectangle(&toggleBg, lay.toggle.left, lay.toggle.top, toggleW, toggleH);

            const int thumbX = m_isEnabled
                ? (lay.toggle.right - thumbSize - thumbPad)
                : (lay.toggle.left + thumbPad);
            const int thumbY = lay.toggle.top + thumbPad;

            SolidBrush thumbBrush(ToGdiColor(kColorToggleThumb));
            g.FillRectangle(&thumbBrush, thumbX, thumbY, thumbSize, thumbSize);

            // -----------------------------------------------------------------
            // Slider track
            // -----------------------------------------------------------------
            const COLORREF activeColor = m_isEnabled ? kColorText : kColorDisabled;
            const float trackHf = static_cast<float>(lay.trackH);
            const float cy = static_cast<float>(lay.trackCenterY);

            const int visualBrightness = m_isEnabled ? m_displayBrightness : m_savedBrightness;
            const double ratio = visualBrightness / 100.0;
            const int thumbX_tr = lay.trackLeft + static_cast<int>((lay.trackRight - lay.trackLeft) * ratio);

            Pen inactivePen(ToGdiColor(kColorMidBorder), trackHf);
            g.DrawLine(&inactivePen, static_cast<REAL>(lay.trackLeft), cy, static_cast<REAL>(lay.trackRight), cy);

            Pen activePen(ToGdiColor(activeColor), trackHf);
            g.DrawLine(&activePen, static_cast<REAL>(lay.trackLeft), cy, static_cast<REAL>(thumbX_tr), cy);

            // -----------------------------------------------------------------
            // Percent label
            // -----------------------------------------------------------------
            wchar_t buf[8];
            swprintf_s(buf, L"%d%%", visualBrightness);

            FontFamily family(L"Geist Mono");
            Font font(&family, 9.0f, FontStyleRegular, UnitPoint);

            SolidBrush textBrush(ToGdiColor(m_isEnabled ? kColorText : kColorDisabled));

            StringFormat fmt;
            fmt.SetAlignment(StringAlignmentCenter);
            fmt.SetLineAlignment(StringAlignmentCenter);

            g.DrawString(buf, -1, &font, lay.labelRect, &fmt, &textBrush);

            BitBlt(hdc, 0, 0, W, H, memDc.Get(), 0, 0, SRCCOPY);
        }

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        const int   dpi = GetDpiForHwnd(hWnd);
        const Layout lay = BuildLayout(dpi);
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        // Toggle hit area (expand by 4 logical px for easier clicking)
        const int hitPad = ScaleByDpi(4, dpi);
        RECT toggleHit = {
            lay.toggle.left - hitPad, lay.toggle.top - hitPad,
            lay.toggle.right + hitPad, lay.toggle.bottom + hitPad
        };
        if (PtInRect(&toggleHit, pt)) {
            SetEnabled(!m_isEnabled);
            ResetAutoHideTimer();
            return 0;
        }

        if (m_isEnabled) {
            // Vertical hit area is half the window height above/below track center
            const int vPad = lay.trackCenterY / 2;
            const int hPad = ScaleByDpi(kBaseThumbRadius + 4, dpi);

            const bool inTrack =
                pt.x >= lay.trackLeft - hPad &&
                pt.x <= lay.trackRight + hPad &&
                pt.y >= lay.trackCenterY - vPad &&
                pt.y <= lay.trackCenterY + vPad;

            if (inTrack) {
                m_isDragging = true;
                SetCapture(hWnd);
                SetDisplayedBrightness(XToPercent(pt.x, lay));
                ResetDebounceTimer();
                ResetAutoHideTimer();
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!m_isDragging) return 0;
        const int   dpi = GetDpiForHwnd(hWnd);
        const Layout lay = BuildLayout(dpi);
        SetDisplayedBrightness(XToPercent(GET_X_LPARAM(lParam), lay));
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
        if (!m_isEnabled) return 0;
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1;
        SetDisplayedBrightness(m_displayBrightness + delta);
        ResetDebounceTimer();
        ResetAutoHideTimer();
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { Hide(); return 0; }
        if (m_isEnabled) {
            if (wParam == VK_LEFT || wParam == VK_DOWN) {
                SetDisplayedBrightness(m_displayBrightness - 1);
                ResetDebounceTimer(); ResetAutoHideTimer();
                return 0;
            }
            if (wParam == VK_RIGHT || wParam == VK_UP) {
                SetDisplayedBrightness(m_displayBrightness + 1);
                ResetDebounceTimer(); ResetAutoHideTimer();
                return 0;
            }
        }
        break;

    case WM_TIMER:
        if (wParam == kAutoHideTimerId) { Hide(); return 0; }
        if (wParam == kDebounceTimerId) { CommitPendingBrightness(); return 0; }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && GetTickCount64() - m_showTime > 500)
            Hide();
        return 0;

    case WM_KILLFOCUS:
        if (GetTickCount64() - m_showTime > 500)
            Hide();
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
        if (hWnd == m_hWnd) m_hWnd = nullptr;
        return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}