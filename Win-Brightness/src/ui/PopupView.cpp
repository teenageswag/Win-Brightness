#define NOMINMAX

#include "PopupView.h"
#include "../platform/Win32Helpers.h"
#include <algorithm>
#include <cwchar>
#include <string>
#include <windowsx.h>

namespace {
    constexpr COLORREF kBackground = RGB(0x0D, 0x0F, 0x10);
    constexpr COLORREF kSurface = RGB(0x17, 0x1A, 0x1C);
    constexpr COLORREF kRaised = RGB(0x20, 0x24, 0x27);
    constexpr COLORREF kBorder = RGB(0x34, 0x3A, 0x3D);
    constexpr COLORREF kText = RGB(0xF1, 0xF2, 0xEC);
    constexpr COLORREF kMuted = RGB(0x92, 0x9A, 0x96);
    constexpr COLORREF kDisabled = RGB(0x59, 0x60, 0x5D);
    constexpr COLORREF kAccent = RGB(0xC6, 0xF3, 0x6B);
    constexpr COLORREF kAccentText = RGB(0x10, 0x15, 0x0A);
    constexpr COLORREF kAccentDim = RGB(0x35, 0x45, 0x1F);

    struct Palette {
        COLORREF background;
        COLORREF surface;
        COLORREF raised;
        COLORREF border;
        COLORREF text;
        COLORREF muted;
        COLORREF disabled;
        COLORREF accent;
        COLORREF accentText;
        COLORREF accentDim;
    };

    Palette GetPalette() {
        HIGHCONTRAST contrast{sizeof(contrast)};
        if (SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
            (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0) {
            return {
                GetSysColor(COLOR_WINDOW),
                GetSysColor(COLOR_BTNFACE),
                GetSysColor(COLOR_BTNFACE),
                GetSysColor(COLOR_WINDOWTEXT),
                GetSysColor(COLOR_WINDOWTEXT),
                GetSysColor(COLOR_GRAYTEXT),
                GetSysColor(COLOR_GRAYTEXT),
                GetSysColor(COLOR_HIGHLIGHT),
                GetSysColor(COLOR_HIGHLIGHTTEXT),
                GetSysColor(COLOR_HIGHLIGHT)
            };
        }

        return {kBackground, kSurface, kRaised, kBorder, kText, kMuted, kDisabled, kAccent, kAccentText, kAccentDim};
    }

    Gdiplus::Color ToGdiColor(COLORREF value) {
        return Gdiplus::Color(255, GetRValue(value), GetGValue(value), GetBValue(value));
    }

    Gdiplus::RectF TextRect(const RECT& rect) {
        return {
            static_cast<Gdiplus::REAL>(rect.left),
            static_cast<Gdiplus::REAL>(rect.top),
            static_cast<Gdiplus::REAL>(rect.right - rect.left),
            static_cast<Gdiplus::REAL>(rect.bottom - rect.top)
        };
    }

    Gdiplus::Rect PixelRect(const RECT& rect) {
        return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
    }

    RECT Inset(RECT rect, int amount) {
        InflateRect(&rect, -amount, -amount);
        return rect;
    }

    bool Intersects(const RECT& left, const RECT& right) {
        RECT intersection{};
        return IntersectRect(&intersection, &left, &right) != FALSE;
    }

    void FillAndBorder(Gdiplus::Graphics& graphics, const RECT& rect, COLORREF fill, COLORREF border, float width = 1.0f) {
        Gdiplus::SolidBrush brush(ToGdiColor(fill));
        graphics.FillRectangle(&brush, PixelRect(rect));
        Gdiplus::Pen pen(ToGdiColor(border), width);
        graphics.DrawRectangle(
            &pen,
            static_cast<INT>(rect.left),
            static_cast<INT>(rect.top),
            static_cast<INT>(rect.right - rect.left - 1),
            static_cast<INT>(rect.bottom - rect.top - 1));
    }

    void DrawText(
        Gdiplus::Graphics& graphics,
        const std::wstring& text,
        const Gdiplus::Font& font,
        const RECT& rect,
        COLORREF color,
        Gdiplus::StringAlignment horizontal = Gdiplus::StringAlignmentNear,
        Gdiplus::StringAlignment vertical = Gdiplus::StringAlignmentCenter) {
        Gdiplus::StringFormat format;
        format.SetAlignment(horizontal);
        format.SetLineAlignment(vertical);
        format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        Gdiplus::SolidBrush brush(ToGdiColor(color));
        graphics.DrawString(text.c_str(), -1, &font, TextRect(rect), &format, &brush);
    }

    void DrawCheck(Gdiplus::Graphics& graphics, const RECT& rect, COLORREF color, int dpi) {
        Gdiplus::Pen pen(ToGdiColor(color), static_cast<Gdiplus::REAL>((std::max)(2, ScaleByDpi(2, dpi))));
        pen.SetStartCap(Gdiplus::LineCapSquare);
        pen.SetEndCap(Gdiplus::LineCapSquare);
        graphics.DrawLine(
            &pen,
            static_cast<Gdiplus::REAL>(rect.left + (rect.right - rect.left) * 0.20),
            static_cast<Gdiplus::REAL>(rect.top + (rect.bottom - rect.top) * 0.52),
            static_cast<Gdiplus::REAL>(rect.left + (rect.right - rect.left) * 0.43),
            static_cast<Gdiplus::REAL>(rect.top + (rect.bottom - rect.top) * 0.74));
        graphics.DrawLine(
            &pen,
            static_cast<Gdiplus::REAL>(rect.left + (rect.right - rect.left) * 0.43),
            static_cast<Gdiplus::REAL>(rect.top + (rect.bottom - rect.top) * 0.74),
            static_cast<Gdiplus::REAL>(rect.left + (rect.right - rect.left) * 0.82),
            static_cast<Gdiplus::REAL>(rect.top + (rect.bottom - rect.top) * 0.27));
    }

    void DrawMonitorIcon(Gdiplus::Graphics& graphics, const RECT& rect, COLORREF color, int dpi) {
        const float stroke = static_cast<Gdiplus::REAL>((std::max)(1, ScaleByDpi(1, dpi)));
        Gdiplus::Pen pen(ToGdiColor(color), stroke);
        const int standY = rect.bottom - ScaleByDpi(3, dpi);
        graphics.DrawRectangle(
            &pen,
            static_cast<INT>(rect.left),
            static_cast<INT>(rect.top),
            static_cast<INT>(rect.right - rect.left - 1),
            static_cast<INT>(rect.bottom - rect.top - ScaleByDpi(6, dpi)));
        const int centerX = (rect.left + rect.right) / 2;
        graphics.DrawLine(&pen, centerX, rect.bottom - ScaleByDpi(6, dpi), centerX, standY);
        graphics.DrawLine(&pen, rect.left + ScaleByDpi(5, dpi), standY, rect.right - ScaleByDpi(5, dpi), standY);
    }

    LRESULT CALLBACK PopupWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        PopupView* view = nullptr;
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<LPCREATESTRUCT>(lParam);
            view = static_cast<PopupView*>(create->lpCreateParams);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
        } else {
            view = reinterpret_cast<PopupView*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
        }

        return view ? view->HandleMessage(hWnd, message, wParam, lParam)
                    : DefWindowProc(hWnd, message, wParam, lParam);
    }
} // namespace

void PopupView::Layout::Compute(const RECT& client, int dpi, size_t monitorCount, int scrollOffset) {
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const int padding = ScaleByDpi(24, dpi);
    const int gap = ScaleByDpi(10, dpi);

    const int powerSize = ScaleByDpi(44, dpi);
    power = {width - padding - powerSize, padding, width - padding, padding + powerSize};

    brightnessCard = {
        padding,
        ScaleByDpi(84, dpi),
        width - padding,
        ScaleByDpi(196, dpi)
    };
    sliderLeft = brightnessCard.left + ScaleByDpi(18, dpi);
    sliderRight = brightnessCard.right - ScaleByDpi(18, dpi);
    sliderY = brightnessCard.bottom - ScaleByDpi(24, dpi);
    sliderHit = {sliderLeft, sliderY - ScaleByDpi(16, dpi), sliderRight, sliderY + ScaleByDpi(16, dpi)};

    const int modeTop = ScaleByDpi(236, dpi);
    const int modeBottom = modeTop + ScaleByDpi(46, dpi);
    const int contentWidth = width - padding * 2;
    const int halfWidth = (contentWidth - gap) / 2;
    softwareMode = {padding, modeTop, padding + halfWidth, modeBottom};
    hardwareMode = {softwareMode.right + gap, modeTop, width - padding, modeBottom};

    const int scopeTop = ScaleByDpi(326, dpi);
    const int scopeBottom = scopeTop + ScaleByDpi(44, dpi);
    allDisplays = {padding, scopeTop, padding + halfWidth, scopeBottom};
    selectedDisplays = {allDisplays.right + gap, scopeTop, width - padding, scopeBottom};

    autostart = {
        padding,
        height - padding - ScaleByDpi(48, dpi),
        width - padding,
        height - padding
    };
    monitorViewport = {
        padding,
        scopeBottom + ScaleByDpi(10, dpi),
        width - padding,
        autostart.top - ScaleByDpi(18, dpi)
    };

    monitorItemHeight = ScaleByDpi(44, dpi);
    monitorItemGap = ScaleByDpi(6, dpi);
    monitorItems.clear();
    monitorItems.reserve(monitorCount);
    int itemTop = monitorViewport.top - scrollOffset;
    for (size_t i = 0; i < monitorCount; ++i) {
        monitorItems.push_back({
            monitorViewport.left,
            itemTop,
            monitorViewport.right,
            itemTop + monitorItemHeight
        });
        itemTop += monitorItemHeight + monitorItemGap;
    }
}

PopupView::PopupView(HINSTANCE hInstance, PopupActions actions)
    : m_hInstance(hInstance), m_actions(std::move(actions)) {}

PopupView::~PopupView() {
    if (m_hWnd) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
}

bool PopupView::Register() {
    WNDCLASSEX windowClass{sizeof(windowClass)};
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    windowClass.lpfnWndProc = PopupWndProc;
    windowClass.hInstance = m_hInstance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = L"WinBrightnessControlCenter";
    return RegisterClassEx(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool PopupView::Create() {
    m_hWnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"WinBrightnessControlCenter", L"Win-Brightness",
        WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, kBaseWidth, kBaseHeight,
        nullptr, nullptr, m_hInstance, this);
    return m_hWnd != nullptr;
}

void PopupView::Toggle(POINT monitorPoint, bool keyboardInvoked) {
    if (!m_hWnd) {
        return;
    }
    if (IsVisible()) {
        Hide();
        return;
    }

    const HMONITOR monitor = MonitorFromPoint(monitorPoint, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfo(monitor, &info)) {
        return;
    }

    const int dpi = GetDpiForPoint(monitorPoint);
    const int margin = ScaleByDpi(16, dpi);
    const int workWidth = info.rcWork.right - info.rcWork.left;
    const int workHeight = info.rcWork.bottom - info.rcWork.top;
    const int width = (std::min)(ScaleByDpi(kBaseWidth, dpi), workWidth - margin * 2);
    const int preferredHeight = ScaleByDpi(kBaseHeight, dpi);
    const int minimumHeight = (std::min)(ScaleByDpi(kBaseMinimumHeight, dpi), workHeight - margin * 2);
    const int height = (std::max)(minimumHeight, (std::min)(preferredHeight, workHeight - margin * 2));
    const int x = info.rcWork.left + (workWidth - width) / 2;
    const int y = info.rcWork.top + (workHeight - height) / 2;

    m_showKeyboardFocus = keyboardInvoked;
    m_focus = {FocusKind::Slider, 0};
    m_hover = {};
    m_pressed = {};
    m_showTime = GetTickCount64();
    EnsureFocusedMonitorVisible();
    SetWindowPos(m_hWnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
    SetForegroundWindow(m_hWnd);
    SetFocus(m_hWnd);
    UpdateAccessibleName();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void PopupView::Hide() {
    if (!m_hWnd) {
        return;
    }
    CommitBrightness();
    KillTimer(m_hWnd, kBrightnessTimerId);
    if (GetCapture() == m_hWnd) {
        ReleaseCapture();
    }
    m_isDragging = false;
    m_pressed = {};
    ShowWindow(m_hWnd, SW_HIDE);
}

bool PopupView::IsVisible() const {
    return m_hWnd && IsWindowVisible(m_hWnd);
}

void PopupView::SetState(PopupState state) {
    state.brightness = ClampBrightness(state.brightness);
    m_state = std::move(state);
    if (m_focus.kind == FocusKind::Monitor && m_focus.monitorIndex >= m_state.monitors.size()) {
        m_focus = {FocusKind::SelectedDisplays, 0};
    }
    if (m_hWnd) {
        const Layout layout = BuildLayout();
        m_scrollOffset = std::clamp(m_scrollOffset, 0, MaximumScroll(layout));
        UpdateAccessibleName();
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

PopupView::Layout PopupView::BuildLayout() const {
    RECT client{};
    GetClientRect(m_hWnd, &client);
    Layout layout;
    layout.Compute(client, GetDpiForHwnd(m_hWnd), m_state.monitors.size(), m_scrollOffset);
    return layout;
}

std::vector<PopupView::FocusTarget> PopupView::FocusOrder() const {
    std::vector<FocusTarget> order = {
        {FocusKind::Power, 0},
        {FocusKind::Slider, 0},
        {FocusKind::SoftwareMode, 0},
        {FocusKind::HardwareMode, 0},
        {FocusKind::AllDisplays, 0},
        {FocusKind::SelectedDisplays, 0}
    };
    for (size_t i = 0; i < m_state.monitors.size(); ++i) {
        order.push_back({FocusKind::Monitor, i});
    }
    order.push_back({FocusKind::Autostart, 0});
    return order;
}

PopupView::FocusTarget PopupView::HitTest(POINT point, const Layout& layout) const {
    if (PtInRect(&layout.power, point)) return {FocusKind::Power, 0};
    if (PtInRect(&layout.sliderHit, point)) return {FocusKind::Slider, 0};
    if (PtInRect(&layout.softwareMode, point)) return {FocusKind::SoftwareMode, 0};
    if (PtInRect(&layout.hardwareMode, point)) return {FocusKind::HardwareMode, 0};
    if (PtInRect(&layout.allDisplays, point)) return {FocusKind::AllDisplays, 0};
    if (PtInRect(&layout.selectedDisplays, point)) return {FocusKind::SelectedDisplays, 0};
    if (PtInRect(&layout.monitorViewport, point)) {
        for (size_t i = 0; i < layout.monitorItems.size(); ++i) {
            if (PtInRect(&layout.monitorItems[i], point)) {
                return {FocusKind::Monitor, i};
            }
        }
    }
    if (PtInRect(&layout.autostart, point)) return {FocusKind::Autostart, 0};
    return {};
}

RECT PopupView::RectForTarget(const FocusTarget& target, const Layout& layout) const {
    switch (target.kind) {
    case FocusKind::Power: return layout.power;
    case FocusKind::Slider: return layout.sliderHit;
    case FocusKind::SoftwareMode: return layout.softwareMode;
    case FocusKind::HardwareMode: return layout.hardwareMode;
    case FocusKind::AllDisplays: return layout.allDisplays;
    case FocusKind::SelectedDisplays: return layout.selectedDisplays;
    case FocusKind::Monitor:
        if (target.monitorIndex < layout.monitorItems.size()) return layout.monitorItems[target.monitorIndex];
        break;
    case FocusKind::Autostart: return layout.autostart;
    case FocusKind::None: break;
    }
    return {};
}

void PopupView::SetFocusTarget(FocusTarget target, bool keyboardFocus) {
    m_focus = target;
    m_showKeyboardFocus = keyboardFocus;
    EnsureFocusedMonitorVisible();
    UpdateAccessibleName();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void PopupView::MoveFocus(bool backwards) {
    const std::vector<FocusTarget> order = FocusOrder();
    if (order.empty()) {
        return;
    }

    auto current = std::ranges::find(order, m_focus);
    size_t index = current == order.end() ? 0 : static_cast<size_t>(current - order.begin());
    if (backwards) {
        index = index == 0 ? order.size() - 1 : index - 1;
    } else {
        index = (index + 1) % order.size();
    }
    SetFocusTarget(order[index], true);
}

void PopupView::EnsureFocusedMonitorVisible() {
    if (!m_hWnd || m_focus.kind != FocusKind::Monitor || m_focus.monitorIndex >= m_state.monitors.size()) {
        return;
    }

    Layout layout = BuildLayout();
    const RECT item = layout.monitorItems[m_focus.monitorIndex];
    if (item.top < layout.monitorViewport.top) {
        m_scrollOffset -= layout.monitorViewport.top - item.top;
    } else if (item.bottom > layout.monitorViewport.bottom) {
        m_scrollOffset += item.bottom - layout.monitorViewport.bottom;
    }
    layout = BuildLayout();
    m_scrollOffset = std::clamp(m_scrollOffset, 0, MaximumScroll(layout));
}

void PopupView::Activate(const FocusTarget& target) {
    switch (target.kind) {
    case FocusKind::Power:
        m_state.enabled = !m_state.enabled;
        if (m_actions.setEnabled) m_actions.setEnabled(m_state.enabled);
        break;
    case FocusKind::SoftwareMode:
        m_state.mode = BrightnessMode::Software;
        if (m_actions.setMode) m_actions.setMode(m_state.mode);
        break;
    case FocusKind::HardwareMode:
        m_state.mode = BrightnessMode::Hardware;
        if (m_actions.setMode) m_actions.setMode(m_state.mode);
        break;
    case FocusKind::AllDisplays:
        m_state.selection.all = true;
        if (m_actions.setSelection) m_actions.setSelection(m_state.selection);
        break;
    case FocusKind::SelectedDisplays:
        m_state.selection.all = false;
        if (m_state.selection.ids.empty() && !m_state.monitors.empty()) {
            m_state.selection.ids.push_back(m_state.monitors.front().id);
        }
        if (m_actions.setSelection) m_actions.setSelection(m_state.selection);
        break;
    case FocusKind::Monitor:
        if (target.monitorIndex < m_state.monitors.size()) {
            const std::wstring& id = m_state.monitors[target.monitorIndex].id;
            if (m_state.selection.all) {
                m_state.selection.all = false;
                m_state.selection.ids = {id};
            } else {
                auto selected = std::ranges::find(m_state.selection.ids, id);
                if (selected == m_state.selection.ids.end()) {
                    m_state.selection.ids.push_back(id);
                } else if (m_state.selection.ids.size() > 1) {
                    m_state.selection.ids.erase(selected);
                }
            }
            if (m_actions.setSelection) m_actions.setSelection(m_state.selection);
        }
        break;
    case FocusKind::Autostart:
        m_state.autostart = !m_state.autostart;
        if (m_actions.setAutostart) m_actions.setAutostart(m_state.autostart);
        break;
    case FocusKind::Slider:
    case FocusKind::None:
        break;
    }

    UpdateAccessibleName();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void PopupView::UpdateAccessibleName() {
    if (!m_hWnd) {
        return;
    }

    std::wstring name;
    switch (m_focus.kind) {
    case FocusKind::Power:
        name = m_state.enabled ? L"Dimming on. Press Space to pause." : L"Dimming paused. Press Space to resume.";
        break;
    case FocusKind::Slider:
        name = L"Brightness " + std::to_wstring(m_state.brightness) + L" percent. Use arrow keys to adjust.";
        break;
    case FocusKind::SoftwareMode:
        name = m_state.mode == BrightnessMode::Software ? L"Software dimming, selected." : L"Software dimming.";
        break;
    case FocusKind::HardwareMode:
        name = m_state.mode == BrightnessMode::Hardware ? L"Hardware DDC CI, selected." : L"Hardware DDC CI.";
        break;
    case FocusKind::AllDisplays:
        name = m_state.selection.all ? L"All displays, selected." : L"All displays.";
        break;
    case FocusKind::SelectedDisplays:
        name = !m_state.selection.all ? L"Selected displays, selected." : L"Selected displays.";
        break;
    case FocusKind::Monitor:
        if (m_focus.monitorIndex < m_state.monitors.size()) {
            const MonitorInfo& monitor = m_state.monitors[m_focus.monitorIndex];
            name = L"Display " + std::to_wstring(m_focus.monitorIndex + 1) + L", " + monitor.name;
            name += m_state.selection.Contains(monitor.id) ? L", included." : L", not included.";
            name += monitor.hardwareBrightness ? L" DDC CI available." : L" DDC CI unavailable.";
        }
        break;
    case FocusKind::Autostart:
        name = m_state.autostart ? L"Start with Windows, on." : L"Start with Windows, off.";
        break;
    case FocusKind::None:
        name = L"Win-Brightness";
        break;
    }

    SetWindowText(m_hWnd, name.c_str());
    if (IsVisible()) {
        NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, m_hWnd, OBJID_WINDOW, CHILDID_SELF);
    }
}

int PopupView::XToBrightness(int x, const Layout& layout) const {
    const double ratio = static_cast<double>(x - layout.sliderLeft) /
                         static_cast<double>(layout.sliderRight - layout.sliderLeft);
    return ClampBrightness(static_cast<int>(std::clamp(ratio, 0.0, 1.0) * 99.0 + 1.0));
}

void PopupView::SetDisplayedBrightness(int percent) {
    const int brightness = ClampBrightness(percent);
    if (brightness == m_state.brightness) {
        return;
    }
    m_state.brightness = brightness;
    UpdateAccessibleName();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void PopupView::QueueBrightnessCommit() {
    m_hasPendingBrightness = true;
    KillTimer(m_hWnd, kBrightnessTimerId);
    SetTimer(m_hWnd, kBrightnessTimerId, kBrightnessDelayMs, nullptr);
}

void PopupView::CommitBrightness() {
    if (!m_hasPendingBrightness) {
        return;
    }
    m_hasPendingBrightness = false;
    KillTimer(m_hWnd, kBrightnessTimerId);
    if (m_actions.setBrightness) {
        m_actions.setBrightness(m_state.brightness);
    }
}

void PopupView::ScrollMonitors(int direction) {
    Layout layout = BuildLayout();
    m_scrollOffset = std::clamp(
        m_scrollOffset + direction * (layout.monitorItemHeight + layout.monitorItemGap),
        0,
        MaximumScroll(layout));
    InvalidateRect(m_hWnd, &layout.monitorViewport, FALSE);
}

int PopupView::MaximumScroll(const Layout& layout) const {
    if (m_state.monitors.empty()) {
        return 0;
    }
    const int contentHeight = static_cast<int>(m_state.monitors.size()) * layout.monitorItemHeight +
                              static_cast<int>(m_state.monitors.size() - 1) * layout.monitorItemGap;
    return (std::max)(0, contentHeight - static_cast<int>(layout.monitorViewport.bottom - layout.monitorViewport.top));
}

LRESULT PopupView::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC target = BeginPaint(hWnd, &paint);
        RECT client{};
        GetClientRect(hWnd, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        const int dpi = GetDpiForHwnd(hWnd);
        const Layout layout = BuildLayout();
        const Palette palette = GetPalette();

        MemoryPaintDc buffer(target, width, height);
        if (buffer.Get()) {
            using namespace Gdiplus;
            Graphics graphics(buffer.Get());
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);
            graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

            FontFamily family(L"Bahnschrift");
            Font eyebrow(&family, 8.0f, FontStyleRegular, UnitPoint);
            Font title(&family, 19.0f, FontStyleBold, UnitPoint);
            Font section(&family, 9.0f, FontStyleBold, UnitPoint);
            Font body(&family, 10.0f, FontStyleRegular, UnitPoint);
            Font bodyBold(&family, 10.0f, FontStyleBold, UnitPoint);
            Font detail(&family, 8.5f, FontStyleRegular, UnitPoint);
            Font value(&family, 38.0f, FontStyleBold, UnitPoint);

            SolidBrush background(ToGdiColor(palette.background));
            graphics.FillRectangle(&background, 0, 0, width, height);
            graphics.SetSmoothingMode(SmoothingModeNone);
            Pen windowBorder(ToGdiColor(palette.border), 1.0f);
            graphics.DrawRectangle(&windowBorder, 0, 0, width - 1, height - 1);

            const int padding = ScaleByDpi(24, dpi);
            const std::wstring eyebrowText = m_state.hotkeyAvailable
                ? L"WIN-BRIGHTNESS  /  CTRL+ALT+B"
                : L"WIN-BRIGHTNESS  /  TRAY CLICK";
            DrawText(graphics, eyebrowText, eyebrow,
                     {padding, ScaleByDpi(14, dpi), layout.power.left - ScaleByDpi(12, dpi), ScaleByDpi(36, dpi)},
                     palette.muted);
            DrawText(graphics, L"Display dimmer", title,
                     {padding, ScaleByDpi(36, dpi), layout.power.left - ScaleByDpi(12, dpi), ScaleByDpi(72, dpi)},
                     palette.text);

            const bool powerHovered = m_hover.kind == FocusKind::Power;
            FillAndBorder(
                graphics,
                layout.power,
                m_state.enabled ? palette.accent : (powerHovered ? palette.raised : palette.surface),
                m_state.enabled ? palette.accent : palette.border);
            if (m_state.enabled) {
                SolidBrush icon(ToGdiColor(palette.accentText));
                const int barWidth = ScaleByDpi(4, dpi);
                const int barHeight = ScaleByDpi(16, dpi);
                const int centerX = (layout.power.left + layout.power.right) / 2;
                const int centerY = (layout.power.top + layout.power.bottom) / 2;
                graphics.FillRectangle(&icon, centerX - ScaleByDpi(7, dpi), centerY - barHeight / 2, barWidth, barHeight);
                graphics.FillRectangle(&icon, centerX + ScaleByDpi(3, dpi), centerY - barHeight / 2, barWidth, barHeight);
            } else {
                PointF triangle[] = {
                    {static_cast<REAL>(layout.power.left + ScaleByDpi(17, dpi)), static_cast<REAL>(layout.power.top + ScaleByDpi(13, dpi))},
                    {static_cast<REAL>(layout.power.left + ScaleByDpi(17, dpi)), static_cast<REAL>(layout.power.bottom - ScaleByDpi(13, dpi))},
                    {static_cast<REAL>(layout.power.right - ScaleByDpi(13, dpi)), static_cast<REAL>((layout.power.top + layout.power.bottom) / 2)}
                };
                SolidBrush icon(ToGdiColor(palette.text));
                graphics.FillPolygon(&icon, triangle, 3);
            }

            FillAndBorder(graphics, layout.brightnessCard, palette.surface, palette.border);
            DrawText(graphics, L"Brightness", section,
                     {layout.brightnessCard.left + ScaleByDpi(18, dpi), layout.brightnessCard.top + ScaleByDpi(10, dpi),
                      layout.brightnessCard.right - ScaleByDpi(140, dpi), layout.brightnessCard.top + ScaleByDpi(34, dpi)},
                     m_state.enabled ? palette.muted : palette.disabled);
            DrawText(graphics, m_state.enabled ? L"Active" : L"Paused", detail,
                     {layout.brightnessCard.left + ScaleByDpi(18, dpi), layout.brightnessCard.top + ScaleByDpi(36, dpi),
                      layout.brightnessCard.right - ScaleByDpi(140, dpi), layout.brightnessCard.top + ScaleByDpi(58, dpi)},
                     m_state.enabled ? palette.accent : palette.muted);
            DrawText(graphics, std::to_wstring(m_state.brightness) + L"%", value,
                     {layout.brightnessCard.right - ScaleByDpi(150, dpi), layout.brightnessCard.top + ScaleByDpi(10, dpi),
                      layout.brightnessCard.right - ScaleByDpi(18, dpi), layout.brightnessCard.top + ScaleByDpi(70, dpi)},
                     m_state.enabled ? palette.text : palette.disabled,
                     StringAlignmentFar);

            const double sliderRatio = (m_state.brightness - kMinBrightness) / 99.0;
            const int thumbX = layout.sliderLeft + static_cast<int>((layout.sliderRight - layout.sliderLeft) * sliderRatio);
            const int trackHeight = ScaleByDpi(4, dpi);
            SolidBrush trackBrush(ToGdiColor(palette.raised));
            graphics.FillRectangle(&trackBrush, layout.sliderLeft, layout.sliderY - trackHeight / 2,
                                   layout.sliderRight - layout.sliderLeft, trackHeight);
            SolidBrush activeTrack(ToGdiColor(m_state.enabled ? palette.accent : palette.disabled));
            graphics.FillRectangle(&activeTrack, layout.sliderLeft, layout.sliderY - trackHeight / 2,
                                   thumbX - layout.sliderLeft, trackHeight);
            const int thumbSize = ScaleByDpi(14, dpi);
            SolidBrush thumbBrush(ToGdiColor(m_state.enabled ? palette.text : palette.disabled));
            graphics.FillRectangle(&thumbBrush, thumbX - thumbSize / 2, layout.sliderY - thumbSize / 2, thumbSize, thumbSize);

            DrawText(graphics, L"Method", section,
                     {padding, layout.softwareMode.top - ScaleByDpi(27, dpi), width - padding, layout.softwareMode.top - ScaleByDpi(5, dpi)},
                     palette.muted);

            auto drawChoice = [&](const RECT& rect, bool selected, bool hovered, const std::wstring& label, const std::wstring& detailText) {
                const COLORREF fill = selected ? palette.accent : (hovered ? palette.raised : palette.surface);
                const COLORREF border = selected ? palette.accent : palette.border;
                const COLORREF primary = selected ? palette.accentText : palette.text;
                const COLORREF secondary = selected ? palette.accentText : palette.muted;
                FillAndBorder(graphics, rect, fill, border);
                DrawText(graphics, label, bodyBold,
                         {rect.left + ScaleByDpi(14, dpi), rect.top + ScaleByDpi(3, dpi), rect.right - ScaleByDpi(12, dpi), rect.top + ScaleByDpi(25, dpi)},
                         primary);
                DrawText(graphics, detailText, detail,
                         {rect.left + ScaleByDpi(14, dpi), rect.top + ScaleByDpi(22, dpi), rect.right - ScaleByDpi(12, dpi), rect.bottom - ScaleByDpi(2, dpi)},
                         secondary);
            };
            drawChoice(layout.softwareMode, m_state.mode == BrightnessMode::Software,
                       m_hover.kind == FocusKind::SoftwareMode, L"Software", L"Per-display overlay");
            drawChoice(layout.hardwareMode, m_state.mode == BrightnessMode::Hardware,
                       m_hover.kind == FocusKind::HardwareMode, L"Hardware", L"DDC / CI");

            size_t selectedCount = 0;
            size_t hardwareCount = 0;
            for (const MonitorInfo& monitor : m_state.monitors) {
                if (m_state.selection.Contains(monitor.id)) {
                    ++selectedCount;
                    if (monitor.hardwareBrightness) ++hardwareCount;
                }
            }
            std::wstring displaySummary = L"Displays  ·  " + std::to_wstring(selectedCount) + L" / " +
                                          std::to_wstring(m_state.monitors.size());
            if (m_state.mode == BrightnessMode::Hardware && selectedCount > 0 && hardwareCount < selectedCount) {
                displaySummary += L"  ·  DDC " + std::to_wstring(hardwareCount) + L" / " + std::to_wstring(selectedCount);
            }
            DrawText(graphics, displaySummary, section,
                     {padding, layout.allDisplays.top - ScaleByDpi(27, dpi), width - padding, layout.allDisplays.top - ScaleByDpi(5, dpi)},
                     palette.muted);

            drawChoice(layout.allDisplays, m_state.selection.all,
                       m_hover.kind == FocusKind::AllDisplays, L"All displays", L"Keep every screen in sync");
            drawChoice(layout.selectedDisplays, !m_state.selection.all,
                       m_hover.kind == FocusKind::SelectedDisplays, L"Selected", L"Choose screens below");

            const GraphicsState clipState = graphics.Save();
            graphics.SetClip(PixelRect(layout.monitorViewport));
            for (size_t i = 0; i < m_state.monitors.size(); ++i) {
                const RECT& item = layout.monitorItems[i];
                if (!Intersects(item, layout.monitorViewport)) {
                    continue;
                }

                const MonitorInfo& monitor = m_state.monitors[i];
                const bool included = m_state.selection.Contains(monitor.id);
                const bool hovered = m_hover.kind == FocusKind::Monitor && m_hover.monitorIndex == i;
                FillAndBorder(graphics, item, hovered ? palette.raised : palette.surface,
                              included ? palette.accentDim : palette.border);

                RECT iconRect = {
                    item.left + ScaleByDpi(14, dpi),
                    item.top + ScaleByDpi(12, dpi),
                    item.left + ScaleByDpi(38, dpi),
                    item.bottom - ScaleByDpi(9, dpi)
                };
                DrawMonitorIcon(graphics, iconRect, included ? palette.accent : palette.muted, dpi);

                std::wstring monitorLabel = L"Display " + std::to_wstring(i + 1) + L"  ·  " + monitor.name;
                DrawText(graphics, monitorLabel, bodyBold,
                         {item.left + ScaleByDpi(52, dpi), item.top + ScaleByDpi(2, dpi), item.right - ScaleByDpi(46, dpi), item.top + ScaleByDpi(24, dpi)},
                         included ? palette.text : palette.muted);

                const int monitorWidth = monitor.bounds.right - monitor.bounds.left;
                const int monitorHeight = monitor.bounds.bottom - monitor.bounds.top;
                std::wstring monitorDetail = std::to_wstring(monitorWidth) + L" × " + std::to_wstring(monitorHeight);
                if (monitor.primary) monitorDetail += L"  ·  Primary";
                monitorDetail += monitor.hardwareBrightness ? L"  ·  DDC/CI" : L"  ·  No DDC/CI";
                DrawText(graphics, monitorDetail, detail,
                         {item.left + ScaleByDpi(52, dpi), item.top + ScaleByDpi(21, dpi), item.right - ScaleByDpi(46, dpi), item.bottom - ScaleByDpi(2, dpi)},
                         monitor.hardwareBrightness ? palette.muted : palette.disabled);

                RECT checkRect = {
                    item.right - ScaleByDpi(31, dpi),
                    item.top + ScaleByDpi(14, dpi),
                    item.right - ScaleByDpi(17, dpi),
                    item.top + ScaleByDpi(28, dpi)
                };
                FillAndBorder(graphics, checkRect, included ? palette.accent : palette.background,
                              included ? palette.accent : palette.border);
                if (included) DrawCheck(graphics, checkRect, palette.accentText, dpi);
            }

            if (m_showKeyboardFocus && GetFocus() == hWnd && m_focus.kind == FocusKind::Monitor) {
                RECT focusRect = RectForTarget(m_focus, layout);
                focusRect = Inset(focusRect, ScaleByDpi(2, dpi));
                Pen focusPen(ToGdiColor(palette.accent), static_cast<REAL>((std::max)(2, ScaleByDpi(2, dpi))));
                graphics.DrawRectangle(
                    &focusPen,
                    static_cast<INT>(focusRect.left),
                    static_cast<INT>(focusRect.top),
                    static_cast<INT>(focusRect.right - focusRect.left - 1),
                    static_cast<INT>(focusRect.bottom - focusRect.top - 1));
            }
            graphics.Restore(clipState);

            const bool autoHovered = m_hover.kind == FocusKind::Autostart;
            FillAndBorder(graphics, layout.autostart, autoHovered ? palette.raised : palette.surface, palette.border);
            DrawText(graphics, L"Start with Windows", bodyBold,
                     {layout.autostart.left + ScaleByDpi(14, dpi), layout.autostart.top + ScaleByDpi(3, dpi),
                      layout.autostart.right - ScaleByDpi(48, dpi), layout.autostart.top + ScaleByDpi(26, dpi)},
                     palette.text);
            DrawText(graphics, L"Launch quietly in the notification area", detail,
                     {layout.autostart.left + ScaleByDpi(14, dpi), layout.autostart.top + ScaleByDpi(23, dpi),
                      layout.autostart.right - ScaleByDpi(48, dpi), layout.autostart.bottom - ScaleByDpi(2, dpi)},
                     palette.muted);
            RECT autoCheck = {
                layout.autostart.right - ScaleByDpi(34, dpi),
                layout.autostart.top + ScaleByDpi(15, dpi),
                layout.autostart.right - ScaleByDpi(18, dpi),
                layout.autostart.top + ScaleByDpi(31, dpi)
            };
            FillAndBorder(graphics, autoCheck, m_state.autostart ? palette.accent : palette.background,
                          m_state.autostart ? palette.accent : palette.border);
            if (m_state.autostart) DrawCheck(graphics, autoCheck, palette.accentText, dpi);

            if (m_showKeyboardFocus && GetFocus() == hWnd && m_focus.kind != FocusKind::Monitor && m_focus.kind != FocusKind::None) {
                RECT focusRect = RectForTarget(m_focus, layout);
                focusRect = Inset(focusRect, ScaleByDpi(2, dpi));
                Pen focusPen(ToGdiColor(palette.accent), static_cast<REAL>((std::max)(2, ScaleByDpi(2, dpi))));
                graphics.DrawRectangle(
                    &focusPen,
                    static_cast<INT>(focusRect.left),
                    static_cast<INT>(focusRect.top),
                    static_cast<INT>(focusRect.right - focusRect.left - 1),
                    static_cast<INT>(focusRect.bottom - focusRect.top - 1));
            }

            BitBlt(target, 0, 0, width, height, buffer.Get(), 0, 0, SRCCOPY);
        }

        EndPaint(hWnd, &paint);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        const Layout layout = BuildLayout();
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const FocusTarget target = HitTest(point, layout);
        SetFocus(hWnd);
        SetFocusTarget(target, false);
        if (target.kind == FocusKind::Slider && m_state.enabled) {
            m_isDragging = true;
            SetCapture(hWnd);
            SetDisplayedBrightness(XToBrightness(point.x, layout));
            QueueBrightnessCommit();
        } else if (target.kind != FocusKind::None) {
            m_pressed = target;
            SetCapture(hWnd);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!m_trackingMouse) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hWnd, 0};
            TrackMouseEvent(&tracking);
            m_trackingMouse = true;
        }

        const Layout layout = BuildLayout();
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const FocusTarget hover = HitTest(point, layout);
        if (!(hover == m_hover)) {
            m_hover = hover;
            InvalidateRect(hWnd, nullptr, FALSE);
        }

        if (m_isDragging) {
            SetDisplayedBrightness(XToBrightness(point.x, layout));
            QueueBrightnessCommit();
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        m_trackingMouse = false;
        m_hover = {};
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONUP: {
        const Layout layout = BuildLayout();
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (m_isDragging) {
            m_isDragging = false;
            CommitBrightness();
        } else if (m_pressed.kind != FocusKind::None && HitTest(point, layout) == m_pressed) {
            Activate(m_pressed);
        }
        m_pressed = {};
        if (GetCapture() == hWnd) ReleaseCapture();
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hWnd, &point);
        const Layout layout = BuildLayout();
        const int direction = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1;
        if (PtInRect(&layout.monitorViewport, point) && MaximumScroll(layout) > 0) {
            ScrollMonitors(direction);
        } else if (m_state.enabled) {
            SetFocusTarget({FocusKind::Slider, 0}, false);
            SetDisplayedBrightness(m_state.brightness - direction);
            QueueBrightnessCommit();
        }
        return 0;
    }

    case WM_KEYDOWN: {
        if (wParam == VK_ESCAPE) {
            Hide();
            return 0;
        }
        if (wParam == VK_TAB) {
            MoveFocus((GetKeyState(VK_SHIFT) & 0x8000) != 0);
            return 0;
        }
        if (wParam == VK_SPACE || wParam == VK_RETURN) {
            Activate(m_focus);
            return 0;
        }

        if (m_focus.kind == FocusKind::Slider && m_state.enabled) {
            int next = m_state.brightness;
            if (wParam == VK_LEFT || wParam == VK_DOWN) --next;
            else if (wParam == VK_RIGHT || wParam == VK_UP) ++next;
            else if (wParam == VK_PRIOR) next += 10;
            else if (wParam == VK_NEXT) next -= 10;
            else if (wParam == VK_HOME) next = kMinBrightness;
            else if (wParam == VK_END) next = kMaxBrightness;
            else break;
            SetFocusTarget(m_focus, true);
            SetDisplayedBrightness(next);
            QueueBrightnessCommit();
            return 0;
        }

        if ((m_focus.kind == FocusKind::SoftwareMode || m_focus.kind == FocusKind::HardwareMode) &&
            (wParam == VK_LEFT || wParam == VK_RIGHT)) {
            SetFocusTarget({wParam == VK_LEFT ? FocusKind::SoftwareMode : FocusKind::HardwareMode, 0}, true);
            Activate(m_focus);
            return 0;
        }
        if ((m_focus.kind == FocusKind::AllDisplays || m_focus.kind == FocusKind::SelectedDisplays) &&
            (wParam == VK_LEFT || wParam == VK_RIGHT)) {
            SetFocusTarget({wParam == VK_LEFT ? FocusKind::AllDisplays : FocusKind::SelectedDisplays, 0}, true);
            Activate(m_focus);
            return 0;
        }
        if (m_focus.kind == FocusKind::Monitor && !m_state.monitors.empty() &&
            (wParam == VK_UP || wParam == VK_DOWN)) {
            size_t index = m_focus.monitorIndex;
            if (wParam == VK_UP && index > 0) --index;
            if (wParam == VK_DOWN && index + 1 < m_state.monitors.size()) ++index;
            SetFocusTarget({FocusKind::Monitor, index}, true);
            return 0;
        }
        break;
    }

    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS;

    case WM_TIMER:
        if (wParam == kBrightnessTimerId) {
            CommitBrightness();
            return 0;
        }
        break;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hWnd, &point);
            const FocusTarget target = HitTest(point, BuildLayout());
            SetCursor(LoadCursor(nullptr, target.kind == FocusKind::None ? IDC_ARROW : IDC_HAND));
            return TRUE;
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && GetTickCount64() - m_showTime > 200) {
            Hide();
        }
        return 0;

    case WM_KILLFOCUS:
        if (GetTickCount64() - m_showTime > 200) Hide();
        return 0;

    case WM_SETFOCUS:
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
        if (hWnd == m_hWnd) m_hWnd = nullptr;
        return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}
