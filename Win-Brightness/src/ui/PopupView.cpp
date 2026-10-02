#include "PopupView.h"
#include "island/Spring.h"
#include "island/FrameClock.h"
#include "../platform/Win32Helpers.h"
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <commctrl.h>
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")

namespace {
constexpr UINT_PTR kBrightnessTimer = 1;
constexpr UINT_PTR kGraphicsTimer = 2;
constexpr UINT kBrightnessInterval = 70;
constexpr wchar_t kWindowClass[] = L"TrenchesDynamicIsland";
}

struct PopupView::Impl {
    HINSTANCE instance;
    HWND window = nullptr, tooltip = nullptr, previousForeground = nullptr;
    PopupActions actions;
    PopupState state;
    island::Preferences preferences;
    std::unique_ptr<island::Renderer> renderer;
    island::FrameClock clock;
    island::Spring width{island::kCompactWidth}, height{island::kCompactHeight};
    island::Spring expansion{0}, visibility{1, island::kFeedbackSpring};
    island::Spring slider{72, island::kSliderSpring}, mode{0, island::kFeedbackSpring};
    island::Spring feedback{1, island::kFeedbackSpring}, number{0, island::kFeedbackSpring};
    island::Target focus{island::Control::Slider, 0}, hover{}, pressed{};
    bool expanded = false, dirty = true, dragging = false, tracking = false;
    bool pendingBrightness = false, brightnessTimer = false, hiding = false, keyboardMode = false;
    bool reducedMotion = false, repositioning = false, finishingDrag = false;
    std::wstring dragId, tooltipText;
    float scroll = 0, maximumHeight = 449, maximumWidth = 420;
    UINT dpi = 96, graphicsRetries = 0;
    HRESULT renderError = S_OK;
    LARGE_INTEGER frequency{}, lastFrame{};

    Impl(HINSTANCE module, PopupActions callbacks) : instance(module), actions(std::move(callbacks)) {
        QueryPerformanceFrequency(&frequency);
        RefreshMotionPreference();
    }
    ~Impl() {
        if (window) {
            KillTimer(window, kBrightnessTimer); KillTimer(window, kGraphicsTimer);
            if (GetCapture() == window) ReleaseCapture();
            clock.Stop(); renderer.reset(); DestroyWindow(window);
        }
    }
    bool Animate() const { return preferences.animations && !reducedMotion; }
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
        return width.Active(0.05) || height.Active(0.05) || expansion.Active() || visibility.Active() ||
            slider.Active(0.05) || mode.Active() || feedback.Active() || number.Active();
    }
    island::Layout Layout(bool isExpanded) const {
        return island::Layout::Build(static_cast<float>(width.Value()), static_cast<float>(height.Value()),
            isExpanded, state.monitors.size(), Primary(), scroll);
    }
    void Targets() {
        width.Target(std::min(expanded ? island::kExpandedWidth : island::kCompactWidth, maximumWidth));
        height.Target(expanded ? island::ExpandedHeight(state.monitors.size(), maximumHeight) : island::kCompactHeight);
        expansion.Target(expanded ? 1.0 : 0.0);
        if (!Animate()) {
            width.Snap(width.Target()); height.Snap(height.Target()); expansion.Snap(expansion.Target());
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
        maximumWidth = std::max(200.0f, std::min(island::kExpandedWidth,
            static_cast<float>(info.rcMonitor.right - info.rcMonitor.left) / Scale() - island::kShadowMargin * 2));
        maximumHeight = std::max(island::kCompactHeight, std::min(island::ExpandedHeight(5, 10000),
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
    island::Point Mouse(LPARAM param) const {
        RECT client{}; GetClientRect(window, &client);
        const float inset = (static_cast<float>(client.right) / Scale() - static_cast<float>(width.Value())) * 0.5f;
        return {GET_X_LPARAM(param) / Scale() - inset, GET_Y_LPARAM(param) / Scale()};
    }
    island::Rect SliderRect(size_t index) const {
        const auto layout = Layout(expanded);
        const auto row = std::ranges::find_if(layout.rows, [index](const auto& item) { return item.index == index; });
        return row == layout.rows.end() ? island::Rect{} : row->slider;
    }
    int BrightnessAt(float x, size_t index) const {
        auto rect = SliderRect(index);
        if (expanded) { rect.left += 10; rect.right -= 10; }
        if (rect.Width() <= 0) return state.brightness;
        const float ratio = std::clamp((x - rect.left) / rect.Width(), 0.0f, 1.0f);
        return ClampBrightness(static_cast<int>(std::lround(ratio * 99.0f + 1.0f)));
    }
    void AccessibleName() {
        std::wstring name = L"trenches. ";
        switch (focus.control) {
        case island::Control::Slider:
            if (focus.monitor < state.monitors.size()) name += state.monitors[focus.monitor].name + L". ";
            name += L"Linked brightness " + std::to_wstring(state.brightness) + L" percent.";
            break;
        case island::Control::Monitor:
            if (focus.monitor < state.monitors.size()) name += state.monitors[focus.monitor].name +
                (state.selection.Contains(state.monitors[focus.monitor].id) ? L". Selected." : L". Not selected.");
            break;
        case island::Control::Software: name += L"Software dimming."; break;
        case island::Control::Hardware: name += L"Hardware DDC CI."; break;
        case island::Control::Power: name += state.enabled ? L"Disable dimming." : L"Enable dimming."; break;
        case island::Control::Scope: name += state.selection.all ? L"All displays." : L"Selected group."; break;
        case island::Control::Expand: name += expanded ? L"Collapse." : L"Expand."; break;
        case island::Control::None: break;
        }
        if (FAILED(renderError)) name += L" Graphics unavailable.";
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
            tooltipText = state.monitors[control.monitor].name + L"\nSelected displays share one brightness value.";
            const DWORD error = state.monitors[control.monitor].hardwareError;
            if (error) tooltipText += L"\nDDC/CI error " + std::to_wstring(error) + L".";
            Tooltip(true);
        } else if (control.control == island::Control::Scope) {
            tooltipText = state.selection.all ? L"Switch to selected displays" : L"Select all displays";
            Tooltip(true);
        }
        RequestFrame();
    }
    void QueueBrightness() {
        pendingBrightness = true;
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
        if (!pendingBrightness) return;
        pendingBrightness = false;
        if (actions.setBrightness) actions.setBrightness(state.brightness);
    }
    void DisplayBrightness(int value, bool direct) {
        value = ClampBrightness(value);
        if (state.brightness == value) return;
        state.brightness = value;
        if (direct || !Animate()) slider.Snap(value); else slider.Target(value);
        number.Target(1.0); QueueBrightness(); AccessibleName(); RequestFrame();
    }
    void SelectSliderTarget(size_t index) {
        if (index >= state.monitors.size()) return;
        const auto id = state.monitors[index].id;
        const bool primaryOnly = !expanded && (state.selection.all || state.selection.ids.size() != 1 ||
                                               !state.selection.Contains(id));
        if (primaryOnly || !state.selection.Contains(id)) {
            CommitBrightness(); state.selection = {false, {id}};
            if (actions.setSelection) actions.setSelection(state.selection);
        }
    }
    void FinishDrag(bool commit = true) {
        if (finishingDrag) return;
        finishingDrag = true; dragging = false; pressed = {}; dragId.clear();
        feedback.Target(hover.control == island::Control::None ? 1.0 : 1.015);
        if (GetCapture() == window) ReleaseCapture();
        if (commit) CommitBrightness();
        else { pendingBrightness = false; brightnessTimer = false; KillTimer(window, kBrightnessTimer); }
        finishingDrag = false; RequestFrame();
    }
    void EnsureFocusVisible() {
        if (!expanded || (focus.control != island::Control::Slider && focus.control != island::Control::Monitor)) return;
        auto layout = Layout(true);
        if (focus.monitor >= layout.rows.size()) return;
        const auto& row = layout.rows[focus.monitor];
        if (row.label.top < layout.viewport.top) scroll -= layout.viewport.top - row.label.top;
        if (row.slider.bottom > layout.viewport.bottom) scroll += row.slider.bottom - layout.viewport.bottom;
        scroll = std::clamp(scroll, 0.0f, layout.MaximumScroll());
    }
    std::vector<island::Target> FocusOrder() const {
        std::vector<island::Target> order;
        const auto layout = Layout(expanded);
        for (const auto& row : layout.rows) {
            if (expanded) order.push_back({island::Control::Monitor, row.index});
            order.push_back({island::Control::Slider, row.index});
        }
        if (expanded) {
            order.push_back({island::Control::Software}); order.push_back({island::Control::Hardware});
            order.push_back({island::Control::Scope});
        }
        order.push_back({island::Control::Power}); order.push_back({island::Control::Expand});
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
    void Expand(bool value) {
        FinishDrag(); expanded = value; scroll = 0;
        focus = {island::Control::Slider, Primary()}; hover = {}; Tooltip(false);
        Targets(); if (!Animate()) Position(); AccessibleName();
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
        case island::Control::Scope:
            state.selection.all = !state.selection.all;
            if (!state.selection.all && state.selection.ids.empty() && !state.monitors.empty())
                state.selection.ids = {state.monitors[Primary()].id};
            if (actions.setSelection) actions.setSelection(state.selection);
            break;
        case island::Control::Monitor:
            if (control.monitor < state.monitors.size()) {
                const auto id = state.monitors[control.monitor].id;
                if (state.selection.all) state.selection = {false, {id}};
                else {
                    const auto found = std::ranges::find(state.selection.ids, id);
                    if (found == state.selection.ids.end()) state.selection.ids.push_back(id);
                    else if (state.selection.ids.size() > 1) state.selection.ids.erase(found);
                }
                if (actions.setSelection) actions.setSelection(state.selection);
            }
            break;
        case island::Control::Expand: Expand(!expanded); break;
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
bool PopupView::IsExpanded() const { return m_impl->expanded; }
island::Layout PopupView::GetLayout() const { return m_impl->Layout(m_impl->expanded); }
HRESULT PopupView::LastRenderError() const { return m_impl->renderError; }
island::Preferences PopupView::GetPreferences() const { return m_impl->preferences; }
void PopupView::SetExpanded(bool expanded) { m_impl->Expand(expanded); }

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
    r.MeasureMonitor(true); r.Targets(); r.Position();
    if (!r.renderer) {
        const HRESULT result = r.InitializeRenderer();
        if (FAILED(result)) { r.GraphicsFailure(result); return; }
    } else {
        const HRESULT result = r.renderer->Resize(r.dpi, r.maximumHeight);
        if (FAILED(result)) { r.GraphicsFailure(result); return; }
    }
    if (!IsVisible()) r.visibility.Snap(r.Animate() ? 0.0 : 1.0);
    r.hiding = false; r.visibility.Target(1.0); r.lastFrame = {};
    r.keyboardMode = keyboardInvoked; r.focus = {island::Control::Slider, r.Primary()};
    ShowWindow(r.window, SW_SHOWNOACTIVATE);
    if (keyboardInvoked) {
        // Only explicit keyboard control requests activation. Mouse/tray opening
        // and every resize preserve the foreground application.
        r.previousForeground = GetForegroundWindow(); SetForegroundWindow(r.window); SetFocus(r.window);
    }
    r.AccessibleName(); r.RequestFrame();
}
void PopupView::Hide(bool animated) {
    auto& r = *m_impl;
    if (!r.window) return;
    r.FinishDrag(); r.Tooltip(false);
    if (animated && r.Animate() && IsVisible() && r.renderer) {
        r.hiding = true; r.visibility.Target(0.0); r.Expand(false);
    } else {
        r.hiding = false; r.visibility.Snap(0); r.expanded = false;
        r.width.Snap(std::min(island::kCompactWidth, r.maximumWidth));
        r.height.Snap(island::kCompactHeight); r.expansion.Snap(0);
        const bool restore = GetForegroundWindow() == r.window && IsWindow(r.previousForeground);
        r.keyboardMode = false;
        ShowWindow(r.window, SW_HIDE); r.dirty = false; r.lastFrame = {};
        KillTimer(r.window, kGraphicsTimer);
        if (restore) SetForegroundWindow(r.previousForeground);
    }
}
void PopupView::SetState(PopupState state) {
    auto& r = *m_impl;
    const int incomingBrightness = ClampBrightness(state.brightness);
    state.brightness = r.pendingBrightness ? r.state.brightness : ClampBrightness(state.brightness);
    const std::wstring draggingId = r.dragId;
    r.state = std::move(state);
    if (r.dragging) {
        const auto found = std::ranges::find_if(r.state.monitors, [&draggingId](const auto& monitor) { return monitor.id == draggingId; });
        if (found == r.state.monitors.end() || (!r.expanded && r.state.monitors[r.Primary()].id != draggingId)) {
            r.FinishDrag(false); r.state.brightness = incomingBrightness; r.slider.Snap(incomingBrightness);
        }
        else r.pressed.monitor = static_cast<size_t>(found - r.state.monitors.begin());
    const bool monitorFocus = r.focus.control == island::Control::Slider || r.focus.control == island::Control::Monitor;
    const std::wstring focusedId = monitorFocus && r.focus.monitor < r.state.monitors.size()
        ? r.state.monitors[r.focus.monitor].id : L"";
    }
    if (r.focus.monitor >= r.state.monitors.size()) r.focus = {island::Control::Expand};
    if (!r.dragging && !r.pendingBrightness) r.slider.Target(r.state.brightness);
    r.mode.Target(r.state.mode == BrightnessMode::Hardware ? 1.0 : 0.0);
    if (!r.window) return;
    r.MeasureMonitor(true); r.Targets();
    r.scroll = std::clamp(r.scroll, 0.0f, r.Layout(true).MaximumScroll());
    r.Tooltip(false); r.AccessibleName(); if (IsVisible()) r.Position();
}
void PopupView::SetPreferences(island::Preferences preferences) {
    if (!focusedId.empty()) {
        const auto found = std::ranges::find_if(r.state.monitors, [&focusedId](const auto& monitor) { return monitor.id == focusedId; });
        r.focus = found == r.state.monitors.end() ? island::Target{island::Control::Expand}
            : island::Target{r.focus.control, static_cast<size_t>(found - r.state.monitors.begin())};
    }
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
    advance(r.width, 0.05); advance(r.height, 0.05); advance(r.expansion); advance(r.visibility);
    advance(r.slider, 0.05); advance(r.mode); advance(r.feedback); advance(r.number);
    if (r.number.Target() > 0.5 && r.number.Value() > 0.9) r.number.Target(0.0);
    r.Position();
    island::Frame frame;
    frame.state = &r.state; frame.compact = r.Layout(false); frame.expanded = r.Layout(true);
    frame.expansion = static_cast<float>(std::clamp(r.expansion.Value(), 0.0, 1.0));
    frame.opacity = static_cast<float>(std::clamp(r.visibility.Value(), 0.0, 1.0));
    frame.radius = std::lerp(static_cast<float>(r.height.Value()) * 0.5f,
                            static_cast<float>(r.height.Value()) * 0.14f, frame.expansion);
    frame.sliderValue = static_cast<float>(r.slider.Value()); frame.modePosition = static_cast<float>(r.mode.Value());
    frame.numberFeedback = static_cast<float>(r.number.Value());
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

LRESULT PopupView::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto& r = *m_impl;
    switch (message) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: ValidateRect(window, nullptr); r.RequestFrame(); return 0;
    case WM_SIZE: r.RequestFrame(); return 0;
    case WM_NCHITTEST: {
        POINT pixel{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; ScreenToClient(window, &pixel);
        const auto point = r.Mouse(MAKELPARAM(pixel.x, pixel.y));
        const float e = static_cast<float>(r.expansion.Value());
        const float radius = static_cast<float>(r.height.Value()) * std::lerp(0.5f, 0.14f, e);
        return island::InsideSquircle(point, r.Layout(false).shell, radius) ? HTCLIENT : HTTRANSPARENT;
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
        if (r.dragging) r.DisplayBrightness(r.BrightnessAt(point.x, r.pressed.monitor), true);
        else r.Hover(r.Layout(r.expanded).Hit(point));
        if (!r.tracking) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0}; r.tracking = TrackMouseEvent(&track) != FALSE;
        }
        return 0;
    }
    case WM_MOUSELEAVE: r.tracking = false; r.Hover({}); return 0;
    case WM_LBUTTONDOWN: {
        r.Tooltip(false);
        const auto point = r.Mouse(lParam); const auto hit = r.Layout(r.expanded).Hit(point);
        if (hit.control == island::Control::None) return 0;
        r.focus = hit; r.pressed = hit; r.feedback.Target(0.97);
        if (hit.control == island::Control::Slider && hit.monitor < r.state.monitors.size()) {
            const auto id = r.state.monitors[hit.monitor].id;
            r.SelectSliderTarget(hit.monitor);
            const auto current = std::ranges::find_if(r.state.monitors, [&id](const auto& monitor) { return monitor.id == id; });
            if (current == r.state.monitors.end()) { r.pressed = {}; return 0; }
            r.pressed.monitor = static_cast<size_t>(current - r.state.monitors.begin());
            r.dragging = true; r.dragId = id;
            SetCapture(window); r.DisplayBrightness(r.BrightnessAt(point.x, r.pressed.monitor), true);
        } else SetCapture(window);
        r.AccessibleName(); r.RequestFrame(); return 0;
    }
    case WM_LBUTTONUP: {
        const auto pressed = r.pressed; const bool dragging = r.dragging;
        const auto hit = r.Layout(r.expanded).Hit(r.Mouse(lParam));
        r.FinishDrag(); if (!dragging && pressed == hit) r.Activate(pressed);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lParam) != window) r.FinishDrag();
        return 0;
    case WM_CANCELMODE: r.FinishDrag(); return 0;
    case WM_MOUSEWHEEL:
        if (r.expanded) {
            r.scroll = std::clamp(r.scroll - static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA * island::kRowStride,
                                  0.0f, r.Layout(true).MaximumScroll());
            r.Hover({}); r.RequestFrame();
        } else if (!r.state.monitors.empty()) {
            r.SelectSliderTarget(r.Primary());
            r.DisplayBrightness(r.state.brightness + GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 5, false);
        }
        return 0;
    case WM_KEYDOWN:
        r.keyboardMode = true;
        if (wParam == VK_ESCAPE) { if (r.expanded) r.Expand(false); else Hide(true); return 0; }
        if (wParam == VK_TAB) { r.MoveFocus((GetKeyState(VK_SHIFT) & 0x8000) != 0); return 0; }
        if (wParam == 'E') { r.Expand(!r.expanded); return 0; }
        if (wParam == VK_SPACE || wParam == VK_RETURN) { r.Activate(r.focus); return 0; }
        if (r.focus.control == island::Control::Slider) {
            int value = r.state.brightness;
            const int step = (GetKeyState(VK_SHIFT) & 0x8000) != 0 ? 5 : 1;
            if (wParam == VK_RIGHT || wParam == VK_UP) value += step;
            else if (wParam == VK_LEFT || wParam == VK_DOWN) value -= step;
            else if (wParam == VK_PRIOR) value += 10;
            else if (wParam == VK_NEXT) value -= 10;
            else if (wParam == VK_HOME) value = kMinBrightness;
            else if (wParam == VK_END) value = kMaxBrightness;
            else return 0;
            r.SelectSliderTarget(r.focus.monitor); r.DisplayBrightness(value, false);
        } else if (r.focus.control == island::Control::Software || r.focus.control == island::Control::Hardware) {
            if (wParam == VK_LEFT) r.Activate({island::Control::Software});
            else if (wParam == VK_RIGHT) r.Activate({island::Control::Hardware});
        } else if (wParam == VK_UP || wParam == VK_DOWN) r.MoveFocus(wParam == VK_UP);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && r.keyboardMode && IsVisible()) Hide(false);
        return 0;
    case WM_CONTEXTMENU:
        if (r.actions.showContextMenu) {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (point.x == -1 && point.y == -1) GetCursorPos(&point);
            r.actions.showContextMenu(point);
        }
        return 0;
    case WM_NCDESTROY:
        r.clock.Stop(); r.renderer.reset(); r.window = nullptr; r.tooltip = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0); break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
