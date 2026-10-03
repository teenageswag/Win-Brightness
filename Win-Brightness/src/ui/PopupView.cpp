#include "PopupView.h"
#include "island/Spring.h"
#include "island/FrameClock.h"
#include "island/Dismissal.h"
#include "../platform/Win32Helpers.h"
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <commctrl.h>
#include <windowsx.h>
#include <array>

#pragma comment(lib, "comctl32.lib")

namespace {
constexpr UINT_PTR kBrightnessTimer = 1;
constexpr UINT_PTR kGraphicsTimer = 2;
constexpr UINT_PTR kDismissTimer = 3;
constexpr UINT kBrightnessInterval = 70;
constexpr wchar_t kWindowClass[] = L"TrenchesDynamicIsland";
struct KeyboardShortcut { int id; UINT key; UINT command; UINT modifiers; };
constexpr UINT kControlModifiers = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
constexpr std::array<KeyboardShortcut, 12> kShortcuts{{
    {21, VK_LEFT, VK_LEFT, kControlModifiers}, {22, VK_RIGHT, VK_RIGHT, kControlModifiers},
    {23, VK_UP, VK_UP, kControlModifiers}, {24, VK_DOWN, VK_DOWN, kControlModifiers},
    {25, VK_HOME, VK_HOME, kControlModifiers}, {26, VK_END, VK_END, kControlModifiers},
    {27, VK_PRIOR, VK_PRIOR, kControlModifiers}, {28, VK_NEXT, VK_NEXT, kControlModifiers},
    {29, 'N', VK_TAB, kControlModifiers}, {30, VK_RETURN, VK_RETURN, kControlModifiers},
    {32, VK_ESCAPE, VK_ESCAPE, kControlModifiers},
    {33, 'N', VK_TAB, kControlModifiers | MOD_SHIFT}
}};
}

struct PopupView::Impl {
    HINSTANCE instance;
    HWND window = nullptr, tooltip = nullptr;
    PopupActions actions;
    PopupState state;
    island::Preferences preferences;
    std::unique_ptr<island::Renderer> renderer;
    island::FrameClock clock;
    island::Spring width{island::kWidth}, height{island::kMinimumHeight};
    island::Spring visibility{1, island::kFeedbackSpring};
    struct RowMotion {
        island::Spring slider{kDefaultBrightness, island::kSliderSpring};
        island::Spring number{0, island::kFeedbackSpring};
    };
    std::map<std::wstring, RowMotion> rows;
    MonitorBrightnessValues pending;
    island::Spring mode{0, island::kFeedbackSpring}, feedback{1, island::kFeedbackSpring};
    island::Target focus{island::Control::Slider, 0}, hover{}, pressed{};
    bool dirty = true, dragging = false, tracking = false;
    bool brightnessTimer = false, hiding = false, keyboardMode = false;
    bool reducedMotion = false, repositioning = false, finishingDrag = false;
    bool dismissTimer = false, mouseRegistered = false, contextMenu = false;
    island::IdleDismissal dismissal;
    std::optional<POINT> lastPointer;
    std::wstring dragId, tooltipText, wheelId;
    int wheelRemainder = 0;
    float scroll = 0, maximumHeight = 449, maximumWidth = island::kWidth;
    UINT dpi = 96, graphicsRetries = 0;
    HRESULT renderError = S_OK;
    std::array<bool, kShortcuts.size()> shortcuts{};
    DWORD shortcutError = ERROR_SUCCESS;
    LARGE_INTEGER frequency{}, lastFrame{};

    Impl(HINSTANCE module, PopupActions callbacks) : instance(module), actions(std::move(callbacks)) {
        QueryPerformanceFrequency(&frequency);
        RefreshMotionPreference();
    }
    ~Impl() {
        if (window) {
            StopDismissal();
            StopMouseInput();
            KillTimer(window, kBrightnessTimer); KillTimer(window, kGraphicsTimer);
            UnregisterShortcuts();
            if (GetCapture() == window) ReleaseCapture();
            clock.Stop(); renderer.reset(); DestroyWindow(window);
        }
    }
    bool Animate() const { return preferences.animations && !reducedMotion; }
    void StopDismissal() {
        dismissal.Cancel(); dismissTimer = false;
        if (window) KillTimer(window, kDismissTimer);
    }
    void ResetDismissal() {
        if (!window || !IsWindowVisible(window) || hiding || contextMenu || GetCapture() == window) return;
        dismissal.Reset(GetTickCount64());
        if (!dismissTimer) dismissTimer = SetTimer(window, kDismissTimer, island::IdleDismissal::kInterval, nullptr) != 0;
    }
    void StopMouseInput() {
        if (!mouseRegistered) return;
        const RAWINPUTDEVICE device{1, 2, RIDEV_REMOVE, nullptr};
        if (!RegisterRawInputDevices(&device, 1, sizeof(device)) && actions.reportUiError) {
            const DWORD error = GetLastError(); actions.reportUiError(error ? HRESULT_FROM_WIN32(error) : E_FAIL);
        }
        mouseRegistered = false;
    }
    void StartMouseInput() {
        if (mouseRegistered) return;
        // Observe clicks without suppressing legacy input or activating the
        // overlay. This application is the sole owner of raw mouse registration.
        const RAWINPUTDEVICE device{1, 2, RIDEV_INPUTSINK, window};
        mouseRegistered = RegisterRawInputDevices(&device, 1, sizeof(device)) != FALSE;
        if (!mouseRegistered && actions.reportUiError) {
            const DWORD error = GetLastError();
            actions.reportUiError(error ? HRESULT_FROM_WIN32(error) : E_FAIL);
        }
    }
    void UnregisterShortcuts() {
        for (size_t i = 0; i < kShortcuts.size(); ++i) {
            if (shortcuts[i]) UnregisterHotKey(window, kShortcuts[i].id);
            shortcuts[i] = false;
        }
    }
    void RegisterShortcuts() {
        UnregisterShortcuts(); shortcutError = ERROR_SUCCESS;
        for (size_t i = 0; i < kShortcuts.size(); ++i) {
            shortcuts[i] = RegisterHotKey(window, kShortcuts[i].id, kShortcuts[i].modifiers, kShortcuts[i].key) != FALSE;
            if (!shortcuts[i] && shortcutError == ERROR_SUCCESS) {
                shortcutError = GetLastError();
                if (!shortcutError) shortcutError = ERROR_GEN_FAILURE;
            }
        }
    }
    size_t Primary() const {
        const auto primary = std::ranges::find_if(state.monitors, [](const auto& monitor) { return monitor.primary; });
        return primary == state.monitors.end() ? 0 : static_cast<size_t>(primary - state.monitors.begin());
    }
    MONITORINFO Monitor() const {
        MONITORINFO info{sizeof(info)};
        const size_t primary = Primary();
        if (primary < state.monitors.size() && GetMonitorInfoW(state.monitors[primary].handle, &info)) return info;
        GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &info);
        return info;
    }
    float Scale() const { return static_cast<float>(dpi) / 96.0f; }
    void RequestFrame() {
        dirty = true;
        if (window && renderer && IsWindowVisible(window)) clock.Request();
        if (window) PostMessageW(window, WM_NULL, 0, 0);
    }
    bool Moving() const {
        return width.Active(0.05) || height.Active(0.05) || visibility.Active() ||
            mode.Active() || feedback.Active() || std::ranges::any_of(rows, [](const auto& entry) {
                return entry.second.slider.Active(0.05) || entry.second.number.Active();
            });
    }
    island::Layout Layout() const {
        return island::Layout::Build(static_cast<float>(width.Value()), static_cast<float>(height.Value()),
            state.monitors.size(), scroll, maximumWidth);
    }
    void Targets() {
        width.Target(maximumWidth);
        height.Target(island::PanelHeight(state.monitors.size(), maximumHeight, maximumWidth));
        if (!Animate()) {
            width.Snap(width.Target()); height.Snap(height.Target());
        }
        RequestFrame();
    }
    void RefreshMotionPreference() {
        BOOL enabled = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
        reducedMotion = !enabled;
    }
    void MeasureMonitor(bool updateDpi) {
        const auto info = Monitor();
        if (updateDpi) dpi = static_cast<UINT>(GetDpiForPoint({info.rcMonitor.left + 1, info.rcMonitor.top + 1}));
        maximumWidth = std::max(200.0f, std::min(island::kWidth,
            static_cast<float>(info.rcMonitor.right - info.rcMonitor.left) / Scale() - island::kShadowMargin * 2));
        maximumHeight = std::max(island::kMinimumHeight, std::min(island::PanelHeight(5, 10000, maximumWidth),
            static_cast<float>(info.rcWork.bottom - info.rcMonitor.top) / Scale() - island::kShadowMargin));
    }
    void Position() {
        if (!window || repositioning) return;
        const auto info = Monitor();
        const int w = static_cast<int>(std::ceil((width.Value() + island::kShadowMargin * 2) * Scale()));
        const int h = static_cast<int>(std::ceil((height.Value() + island::kShadowMargin) * Scale()));
        // No top shadow inset: both states stay on the screen's top edge.
        const int x = info.rcMonitor.left + (info.rcMonitor.right - info.rcMonitor.left - w) / 2;
        const int y = info.rcMonitor.top;
        RECT current{};
        if (GetWindowRect(window, &current) && current.left == x && current.top == y &&
            current.right - current.left == w && current.bottom - current.top == h) return;
        repositioning = true;
        SetWindowPos(window, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
        repositioning = false;
    }
    HRESULT InitializeRenderer() {
        clock.Stop();
        renderer.reset();
        renderer = std::make_unique<island::Renderer>();
        const HRESULT result = renderer->Initialize(window, instance, dpi, maximumHeight);
        if (FAILED(result)) renderer.reset();
        else if (!clock.Start(renderer->FrameHandle())) {
            renderer.reset(); return E_OUTOFMEMORY;
        }
        else { renderError = S_OK; graphicsRetries = 0; }
        return result;
    }
    void GraphicsFailure(HRESULT error) {
        const bool changed = renderError != error;
        renderError = error; clock.Stop(); renderer.reset(); dirty = false;
        if (changed && actions.reportUiError) actions.reportUiError(error);
        if (graphicsRetries < 3 && window && IsWindowVisible(window)) {
            ++graphicsRetries; SetTimer(window, kGraphicsTimer, 1000, nullptr);
        }
    }
    island::Presentation Presentation() const {
        return island::PresentPanel(static_cast<float>(height.Value()), visibility.Value());
    }
    island::Point ClientPoint(float pixelX, float pixelY) const {
        RECT client{}; GetClientRect(window, &client);
        const auto presentation = Presentation();
        const float center = static_cast<float>(client.right) / Scale() * 0.5f;
        return {(pixelX / Scale() - center) / presentation.scale + static_cast<float>(width.Value()) * 0.5f,
                (pixelY / Scale() - presentation.offsetY) / presentation.scale};
    }
    island::Point Mouse(LPARAM param) const {
        return ClientPoint(static_cast<float>(GET_X_LPARAM(param)), static_cast<float>(GET_Y_LPARAM(param)));
    }
    double BrightnessAt(float x, size_t index) const {
        const auto layout = Layout();
        const auto row = std::ranges::find_if(layout.rows, [index](const auto& item) { return item.index == index; });
        auto rect = row == layout.rows.end() ? island::Rect{} : row->slider;
        rect.left += 10; rect.right -= 10;
        if (rect.Width() <= 0) return index < state.monitors.size() ? state.monitors[index].brightness : kDefaultBrightness;
        const float ratio = std::clamp((x - rect.left) / rect.Width(), 0.0f, 1.0f);
        return static_cast<double>(ratio) * 99.0 + 1.0;
    }
    bool Adjustable(size_t index) const {
        return index < state.monitors.size() && (state.mode == BrightnessMode::Software ||
            (state.monitors[index].hardwareBrightness && state.monitors[index].hardwareStatus == HardwareStatus::Available));
    }
    void AccessibleName() {
        std::wstring name = L"trenches. ";
        switch (focus.control) {
        case island::Control::Slider:
            if (focus.monitor < state.monitors.size()) name += state.monitors[focus.monitor].name + L". ";
            if (focus.monitor < state.monitors.size()) name += L"Brightness " +
                std::to_wstring(state.monitors[focus.monitor].brightness) + L" percent.";
            if (!Adjustable(focus.monitor)) name += L" Hardware brightness unavailable.";
            break;
        case island::Control::Monitor:
            if (focus.monitor < state.monitors.size()) name += state.monitors[focus.monitor].name;
            break;
        case island::Control::Software: name += L"Software dimming."; break;
        case island::Control::Hardware: name += L"Hardware DDC CI."; break;
        case island::Control::Power: name += state.enabled ? L"Disable dimming." : L"Enable dimming."; break;
        case island::Control::None: break;
        }
        if (FAILED(renderError)) name += L" Graphics unavailable.";
        if (keyboardMode && shortcutError) name += L" Some keyboard shortcuts are unavailable. Windows error " +
            std::to_wstring(shortcutError) + L".";
        SetWindowTextW(window, name.c_str());
        if (IsWindowVisible(window)) NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, window, OBJID_WINDOW, CHILDID_SELF);
    }
    void Tooltip(bool show) {
        if (!tooltip) return;
        TOOLINFOW tool{sizeof(tool)}; tool.hwnd = window; tool.uId = 1;
        if (show) {
            tool.lpszText = tooltipText.data();
            SendMessageW(tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
            POINT position{}; GetCursorPos(&position);
            SendMessageW(tooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(position.x + 10, position.y + 24));
        }
        SendMessageW(tooltip, TTM_TRACKACTIVATE, show, reinterpret_cast<LPARAM>(&tool));
    }
    void CreateTooltip() {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES}; InitCommonControlsEx(&controls);
        tooltip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, window, nullptr, instance, nullptr);
        if (!tooltip) return;
        TOOLINFOW tool{sizeof(tool)}; tool.hwnd = window; tool.uId = 1; tool.uFlags = TTF_TRACK | TTF_ABSOLUTE;
        tool.lpszText = const_cast<wchar_t*>(L"");
        SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, 420);
    }
    void Hover(island::Target control) {
        if (hover == control) return;
        hover = control; feedback.Target(control.control == island::Control::None ? 1.0 : 1.015);
        Tooltip(false);
        if (control.control == island::Control::Monitor && control.monitor < state.monitors.size()) {
            tooltipText = state.monitors[control.monitor].name;
            const DWORD error = state.monitors[control.monitor].hardwareError;
            if (error) tooltipText += L"\nDDC/CI error " + std::to_wstring(error) + L".";
            Tooltip(true);
        }
        RequestFrame();
    }
    void QueueBrightness() {
        // Throttle without restarting a debounce on each pointer message.
        // Optimistic UI is immediate; the backend receives at most ~14 targets/s.
        if (!brightnessTimer) {
            brightnessTimer = SetTimer(window, kBrightnessTimer, kBrightnessInterval, nullptr) != 0;
            if (!brightnessTimer) CommitBrightness();
        }
    }
    void CommitBrightness() {
        if (brightnessTimer) KillTimer(window, kBrightnessTimer);
        brightnessTimer = false;
        const auto targets = pending;
        for (const auto& [id, value] : targets) {
            pending.erase(id);
            const auto found = std::ranges::find(state.monitors, id, &MonitorInfo::id);
            if (found != state.monitors.end() && Adjustable(static_cast<size_t>(found - state.monitors.begin())) && actions.setMonitorBrightness)
                actions.setMonitorBrightness(id, value);
        }
    }
    void DisplayBrightness(size_t index, int value, bool direct) {
        if (!Adjustable(index)) return;
        value = ClampBrightness(value);
        auto& monitor = state.monitors[index];
        if (monitor.brightness == value) return;
        monitor.brightness = value; pending[monitor.id] = value;
        auto& motion = rows[monitor.id];
        if (direct || !Animate()) motion.slider.Snap(value); else motion.slider.Target(value);
        motion.number.Target(1.0); QueueBrightness(); AccessibleName(); RequestFrame();
    }
    void DragTo(float x, size_t index) {
        if (!Adjustable(index)) return;
        const std::wstring id = state.monitors[index].id;
        const double value = BrightnessAt(x, index);
        DisplayBrightness(index, static_cast<int>(std::lround(value)), true);
        // Keep the painted fill continuous between integer hardware targets.
        // A failed throttle timer dispatches synchronously; its callback can
        // refresh the catalog and cancel capture before this function returns.
        const auto motion = rows.find(id);
        if (dragging && dragId == id && motion != rows.end()) motion->second.slider.Snap(value);
        RequestFrame();
    }
    void SelectSliderTarget(size_t index) {
        if (index < state.monitors.size()) focus = {island::Control::Slider, index};
    }
    void FinishDrag(bool commit = true) {
        if (finishingDrag) return;
        const auto id = dragId;
        finishingDrag = true; dragging = false; pressed = {}; dragId.clear();
        feedback.Target(hover.control == island::Control::None ? 1.0 : 1.015);
        if (GetCapture() == window) ReleaseCapture();
        if (commit) CommitBrightness();
        else pending.erase(id);
        const auto monitor = std::ranges::find(state.monitors, id, &MonitorInfo::id);
        if (monitor != state.monitors.end()) rows[id].slider.Target(monitor->brightness);
        if (pending.empty()) { brightnessTimer = false; KillTimer(window, kBrightnessTimer); }
        finishingDrag = false; RequestFrame();
        ResetDismissal();
    }
    void EnsureFocusVisible() {
        if (focus.control != island::Control::Slider && focus.control != island::Control::Monitor) return;
        auto layout = Layout();
        if (focus.monitor >= layout.rows.size()) return;
        const auto& row = layout.rows[focus.monitor];
        if (row.label.top < layout.viewport.top) scroll -= layout.viewport.top - row.label.top;
        if (row.slider.bottom > layout.viewport.bottom) scroll += row.slider.bottom - layout.viewport.bottom;
        scroll = std::clamp(scroll, 0.0f, layout.MaximumScroll());
    }
    std::vector<island::Target> FocusOrder() const {
        std::vector<island::Target> order;
        const auto layout = Layout();
        for (const auto& row : layout.rows) {
            if (Adjustable(row.index)) order.push_back({island::Control::Slider, row.index});
        }
        {
            order.push_back({island::Control::Software}); order.push_back({island::Control::Hardware});
        }
        order.push_back({island::Control::Power});
        return order;
    }
    void MoveFocus(bool backwards) {
        const auto order = FocusOrder();
        auto current = std::ranges::find(order, focus);
        size_t index = current == order.end() ? 0 : static_cast<size_t>(current - order.begin());
        index = backwards ? (index + order.size() - 1) % order.size() : (index + 1) % order.size();
        focus = order[index]; EnsureFocusVisible(); AccessibleName(); RequestFrame();
        NotifyWinEvent(EVENT_OBJECT_FOCUS, window, OBJID_WINDOW, CHILDID_SELF);
    }
    void Activate(island::Target control) {
        CommitBrightness();
        switch (control.control) {
        case island::Control::Power:
            state.enabled = !state.enabled;
            if (actions.setEnabled) actions.setEnabled(state.enabled);
            break;
        case island::Control::Software:
        case island::Control::Hardware:
            state.mode = control.control == island::Control::Software ? BrightnessMode::Software : BrightnessMode::Hardware;
            mode.Target(state.mode == BrightnessMode::Hardware ? 1.0 : 0.0);
            if (actions.setMode) actions.setMode(state.mode);
            break;
        case island::Control::Monitor: SelectSliderTarget(control.monitor); break;
        case island::Control::Slider:
        case island::Control::None: break;
        }
        AccessibleName(); RequestFrame();
    }
};

PopupView::PopupView(HINSTANCE instance, PopupActions actions) : m_impl(std::make_unique<Impl>(instance, std::move(actions))) {}
PopupView::~PopupView() = default;
HWND PopupView::GetHWnd() const { return m_impl->window; }
bool PopupView::IsVisible() const { return m_impl->window && IsWindowVisible(m_impl->window); }
island::Layout PopupView::GetLayout() const { return m_impl->Layout(); }
island::Presentation PopupView::GetPresentation() const { return m_impl->Presentation(); }
HRESULT PopupView::LastRenderError() const { return m_impl->renderError; }
island::Preferences PopupView::GetPreferences() const { return m_impl->preferences; }

LRESULT CALLBACK PopupView::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    PopupView* popup = reinterpret_cast<PopupView*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        popup = static_cast<PopupView*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        popup->m_impl->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(popup));
    }
    try {
        return popup ? popup->HandleMessage(window, message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
    } catch (...) {
        // Never unwind a C++ exception through the User32 callback boundary.
        if (popup) { popup->m_impl->renderError = E_OUTOFMEMORY; popup->m_impl->dirty = false; }
        if (GetCapture() == window) ReleaseCapture();
        ShowWindow(window, SW_HIDE);
        return message == WM_NCCREATE ? FALSE : 0;
    }
}
bool PopupView::Register() {
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = WindowProc; windowClass.hInstance = m_impl->instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW); windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass)) return true;
    const DWORD error = GetLastError();
    if (error == ERROR_CLASS_ALREADY_EXISTS) return true;
    m_impl->renderError = error ? HRESULT_FROM_WIN32(error) : E_FAIL;
    return false;
}
bool PopupView::Create() {
    auto& r = *m_impl;
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
        kWindowClass, L"trenches", WS_POPUP, 0, 0, 376, 80, nullptr, nullptr, r.instance, this);
    if (!window) {
        const DWORD error = GetLastError(); r.renderError = error ? HRESULT_FROM_WIN32(error) : E_FAIL;
        return false;
    }
    r.MeasureMonitor(true); r.Targets(); r.Position();
    const HRESULT hr = r.InitializeRenderer();
    if (FAILED(hr)) { r.renderError = hr; return false; }
    r.CreateTooltip(); return true;
}
void PopupView::Toggle(POINT monitorPoint, bool keyboardInvoked) {
    (void)monitorPoint;
    auto& r = *m_impl;
    if (!r.window) return;
    if (IsVisible() && !r.hiding) { Hide(true); return; }
    r.MeasureMonitor(true); r.Targets();
    if (!IsVisible()) { r.width.Snap(r.width.Target()); r.height.Snap(r.height.Target()); }
    r.Position();
    if (!r.renderer) {
        const HRESULT result = r.InitializeRenderer();
        if (FAILED(result)) { r.GraphicsFailure(result); return; }
    } else {
        const HRESULT result = r.renderer->Resize(r.dpi, r.maximumHeight);
        if (FAILED(result)) { r.GraphicsFailure(result); return; }
    }
    if (!IsVisible()) {
        r.visibility.Snap(r.Animate() ? 0.0 : 1.0);
        const HRESULT result = r.renderer->Conceal();
        if (FAILED(result)) { r.GraphicsFailure(result); return; }
    }
    r.hiding = false; r.visibility.Target(1.0); r.lastFrame = {};
    r.wheelId.clear(); r.wheelRemainder = 0;
    r.keyboardMode = keyboardInvoked; r.focus = {island::Control::Slider, r.Primary()};
    ShowWindow(r.window, SW_SHOWNOACTIVATE);
    // Keyboard navigation uses temporary Ctrl+Alt shortcuts, rather than taking
    // focus or intercepting the foreground application's unmodified key events.
    if (keyboardInvoked) r.RegisterShortcuts();
    r.StartMouseInput(); r.ResetDismissal();
    r.AccessibleName(); r.RequestFrame();
}
void PopupView::Hide(bool animated) {
    auto& r = *m_impl;
    if (!r.window) return;
    r.hiding = true; r.StopDismissal(); r.StopMouseInput();
    r.UnregisterShortcuts();
    r.FinishDrag(); r.Tooltip(false);
    if (animated && r.Animate() && IsVisible() && r.renderer) {
        r.hiding = true; r.visibility.Target(0.0); r.RequestFrame();
    } else {
        r.hiding = false; r.visibility.Snap(0);
        r.width.Snap(std::min(island::kWidth, r.maximumWidth));
        r.height.Snap(island::PanelHeight(r.state.monitors.size(), r.maximumHeight, r.maximumWidth));
        r.keyboardMode = false;
        if (r.renderer) {
            const HRESULT result = r.renderer->Conceal();
            if (FAILED(result)) r.GraphicsFailure(result);
        }
        ShowWindow(r.window, SW_HIDE); r.dirty = false; r.lastFrame = {};
        KillTimer(r.window, kGraphicsTimer);
    }
}
void PopupView::SetState(PopupState state) {
    auto& r = *m_impl;
    const bool monitorFocus = r.focus.control == island::Control::Slider || r.focus.control == island::Control::Monitor;
    const std::wstring focusedId = monitorFocus && r.focus.monitor < r.state.monitors.size()
        ? r.state.monitors[r.focus.monitor].id : L"";
    for (auto& monitor : state.monitors) {
        monitor.brightness = ClampBrightness(monitor.brightness);
        const auto pending = r.pending.find(monitor.id);
        if (pending != r.pending.end() && (state.mode == BrightnessMode::Software ||
            (monitor.hardwareBrightness && monitor.hardwareStatus == HardwareStatus::Available))) monitor.brightness = pending->second;
    }
    r.state = std::move(state);
    std::erase_if(r.pending, [&](const auto& entry) {
        const auto monitor = std::ranges::find(r.state.monitors, entry.first, &MonitorInfo::id);
        return monitor == r.state.monitors.end() || !r.Adjustable(static_cast<size_t>(monitor - r.state.monitors.begin()));
    });
    std::erase_if(r.rows, [&](const auto& entry) {
        return std::ranges::find(r.state.monitors, entry.first, &MonitorInfo::id) == r.state.monitors.end();
    });
    for (const auto& monitor : r.state.monitors) {
        const auto [motion, inserted] = r.rows.try_emplace(monitor.id);
        if (inserted) motion->second.slider.Snap(monitor.brightness);
        else if (!r.dragging || r.dragId != monitor.id) motion->second.slider.Target(monitor.brightness);
    }
    if (r.dragging) {
        const auto found = std::ranges::find(r.state.monitors, r.dragId, &MonitorInfo::id);
        if (found == r.state.monitors.end() || !r.Adjustable(static_cast<size_t>(found - r.state.monitors.begin())))
            r.FinishDrag(false);
        else r.pressed.monitor = static_cast<size_t>(found - r.state.monitors.begin());
    }
    if (!focusedId.empty()) {
        const auto found = std::ranges::find(r.state.monitors, focusedId, &MonitorInfo::id);
        r.focus = found == r.state.monitors.end() ? island::Target{island::Control::Power}
            : island::Target{r.focus.control, static_cast<size_t>(found - r.state.monitors.begin())};
    }
    if (r.focus.monitor >= r.state.monitors.size()) r.focus = {island::Control::Power};
    r.mode.Target(r.state.mode == BrightnessMode::Hardware ? 1.0 : 0.0);
    if (!r.window) return;
    r.MeasureMonitor(true); r.Targets();
    r.scroll = std::clamp(r.scroll, 0.0f, r.Layout().MaximumScroll());
    r.Tooltip(false); r.AccessibleName(); if (IsVisible()) r.Position();
}
void PopupView::SetPreferences(island::Preferences preferences) {
    auto& r = *m_impl;
    r.preferences = preferences; r.RefreshMotionPreference();
    if (r.renderer) r.renderer->RefreshTheme();
    r.Targets(); r.RequestFrame();
}
HANDLE PopupView::FrameWaitHandle() const {
    const auto& r = *m_impl;
    return IsVisible() && r.renderer && (r.dirty || r.Moving()) ? r.clock.Ready() : nullptr;
}
void PopupView::RenderFrame() {
    auto& r = *m_impl;
    if (!r.renderer || !IsVisible()) return;
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    const double seconds = r.lastFrame.QuadPart ? static_cast<double>(now.QuadPart - r.lastFrame.QuadPart) /
        static_cast<double>(r.frequency.QuadPart) : 0.0;
    r.lastFrame = now;
    auto advance = [&](island::Spring& spring, double epsilon = 0.001) {
        if (r.Animate()) spring.Advance(seconds, epsilon); else spring.Snap(spring.Target());
    };
    advance(r.width, 0.05); advance(r.height, 0.05); advance(r.visibility);
    advance(r.mode); advance(r.feedback);
    for (auto& [id, motion] : r.rows) {
        advance(motion.slider, 0.05); advance(motion.number);
        if (motion.number.Target() > 0.5 && motion.number.Value() > 0.9) motion.number.Target(0.0);
    }
    r.Position();
    island::Frame frame;
    frame.state = &r.state; frame.layout = r.Layout();
    frame.presentation = r.Presentation();
    frame.radius = std::min(32.0f, static_cast<float>(r.height.Value()) * 0.14f);
    frame.modePosition = static_cast<float>(r.mode.Value());
    for (const auto& monitor : r.state.monitors) {
        const auto& motion = r.rows.at(monitor.id);
        frame.rowValues.push_back(static_cast<float>(motion.slider.Value()));
        frame.rowFeedback.push_back(static_cast<float>(motion.number.Value()));
    }
    frame.hot = r.pressed.control != island::Control::None ? r.pressed : r.hover;
    frame.hotScale = static_cast<float>(r.feedback.Value());
    frame.focus = r.focus; frame.keyboardFocus = r.keyboardMode; frame.preferences = r.preferences;
    const HRESULT result = r.renderer->Draw(frame);
    if (FAILED(result)) { r.GraphicsFailure(result); return; }
    r.dirty = false;
    if (r.hiding && !r.visibility.Active()) Hide(false);
    if (r.Moving() && IsVisible()) r.clock.Request();
    if (!r.Moving()) r.lastFrame = {};
}

void PopupView::NotifyPointerDown(POINT screenPoint) {
    auto& r = *m_impl;
    if (!IsVisible() || r.hiding || r.contextMenu || GetCapture() == r.window) return;
    if (r.actions.isTrayPoint && r.actions.isTrayPoint(screenPoint)) return;
    RECT window{};
    if (!GetWindowRect(r.window, &window)) return;
    const auto point = r.ClientPoint(static_cast<float>(screenPoint.x) - static_cast<float>(window.left),
                                    static_cast<float>(screenPoint.y) - static_cast<float>(window.top));
    const float radius = std::min(32.0f, static_cast<float>(r.height.Value()) * 0.14f);
    if (!island::InsideSquircle(point, r.Layout().shell, radius, true)) Hide(true);
    else r.ResetDismissal();
}

LRESULT PopupView::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto& r = *m_impl;
    switch (message) {
    case WM_INPUT: {
        RAWINPUT input{};
        UINT bytes = sizeof(input);
        const UINT read = GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &input, &bytes, sizeof(RAWINPUTHEADER));
        constexpr USHORT downs = RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_DOWN |
            RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_5_DOWN;
        if (read != static_cast<UINT>(-1) && read >= offsetof(RAWINPUT, data) + sizeof(RAWMOUSE) &&
            input.header.dwType == RIM_TYPEMOUSE && (input.data.mouse.usButtonFlags & downs) != 0) {
            POINT point{};
            if (GetCursorPos(&point)) NotifyPointerDown(point);
        }
        // Foreground WM_INPUT requires DefWindowProc cleanup, including when a
        // packet cannot be read. Background input must not consume the click.
        return GET_RAWINPUT_CODE_WPARAM(wParam) == RIM_INPUT ? DefWindowProcW(window, message, wParam, lParam) : 0;
    }
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: ValidateRect(window, nullptr); r.RequestFrame(); return 0;
    case WM_SIZE: r.RequestFrame(); return 0;
    case WM_NCHITTEST: {
        POINT pixel{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; ScreenToClient(window, &pixel);
        const auto point = r.Mouse(MAKELPARAM(pixel.x, pixel.y));
        const float radius = std::min(32.0f, static_cast<float>(r.height.Value()) * 0.14f);
        return island::InsideSquircle(point, r.Layout().shell, radius, true) ? HTCLIENT : HTTRANSPARENT;
    }
    case WM_DPICHANGED:
        r.dpi = HIWORD(wParam); r.MeasureMonitor(false); r.Targets(); r.Position(); r.lastFrame = {};
        if (r.renderer) {
            const HRESULT result = r.renderer->Resize(r.dpi, r.maximumHeight);
            if (FAILED(result)) r.GraphicsFailure(result);
        }
        r.RequestFrame(); return 0;
    case WM_DISPLAYCHANGE:
        r.MeasureMonitor(true); r.Targets(); r.Position(); r.lastFrame = {};
        if (r.renderer) {
            const HRESULT result = r.renderer->Resize(r.dpi, r.maximumHeight);
            if (FAILED(result)) r.GraphicsFailure(result);
        }
        return 0;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        r.RefreshMotionPreference(); if (r.renderer) r.renderer->RefreshTheme(); r.RequestFrame(); return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
            r.lastFrame = {}; r.RequestFrame();
        }
        return TRUE;
    case WM_TIMER:
        if (wParam == kBrightnessTimer) r.CommitBrightness();
        else if (wParam == kDismissTimer) {
            KillTimer(window, kDismissTimer); r.dismissTimer = false;
            const auto now = GetTickCount64();
            if (r.dismissal.Expired(now)) Hide(true);
            else if (const UINT remaining = r.dismissal.Remaining(now))
                r.dismissTimer = SetTimer(window, kDismissTimer, remaining, nullptr) != 0;
        }
        else if (wParam == kGraphicsTimer) {
            KillTimer(window, kGraphicsTimer);
            const UINT retries = r.graphicsRetries;
            const HRESULT result = r.InitializeRenderer();
            if (FAILED(result)) { r.graphicsRetries = retries; r.GraphicsFailure(result); }
            else r.RequestFrame();
        }
        return 0;
    case WM_MOUSEMOVE: {
        const auto point = r.Mouse(lParam);
        POINT screen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; ClientToScreen(window, &screen);
        if (!r.lastPointer || r.lastPointer->x != screen.x || r.lastPointer->y != screen.y) {
            r.lastPointer = screen; r.ResetDismissal();
        }
        if (r.dragging) r.DragTo(point.x, r.pressed.monitor);
        else r.Hover(r.Layout().Hit(point));
        if (!r.tracking) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0}; r.tracking = TrackMouseEvent(&track) != FALSE;
        }
        return 0;
    }
    case WM_MOUSELEAVE: r.wheelId.clear(); r.wheelRemainder = 0; r.tracking = false; r.Hover({}); return 0;
    case WM_LBUTTONDOWN: {
        r.wheelId.clear(); r.wheelRemainder = 0;
        r.ResetDismissal();
        r.Tooltip(false);
        const auto point = r.Mouse(lParam); const auto inputLayout = r.Layout(); const auto hit = inputLayout.Hit(point);
        if (hit.control == island::Control::None || (hit.control == island::Control::Slider && !r.Adjustable(hit.monitor))) return 0;
        r.keyboardMode = false; r.focus = hit; r.pressed = hit; r.feedback.Target(0.97);
        if (hit.control == island::Control::Slider && hit.monitor < r.state.monitors.size()) {
            const auto id = r.state.monitors[hit.monitor].id;
            r.SelectSliderTarget(hit.monitor);
            const auto current = std::ranges::find_if(r.state.monitors, [&id](const auto& monitor) { return monitor.id == id; });
            if (current == r.state.monitors.end()) { r.pressed = {}; return 0; }
            r.pressed.monitor = static_cast<size_t>(current - r.state.monitors.begin());
            r.dragging = true; r.dragId = id;
            SetCapture(window); r.DragTo(point.x, r.pressed.monitor);
        } else SetCapture(window);
        if (GetCapture() == window) r.StopDismissal();
        r.AccessibleName(); r.RequestFrame(); return 0;
    }
    case WM_LBUTTONUP: {
        const auto pressed = r.pressed; const bool dragging = r.dragging;
        const auto hit = r.Layout().Hit(r.Mouse(lParam));
        r.FinishDrag(); if (!dragging && pressed == hit) r.Activate(pressed);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lParam) != window) r.FinishDrag();
        return 0;
    case WM_CANCELMODE: r.FinishDrag(); return 0;
    case WM_MOUSEWHEEL: {
        r.ResetDismissal();
        if (r.dragging) return 0;
        // Wheel coordinates are screen pixels, unlike WM_MOUSEMOVE. Invert
        // the current presentation after converting them to client pixels.
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (!ScreenToClient(window, &point)) return 0;
        const auto hit = r.Layout().Hit(r.ClientPoint(static_cast<float>(point.x), static_cast<float>(point.y)));
        r.keyboardMode = false;
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (hit.control == island::Control::Slider) {
            if (!r.Adjustable(hit.monitor)) { r.wheelId.clear(); r.wheelRemainder = 0; return 0; }
            const auto& monitor = r.state.monitors[hit.monitor];
            if (r.wheelId != monitor.id) { r.wheelId = monitor.id; r.wheelRemainder = 0; }
            // Accumulate high-resolution wheel deltas per stable display ID,
            // so a partial step cannot spill into a different display.
            r.wheelRemainder += delta;
            const int steps = r.wheelRemainder / WHEEL_DELTA;
            r.wheelRemainder %= WHEEL_DELTA;
            const int increment = (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0 ? 5 : 1;
            r.SelectSliderTarget(hit.monitor);
            r.DisplayBrightness(hit.monitor, monitor.brightness + steps * increment, false);
            r.Hover(hit); r.AccessibleName(); r.RequestFrame(); return 0;
        }
        r.wheelId.clear(); r.wheelRemainder = 0;
        if (r.Layout().viewport.Contains(r.ClientPoint(static_cast<float>(point.x), static_cast<float>(point.y)))) {
            r.scroll = std::clamp(r.scroll - static_cast<float>(delta) / WHEEL_DELTA * island::kRowStride,
                                  0.0f, r.Layout().MaximumScroll());
            r.Hover({}); r.RequestFrame();
        }
        return 0;
    }
    case WM_KEYDOWN:
        r.wheelId.clear(); r.wheelRemainder = 0;
        r.ResetDismissal();
        r.keyboardMode = true;
        if (wParam == VK_ESCAPE) { Hide(true); return 0; }
        if (wParam == VK_TAB) { r.MoveFocus((GetKeyState(VK_SHIFT) & 0x8000) != 0); return 0; }
        if (wParam == VK_SPACE || wParam == VK_RETURN) { r.Activate(r.focus); return 0; }
        if (r.focus.control == island::Control::Slider) {
            if (!r.Adjustable(r.focus.monitor)) return 0;
            int value = r.state.monitors[r.focus.monitor].brightness;
            const int step = (GetKeyState(VK_SHIFT) & 0x8000) != 0 ? 5 : 1;
            if (wParam == VK_RIGHT || wParam == VK_UP) value += step;
            else if (wParam == VK_LEFT || wParam == VK_DOWN) value -= step;
            else if (wParam == VK_PRIOR) value += 10;
            else if (wParam == VK_NEXT) value -= 10;
            else if (wParam == VK_HOME) value = kMinBrightness;
            else if (wParam == VK_END) value = kMaxBrightness;
            else return 0;
            r.SelectSliderTarget(r.focus.monitor); r.DisplayBrightness(r.focus.monitor, value, false);
        } else if (r.focus.control == island::Control::Software || r.focus.control == island::Control::Hardware) {
            if (wParam == VK_LEFT) r.Activate({island::Control::Software});
            else if (wParam == VK_RIGHT) r.Activate({island::Control::Hardware});
        } else if (wParam == VK_UP || wParam == VK_DOWN) r.MoveFocus(wParam == VK_UP);
        return 0;
    case WM_HOTKEY:
        if (IsVisible() && !r.hiding) {
            const auto found = std::ranges::find_if(kShortcuts, [wParam](const auto& shortcut) {
                return static_cast<WPARAM>(shortcut.id) == wParam;
            });
            if (found != kShortcuts.end()) {
                if (found->command == VK_TAB) {
                    r.ResetDismissal();
                    r.keyboardMode = true; r.MoveFocus((found->modifiers & MOD_SHIFT) != 0); return 0;
                }
                return HandleMessage(window, WM_KEYDOWN, found->command, 0);
            }
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && r.keyboardMode && IsVisible() && !r.contextMenu) Hide(true);
        return 0;
    case WM_CONTEXTMENU:
        if (r.actions.showContextMenu) {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (point.x == -1 && point.y == -1) GetCursorPos(&point);
            r.contextMenu = true; r.StopDismissal();
            r.actions.showContextMenu(point);
            r.contextMenu = false; r.ResetDismissal();
        }
        return 0;
    case WM_NCDESTROY:
        r.StopDismissal(); r.StopMouseInput();
        r.UnregisterShortcuts();
        r.clock.Stop(); r.renderer.reset(); r.window = nullptr; r.tooltip = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0); break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
