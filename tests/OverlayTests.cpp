#include "ui/DimOverlay.h"
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace {
WNDPROC original = nullptr;
unsigned positionChanges = 0;
unsigned paints = 0;
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
LRESULT CALLBACK Spy(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_WINDOWPOSCHANGED) ++positionChanges;
    if (message == WM_PAINT) ++paints;
    return CallWindowProcW(original, window, message, wParam, lParam);
}
std::vector<HWND> OverlayWindows() {
    std::vector<HWND> windows;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
        wchar_t name[64]{};
        GetClassNameW(window, name, 64);
        if (std::wstring_view(name) == L"TrenchesDimOverlay") {
            reinterpret_cast<std::vector<HWND>*>(data)->push_back(window);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&windows));
    std::ranges::sort(windows);
    return windows;
}
}

int main() try {
    DimOverlay overlay;
    std::vector<MonitorInfo> monitors(2);
    monitors[0].id = L"first";
    monitors[0].bounds = {-10000, -10000, -9990, -9990};
    monitors[1].id = L"second";
    monitors[1].bounds = {-10000, -9980, -9990, -9970};
    overlay.Apply(50, monitors); // Tiny off-screen windows, no real monitor dimming.
    const auto before = OverlayWindows();
    Check(before.size() == 2, "create independent windows on the calling UI thread");
    BYTE alphaBefore = 0;
    Check(GetLayeredWindowAttributes(before[0], nullptr, &alphaBefore, nullptr), "read initial overlay alpha");
    for (HWND window : before) {
        original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Spy)));
    }
    overlay.Apply(60, monitors);
    BYTE alphaAfter = 0;
    Check(GetLayeredWindowAttributes(before[0], nullptr, &alphaAfter, nullptr) && alphaAfter != alphaBefore,
          "update opacity without recreating windows");
    Check(OverlayWindows() == before && positionChanges == 0 && paints == 0,
          "avoid moving or repainting an unchanged black surface");

    monitors[0].bounds.left += 1;
    overlay.Apply(60, monitors);
    Check(positionChanges > 0, "update geometry when monitor bounds change");
    monitors[0].brightness = 20; monitors[1].brightness = 80;
    overlay.Apply(monitors);
    BYTE firstAlpha = 0, secondAlpha = 0;
    Check(GetLayeredWindowAttributes(before[0], nullptr, &firstAlpha, nullptr) &&
          GetLayeredWindowAttributes(before[1], nullptr, &secondAlpha, nullptr) && firstAlpha != secondAlpha,
          "independent display values produce different overlay alpha");
    monitors[1].brightness = 100;
    overlay.Apply(monitors);
    Check(OverlayWindows().size() == 1, "100 percent removes only its own display overlay");
    monitors.resize(1);
    overlay.Apply(60, monitors);
    Check(OverlayWindows().size() == 1, "remove deselected monitor window");
    overlay.Destroy();
    Check(OverlayWindows().empty(), "destroy overlays on their owning thread");
    std::puts("OverlayTests passed");
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
