#include "ui/PopupView.h"
#include <cstdio>
#include <stdexcept>

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() try {
    int committed = -1;
    int committedBeforeMode = -1;
    PopupActions actions;
    actions.setBrightness = [&](int value) { committed = value; };
    actions.setMode = [&](BrightnessMode) { committedBeforeMode = committed; };
    PopupView popup(GetModuleHandleW(nullptr), std::move(actions));
    Check(popup.Register() && popup.Create(), "create popup");
    PopupState saved;
    saved.brightness = 72;
    popup.SetState(saved);
    popup.HandleMessage(popup.GetHWnd(), WM_KEYDOWN, VK_RIGHT, 0);
    popup.SetState(saved); // Catalog/status publication before timer fires.
    popup.HandleMessage(popup.GetHWnd(), WM_TIMER, 1, 0);
    Check(committed == 73, "preserve pending slider value across state synchronization");

    popup.SetState(saved);
    committed = -1;
    popup.HandleMessage(popup.GetHWnd(), WM_KEYDOWN, VK_RIGHT, 0);
    popup.HandleMessage(popup.GetHWnd(), WM_KEYDOWN, VK_TAB, 0);
    popup.HandleMessage(popup.GetHWnd(), WM_KEYDOWN, VK_RIGHT, 0);
    Check(committedBeforeMode == 73, "commit pending value before changing mode");

    popup.SetState(saved);
    popup.HandleMessage(popup.GetHWnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(300, 172));
    Check(GetCapture() == popup.GetHWnd(), "capture slider drag");
    ReleaseCapture(); // Dispatches WM_CAPTURECHANGED to the actual window procedure.
    const int afterCaptureLoss = committed;
    popup.HandleMessage(popup.GetHWnd(), WM_MOUSEMOVE, 0, MAKELPARAM(578, 172));
    popup.HandleMessage(popup.GetHWnd(), WM_TIMER, 1, 0);
    Check(committed == afterCaptureLoss && afterCaptureLoss != 72,
          "commit drag on capture loss and stop changing brightness afterward");

    popup.HandleMessage(popup.GetHWnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(200, 172));
    popup.HandleMessage(popup.GetHWnd(), WM_CANCELMODE, 0, 0);
    Check(GetCapture() != popup.GetHWnd(), "release capture on cancel mode");
    const int afterCancel = committed;
    popup.HandleMessage(popup.GetHWnd(), WM_MOUSEMOVE, 0, MAKELPARAM(578, 172));
    popup.HandleMessage(popup.GetHWnd(), WM_TIMER, 1, 0);
    Check(committed == afterCancel, "stop drag after cancel mode");

    RECT suggested{100, 150, 900, 750};
    popup.HandleMessage(popup.GetHWnd(), WM_DPICHANGED, MAKELONG(144, 144),
                        reinterpret_cast<LPARAM>(&suggested));
    RECT actual{};
    Check(GetWindowRect(popup.GetHWnd(), &actual) && EqualRect(&suggested, &actual),
          "apply suggested DPI rectangle");
    std::puts("PopupTests passed");
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
