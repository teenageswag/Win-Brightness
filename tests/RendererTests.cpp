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
    CheckHr(renderer.Initialize(window, module, 96, island::PanelHeight(5, 1000)), "initialize renderer");
    Check(renderer.UsesInter(), "renderer uses embedded font");
    PopupState state;
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
    state.monitors[0].brightness = 72;
    state.monitors[1].brightness = 35;
    for (auto& monitor : state.monitors) monitor.hardwareBrightness = monitor.hardwareStatus == HardwareStatus::Available;
    float scale = 1;
    float scroll = 0;
    auto draw = [&](float w, float h, const wchar_t* output) {
        frame.layout = island::Layout::Build(w, h, state.monitors.size(), scroll, w);
        frame.radius = std::min(32.0f, h * 0.14f);
        SetWindowPos(window, nullptr, -10000, -10000, static_cast<int>((w + 32) * scale), static_cast<int>((h + 16) * scale),
                     SWP_NOACTIVATE | SWP_NOZORDER);
        Check(WaitForSingleObject(renderer.FrameHandle(), 2000) == WAIT_OBJECT_0, "VSync frame handle");
        const std::wstring path = argc > 1 ? std::wstring(argv[1]) + L"/" + output : L"";
        CheckHr(renderer.Draw(frame, path.empty() ? nullptr : path.c_str()), "render frame");
    };
    draw(420, island::PanelHeight(8, 1000), L"five-monitors-dark.png");
    scroll = island::kRowStride * 3;
    draw(420, island::PanelHeight(8, 1000), L"scrolled-dark.png");
    scroll = 0;
    state.monitors.resize(2);
    draw(420, island::PanelHeight(2, 1000), L"expanded-dark.png");
    if (argc > 1) {
        frame.keyboardFocus = true;
        frame.focus = {island::Control::Slider, 0};
        draw(420, island::PanelHeight(2, 1000), L"keyboard-slider-dark.png");
        frame.focus = {island::Control::Hardware};
        draw(420, island::PanelHeight(2, 1000), L"keyboard-mode-dark.png");
        frame.focus = {island::Control::Power};
        draw(420, island::PanelHeight(2, 1000), L"keyboard-power-dark.png");
        frame.keyboardFocus = false;
        frame.hot = {island::Control::Slider, 0}; frame.hotScale = 0.97f;
        draw(420, island::PanelHeight(2, 1000), L"pressed-slider-dark.png");
        frame.hot = {island::Control::Power};
        draw(420, island::PanelHeight(2, 1000), L"pressed-power-dark.png");
        frame.hot = {}; frame.hotScale = 1;
    }
    frame.preferences.theme = island::Theme::Light;
    draw(420, island::PanelHeight(2, 1000), L"expanded-light.png");
    state.mode = BrightnessMode::Hardware;
    frame.modePosition = 1;
    draw(420, island::PanelHeight(2, 1000), L"hardware-light.png");
    CheckHr(renderer.Resize(144, island::PanelHeight(5, 1000)), "resize for DPI");
    scale = 1.5f;
    draw(420, island::PanelHeight(2, 1000), L"expanded-144.png");
    scale = 1;
    CheckHr(renderer.Resize(96, island::PanelHeight(5, 1000, 340)), "resize for narrow desktop");
    draw(340, island::PanelHeight(2, 1000, 340), L"narrow-light.png");
    std::puts("RendererTests passed");
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
