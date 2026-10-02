#include "ui/PopupView.h"
#include <cstdio>
#include <stdexcept>

void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
LPARAM At(island::Rect rect, float fraction = 0.5f, float scale = 1.0f) {
    return MAKELPARAM(static_cast<int>((rect.left + rect.Width() * fraction + island::kShadowMargin) * scale),
                     static_cast<int>((rect.top + rect.Height() * 0.5f) * scale));
}
void Click(PopupView& popup, island::Rect rect) {
    popup.HandleMessage(popup.GetHWnd(), WM_LBUTTONDOWN, MK_LBUTTON, At(rect));
    popup.HandleMessage(popup.GetHWnd(), WM_LBUTTONUP, 0, At(rect));
}
void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
}
void Settle(PopupView& popup) {
    static int stage = 0;
    ++stage;
    for (int i = 0; i < 300; ++i) {
        Pump();
        HANDLE frame = popup.FrameWaitHandle();
        if (!frame) return;
        Check(WaitForSingleObject(frame, 2000) == WAIT_OBJECT_0, "wait for frame");
        popup.RenderFrame();
        Check(SUCCEEDED(popup.LastRenderError()), "render popup");
    }
    std::fprintf(stderr, "settle stage %d, width %.2f height %.2f\n", stage,
        popup.GetLayout().shell.Width(), popup.GetLayout().shell.Height());
    throw std::runtime_error("animation failed to settle");
}
int main() try {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int committed = -1, commits = 0, committedBeforeMode = -1, selectionChanges = 0;
    MonitorSelection lastSelection;
    PopupActions actions;
    actions.setBrightness = [&](int value) { committed = value; ++commits; };
    actions.setMode = [&](BrightnessMode) { committedBeforeMode = committed; };
    actions.setSelection = [&](MonitorSelection selection) { ++selectionChanges; lastSelection = std::move(selection); };
    PopupView popup(GetModuleHandleW(nullptr), std::move(actions));
    Check(popup.Register() && popup.Create(), "create popup");
    popup.SetPreferences({island::Theme::Dark, false, false});
    PopupState saved;
    saved.brightness = 72;
    for (size_t i = 0; i < 7; ++i) {
        MonitorInfo monitor;
        monitor.id = std::to_wstring(i); monitor.name = L"Monitor " + std::to_wstring(i + 1); monitor.primary = i == 0;
        saved.monitors.push_back(std::move(monitor));
    }
    popup.SetState(saved);
    const HWND foreground = GetForegroundWindow();
    popup.Toggle({0, 0}, false);
    Check(popup.IsVisible() && GetForegroundWindow() == foreground, "mouse opening does not activate");
    RECT actual{}; MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
    GetWindowRect(popup.GetHWnd(), &actual);
    Check(actual.top == monitor.rcMonitor.top, "island attached to screen top");
    Check((GetWindowLongPtrW(popup.GetHWnd(), GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0, "NOACTIVATE style");
    Check(selectionChanges == 0, "showing does not change monitor selection");
    Settle(popup);
    Check(popup.FrameWaitHandle() == nullptr, "idle has no frame wakeups");
    popup.SetExpanded(true);
    const auto fiveRows = popup.GetLayout();
    Check(fiveRows.MaximumScroll() == 2 * island::kRowStride, "five visible monitors and overflow scroll");
    Check(selectionChanges == 0, "morphing does not change selection");
    popup.HandleMessage(popup.GetHWnd(), WM_KEYDOWN, VK_RIGHT, 0);
    popup.SetState(saved);
    popup.HandleMessage(popup.GetHWnd(), WM_TIMER, 1, 0);
    Check(committed == 73, "pending value survives catalog synchronization");

    popup.SetState(saved); committed = -1;
    popup.HandleMessage(popup.GetHWnd(), WM_KEYDOWN, VK_RIGHT, 0);
    Click(popup, popup.GetLayout().hardware);
    Check(committedBeforeMode == 73, "brightness committed before mode change");
    popup.SetState(saved);
    auto slider = popup.GetLayout().rows[0].slider;
    popup.HandleMessage(popup.GetHWnd(), WM_LBUTTONDOWN, MK_LBUTTON, At(slider, 0.4f));
    Check(GetCapture() == popup.GetHWnd(), "capture slider drag");
    ReleaseCapture();
    const int afterCapture = committed;
    popup.HandleMessage(popup.GetHWnd(), WM_MOUSEMOVE, 0, At(slider, 0.9f));
    popup.HandleMessage(popup.GetHWnd(), WM_TIMER, 1, 0);
    Check(committed == afterCapture && afterCapture != 72, "capture loss commits and ends drag");

    popup.HandleMessage(popup.GetHWnd(), WM_LBUTTONDOWN, MK_LBUTTON, At(slider, 0.3f));
    popup.HandleMessage(popup.GetHWnd(), WM_CANCELMODE, 0, 0);
    Check(GetCapture() != popup.GetHWnd(), "cancel releases capture");
    const int afterCancel = committed;
    popup.HandleMessage(popup.GetHWnd(), WM_MOUSEMOVE, 0, At(slider, 0.95f));
    popup.HandleMessage(popup.GetHWnd(), WM_TIMER, 1, 0);
    Check(committed == afterCancel, "cancel stops brightness changes");

    popup.SetState(saved); commits = 0;
    popup.HandleMessage(popup.GetHWnd(), WM_LBUTTONDOWN, MK_LBUTTON, At(slider, 0.1f));
    for (int i = 0; i < 100; ++i)
        popup.HandleMessage(popup.GetHWnd(), WM_MOUSEMOVE, MK_LBUTTON, At(slider, static_cast<float>(i) / 100.0f));
    Check(commits == 0, "drag does not synchronously dispatch each pointer event");
    popup.HandleMessage(popup.GetHWnd(), WM_TIMER, 1, 0);
    Check(commits == 1 && committed >= 98, "throttle dispatches latest value during drag");
    popup.HandleMessage(popup.GetHWnd(), WM_MOUSEMOVE, MK_LBUTTON, At(slider, 0.8f));
    popup.HandleMessage(popup.GetHWnd(), WM_LBUTTONUP, 0, At(slider, 0.8f));
    Check(commits == 2 && committed < 90, "release flushes final target");

    popup.SetState(saved); commits = 0;
    popup.HandleMessage(popup.GetHWnd(), WM_LBUTTONDOWN, MK_LBUTTON, At(slider, 0.2f));
    auto disconnected = saved; disconnected.monitors.erase(disconnected.monitors.begin());
    popup.SetState(disconnected);
    Check(GetCapture() != popup.GetHWnd(), "hot unplug cancels drag");
    popup.HandleMessage(popup.GetHWnd(), WM_TIMER, 1, 0);
    Check(commits == 0, "hot unplug does not commit to a different row");
    popup.SetState(saved);

    Click(popup, popup.GetLayout().rows[1].label);
    auto reordered = saved;
    std::swap(reordered.monitors[1], reordered.monitors[2]);
    popup.SetState(reordered);
    popup.HandleMessage(popup.GetHWnd(), WM_KEYDOWN, VK_SPACE, 0);
    Check(!lastSelection.all && lastSelection.ids == std::vector<std::wstring>{L"1"},
          "keyboard focus follows monitor ID after catalog reorder");
    popup.SetState(saved);

    RECT suggested{100, 150, 900, 750};
    popup.HandleMessage(popup.GetHWnd(), WM_DPICHANGED, MAKELONG(144, 144), reinterpret_cast<LPARAM>(&suggested));
    GetWindowRect(popup.GetHWnd(), &actual);
    Check(actual.top == monitor.rcMonitor.top && actual.right - actual.left == 678, "DPI preserves top anchor and DIP width");
    popup.HandleMessage(popup.GetHWnd(), WM_DPICHANGED, MAKELONG(96, 96), reinterpret_cast<LPARAM>(&suggested));
    popup.SetPreferences({island::Theme::Light, false, true});
    popup.SetExpanded(false); Settle(popup); popup.SetExpanded(true);
    const int beforeHiddenScope = selectionChanges;
    Click(popup, popup.GetLayout().scope);
    Check(selectionChanges == beforeHiddenScope, "invisible scope button cannot activate during morph");
    for (int i = 0; i < 3; ++i) {
        HANDLE frame = popup.FrameWaitHandle();
        Check(frame && WaitForSingleObject(frame, 2000) == WAIT_OBJECT_0, "morph frame"); popup.RenderFrame();
    }
    const float beforeReverse = popup.GetLayout().shell.Width();
    popup.SetExpanded(false);
    Check(popup.GetLayout().shell.Width() == beforeReverse, "morph reversal starts at current presentation");
    Settle(popup);
    popup.Hide(true); popup.Toggle({0, 0}); Settle(popup);
    Check(popup.IsVisible(), "closing animation can be interrupted");
    popup.Hide();
    Check(!popup.IsVisible() && popup.FrameWaitHandle() == nullptr, "hidden popup has no frames");
    const HWND keyboardForeground = GetForegroundWindow();
    popup.Toggle({0, 0}, true); Settle(popup);
    Check(GetForegroundWindow() == keyboardForeground, "hotkey opening also preserves foreground focus");
    popup.HandleMessage(popup.GetHWnd(), WM_HOTKEY, 22, MAKELPARAM(MOD_CONTROL | MOD_ALT, VK_RIGHT));
    popup.HandleMessage(popup.GetHWnd(), WM_TIMER, 1, 0);
    Check(committed == 73, "temporary shortcut adjusts brightness without focus");
    popup.Hide();
    Check(RegisterHotKey(popup.GetHWnd(), 500, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_RIGHT), "closing releases temporary shortcut");
    UnregisterHotKey(popup.GetHWnd(), 500);
    std::puts("PopupTests passed");
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1;
}
