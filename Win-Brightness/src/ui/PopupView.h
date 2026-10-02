#pragma once

#include "PopupModel.h"
#include "island/Renderer.h"
#include <memory>

class PopupView {
public:
    PopupView(HINSTANCE instance, PopupActions actions);
    ~PopupView();
    PopupView(const PopupView&) = delete;
    PopupView& operator=(const PopupView&) = delete;
    bool Register();
    bool Create();
    void Toggle(POINT monitorPoint, bool keyboardInvoked = false);
    void Hide(bool animated = false);
    bool IsVisible() const;
    HWND GetHWnd() const;
    void SetState(PopupState state);
    void SetPreferences(island::Preferences preferences);
    island::Preferences GetPreferences() const;
    void SetExpanded(bool expanded);
    bool IsExpanded() const;
    island::Layout GetLayout() const;
    HRESULT LastRenderError() const;
    HANDLE FrameWaitHandle() const;
    void RenderFrame();
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
};
