#pragma once

#include "../PopupModel.h"
#include "Layout.h"
#include <memory>

namespace island {
enum class Theme { System, Dark, Light };
struct Preferences {
    Theme theme = Theme::System;
    bool translucent = false;
    bool animations = true;
};
struct Frame {
    const PopupState* state = nullptr;
    Layout compact;
    Layout expanded;
    float expansion = 0.0f;
    float opacity = 1.0f;
    float radius = 32.0f;
    float sliderValue = 72.0f;
    float modePosition = 0.0f;
    float numberFeedback = 0.0f;
    Target hot;
    Target focus;
    float hotScale = 1.0f;
    bool keyboardFocus = false;
    Preferences preferences;
};

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    HRESULT Initialize(HWND window, HINSTANCE module, UINT dpi, float maximumHeight);
    HRESULT Resize(UINT dpi, float maximumHeight);
    HRESULT Draw(const Frame& frame, const wchar_t* snapshotPath = nullptr);
    HANDLE FrameHandle() const;
    bool UsesInter() const;
    void RefreshTheme();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    HRESULT SaveFramePng(const wchar_t* path) const;
};
} // namespace island
