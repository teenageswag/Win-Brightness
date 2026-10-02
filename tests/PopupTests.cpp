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
    std::puts("PopupTests passed");
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
