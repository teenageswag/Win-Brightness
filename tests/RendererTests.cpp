#include "ui/island/Renderer.h"
#include <cstdio>
#include <stdexcept>

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void CheckHr(HRESULT result, const char* message) {
    if (FAILED(result)) {
        std::fprintf(stderr, "%s: 0x%08lX\n", message, static_cast<unsigned long>(result));
        throw std::runtime_error(message);
    }
}
int wmain(int argc, wchar_t** argv) try {
    const auto module = GetModuleHandleW(nullptr);
    HWND window = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"STATIC", L"Island render test", WS_POPUP, -10000, -10000, 452, 465, nullptr, nullptr, module, nullptr);
    Check(window != nullptr, "create renderer window");
    struct WindowOwner { HWND value; ~WindowOwner() { DestroyWindow(value); } } owner{window};
    island::Renderer renderer;
    CheckHr(renderer.Initialize(window, module, 96, island::ExpandedHeight(5, 1000)), "initialize renderer");
    Check(renderer.UsesInter(), "renderer uses embedded font");
    PopupState state;
    state.brightness = 72;
    for (size_t i = 0; i < 8; ++i) {
        MonitorInfo monitor;
        monitor.id = std::to_wstring(i);
        monitor.name = i == 0 ? L"Studio Display" : L"Monitor " + std::to_wstring(i + 1);
        monitor.primary = i == 0;
        monitor.hardwareStatus = i == 1 ? HardwareStatus::Unsupported : HardwareStatus::Available;
        state.monitors.push_back(std::move(monitor));
    }
    island::Frame frame;
    frame.state = &state;
    frame.preferences.theme = island::Theme::Dark;
    frame.sliderValue = 72;
    auto draw = [&](float w, float h, float expansion, const wchar_t* output) {
        frame.compact = island::Layout::Build(w, h, false, state.monitors.size(), 0, 0);
        frame.expanded = island::Layout::Build(w, h, true, state.monitors.size(), 0, 0);
        frame.expansion = expansion;
        frame.radius = expansion == 0 ? 32 : h * 0.14f;
        SetWindowPos(window, nullptr, -10000, -10000, static_cast<int>(w + 32), static_cast<int>(h + 16),
                     SWP_NOACTIVATE | SWP_NOZORDER);
        Check(WaitForSingleObject(renderer.FrameHandle(), 2000) == WAIT_OBJECT_0, "VSync frame handle");
        const std::wstring path = argc > 1 ? std::wstring(argv[1]) + L"/" + output : L"";
        CheckHr(renderer.Draw(frame, path.empty() ? nullptr : path.c_str()), "render frame");
    };
    draw(344, 64, 0, L"compact-dark.png");
    state.monitors.resize(2);
    draw(420, island::ExpandedHeight(2, 1000), 1, L"expanded-dark.png");
    frame.preferences.theme = island::Theme::Light;
    draw(420, island::ExpandedHeight(2, 1000), 1, L"expanded-light.png");
    state.mode = BrightnessMode::Hardware;
    frame.modePosition = 1;
    draw(420, island::ExpandedHeight(2, 1000), 1, L"hardware-light.png");
    CheckHr(renderer.Resize(144, island::ExpandedHeight(5, 1000)), "resize for DPI");
    draw(420, island::ExpandedHeight(2, 1000), 1, L"expanded-144.png");
    std::puts("RendererTests passed");
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
