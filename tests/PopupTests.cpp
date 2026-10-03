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
bool OwnsRawMouse(HWND window) {
    UINT count = 0;
    Check(GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE)) != static_cast<UINT>(-1), "query raw registration count");
    std::vector<RAWINPUTDEVICE> devices(count);
    if (count) Check(GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE)) != static_cast<UINT>(-1), "query raw registrations");
    return std::ranges::any_of(devices, [window](const auto& device) {
        return device.usUsagePage == 1 && device.usUsage == 2 && device.hwndTarget == window;
    });
}
ULONGLONG CpuTicks() {
    FILETIME created{}, exited{}, kernel{}, user{};
    Check(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE, "read CPU time");
    const auto ticks = [](FILETIME time) {
        return (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    };
    return ticks(kernel) + ticks(user);
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
    int committed = -1, commits = 0, committedBeforeMode = -1;
    std::wstring committedId;
    bool testOutsideClicks = false;
    HWND testWindow = nullptr;
    PopupActions actions;
    actions.setMonitorBrightness = [&](const std::wstring& id, int value) { committedId = id; committed = value; ++commits; };
    actions.setMode = [&](BrightnessMode) { committedBeforeMode = committed; };
    actions.isTrayPoint = [&](POINT point) {
        if (point.x == 1234 && point.y == 9876) return true;
        RECT bounds{};
        // Keep unrelated desktop input out of synthetic UI tests. The dedicated
        // dismissal checks below enable the production outside-click path.
        return !testOutsideClicks && GetWindowRect(testWindow, &bounds) && !PtInRect(&bounds, point);
    };
    PopupView popup(GetModuleHandleW(nullptr), std::move(actions));
    Check(popup.Register() && popup.Create(), "create popup");
    testWindow = popup.GetHWnd();
    popup.SetPreferences({island::Theme::Dark, false, false});
    PopupState saved;
    for (size_t i = 0; i < 7; ++i) {
        MonitorInfo monitor;
        monitor.id = std::to_wstring(i); monitor.name = L"Monitor " + std::to_wstring(i + 1); monitor.primary = i == 0;
        saved.monitors.push_back(std::move(monitor));
    }
    popup.SetState(saved);
    const HWND foreground = GetForegroundWindow();
    popup.Toggle({0, 0}, false);
    Check(popup.IsVisible() && GetForegroundWindow() == foreground, "mouse opening does not activate");
    Check(OwnsRawMouse(popup.GetHWnd()), "visible popup observes outside clicks without focus");
    RECT actual{}; MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
    GetWindowRect(popup.GetHWnd(), &actual);
    Check(actual.top == monitor.rcMonitor.top, "island attached to screen top");
    Check((GetWindowLongPtrW(popup.GetHWnd(), GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0, "NOACTIVATE style");
    Settle(popup);
    Check(popup.FrameWaitHandle() == nullptr, "idle has no frame wakeups");
    for (int sample = 0; sample < 3; ++sample) {
        GetWindowRect(popup.GetHWnd(), &actual);
        popup.NotifyPointerDown({actual.left + 100, actual.top + 25});
        Settle(popup);
        const ULONGLONG idleStart = CpuTicks();
        Sleep(1000);
        Pump();
        if (popup.FrameWaitHandle()) {
            std::fprintf(stderr, "Unexpected idle frame: visible=%d y=%.2f raw=%d\n", popup.IsVisible(),
                popup.GetPresentation().offsetY, OwnsRawMouse(popup.GetHWnd()));
        }
        Check(popup.FrameWaitHandle() == nullptr, "idle remains asleep without periodic frames");
        std::printf("Idle CPU, sample %d over 1 second: %.3f ms\n", sample + 1,
                    static_cast<double>(CpuTicks() - idleStart) / 10000.0);
    }
    const auto fiveRows = popup.GetLayout();
    Check(fiveRows.MaximumScroll() == 2 * island::kRowStride, "five visible monitors and overflow scroll");
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
    popup.HandleMessage(popup.GetHWnd(), WM_TIMER, 3, 0);
    Check(popup.IsVisible() && GetCapture() == popup.GetHWnd(), "idle dismissal is suspended during a drag");
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
    popup.HandleMessage(popup.GetHWnd(), WM_KEYDOWN, VK_RIGHT, 0);
    popup.HandleMessage(popup.GetHWnd(), WM_TIMER, 1, 0);
    Check(committedId == L"1", "keyboard focus follows monitor ID after catalog reorder");
    popup.SetState(saved);

    Click(popup, popup.GetLayout().rows[0].label);
    commits = 0;
    popup.HandleMessage(popup.GetHWnd(), WM_KEYDOWN, VK_RIGHT, 0);
    popup.HandleMessage(popup.GetHWnd(), WM_KEYDOWN, VK_TAB, 0);
    popup.HandleMessage(popup.GetHWnd(), WM_KEYDOWN, VK_RIGHT, 0);
    popup.SetState(saved);
    popup.HandleMessage(popup.GetHWnd(), WM_TIMER, 1, 0);
    Check(commits == 2 && committedId == L"1" && committed == 73,
          "two independent pending targets survive synchronization");
    auto hardware = saved; hardware.mode = BrightnessMode::Hardware;
    hardware.monitors[0].hardwareBrightness = true; hardware.monitors[0].hardwareStatus = HardwareStatus::Available;
    hardware.monitors[1].hardwareStatus = HardwareStatus::Unsupported;
    popup.SetState(hardware); commits = 0;
    Click(popup, popup.GetLayout().rows[1].slider);
    popup.HandleMessage(popup.GetHWnd(), WM_TIMER, 1, 0);
    Check(commits == 0 && GetCapture() != popup.GetHWnd(), "unavailable hardware row cannot adjust or capture");
    Click(popup, popup.GetLayout().rows[0].slider);
    Check(commits == 1 && committedId == L"0", "available hardware row remains independently adjustable");
    popup.SetState(saved);

    RECT suggested{100, 150, 900, 750};
    popup.HandleMessage(popup.GetHWnd(), WM_DPICHANGED, MAKELONG(144, 144), reinterpret_cast<LPARAM>(&suggested));
    GetWindowRect(popup.GetHWnd(), &actual);
    Check(actual.top == monitor.rcMonitor.top && actual.right - actual.left == static_cast<LONG>((island::kWidth + 2 * island::kShadowMargin) * 1.5f), "DPI preserves top anchor and DIP width");
    popup.HandleMessage(popup.GetHWnd(), WM_DPICHANGED, MAKELONG(96, 96), reinterpret_cast<LPARAM>(&suggested));
    popup.SetPreferences({island::Theme::Light, false, true});
    popup.Hide(true); popup.Toggle({0, 0}); Settle(popup);
    Check(popup.IsVisible(), "closing animation can be interrupted");
    popup.Hide();
    Check(!OwnsRawMouse(popup.GetHWnd()), "closing unregisters raw mouse observation");
    Check(!popup.IsVisible() && popup.FrameWaitHandle() == nullptr, "hidden popup has no frames");
    const bool shortcutWasFree = RegisterHotKey(popup.GetHWnd(), 500,
        MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_RIGHT) != FALSE;
    if (shortcutWasFree) UnregisterHotKey(popup.GetHWnd(), 500);
    const HWND keyboardForeground = GetForegroundWindow();
    popup.Toggle({0, 0}, true);
    Check(popup.GetPresentation().offsetY < -popup.GetLayout().shell.Height(), "opening starts above the display edge");
    HANDLE opening = popup.FrameWaitHandle();
    Check(opening && WaitForSingleObject(opening, 2000) == WAIT_OBJECT_0, "opening frame is paced");
    popup.RenderFrame();
    const float startY = popup.GetPresentation().offsetY;
    popup.Hide(true);
    Check(popup.GetPresentation().offsetY == startY, "closing starts at the current presentation");
    popup.Toggle({0, 0}, true);
    Check(popup.GetPresentation().offsetY == startY, "reopening preserves the interrupted presentation");
    Settle(popup);
    Check(popup.GetPresentation().offsetY == 0 && popup.GetPresentation().scale == 1,
          "spring appearance settles flush against the screen edge");
    Check(GetForegroundWindow() == keyboardForeground, "hotkey opening also preserves foreground focus");
    popup.HandleMessage(popup.GetHWnd(), WM_HOTKEY, 22, MAKELPARAM(MOD_CONTROL | MOD_ALT, VK_RIGHT));
    popup.HandleMessage(popup.GetHWnd(), WM_TIMER, 1, 0);
    Check(committed == 73, "temporary shortcut adjusts brightness without focus");
    popup.Hide();
    if (shortcutWasFree) {
        Check(RegisterHotKey(popup.GetHWnd(), 500, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_RIGHT), "closing releases temporary shortcut");
        UnregisterHotKey(popup.GetHWnd(), 500);
    }
    const DWORD gdiBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD userBefore = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    for (int i = 0; i < 6; ++i) {
        PopupView temporary(GetModuleHandleW(nullptr), {});
        Check(temporary.Register() && temporary.Create(), "recreate popup resources");
    }
    Pump();
    Check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == gdiBefore, "popup recreation does not leak GDI objects");
    // Deferred graphics cleanup can lower the baseline USER count. A decrease
    // is not a leak; repeated popup destruction must not increase the count.
    Check(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <= userBefore, "popup recreation does not leak USER objects");
    popup.Toggle({0, 0}); Settle(popup);
    GetWindowRect(popup.GetHWnd(), &actual);
    popup.NotifyPointerDown({actual.left + 100, actual.top + 25});
    Check(popup.IsVisible(), "click inside the visible panel keeps it open");
    popup.NotifyPointerDown({1234, 9876});
    Check(popup.IsVisible(), "own tray click is handled by toggle rather than outside dismissal");
    testOutsideClicks = true;
    popup.NotifyPointerDown({actual.right + 100, actual.top + 25});
    Check(popup.IsVisible(), "outside dismissal starts an animation rather than hiding immediately");
    Settle(popup);
    Check(!popup.IsVisible() && !OwnsRawMouse(popup.GetHWnd()), "outside click animates out and releases raw observation");
    testOutsideClicks = false;
    popup.Toggle({0, 0}); Settle(popup);
    Sleep(3600); Pump(); Settle(popup);
    Check(!popup.IsVisible() && popup.FrameWaitHandle() == nullptr && !OwnsRawMouse(popup.GetHWnd()),
          "3.5 second timer animates out and leaves no hidden frame or input wakeups");
    std::puts("PopupTests passed");
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1;
}
