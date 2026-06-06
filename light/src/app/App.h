#pragma once
#include "../brightness/BrightnessController.h"
#include "../ui/PopupView.h"
#include "SettingsStore.h"
#include <memory>
#include <shellapi.h>
#include <windows.h>

class App {
public:
    explicit App(HINSTANCE hInstance);
    ~App();

    bool Init();
    int Run();

    LRESULT HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    bool CreateMsgWindow();
    void AddTrayIcon();
    void RemoveTrayIcon();
    void UpdateTrayIcon(int percent);
    void RefreshVisiblePopup();
    void ShowDdcFallbackBalloonOnce();
    void ShowContextMenu(POINT pt);
    void SetBrightnessMode(BrightnessMode mode);
    void SetBrightnessEnabled(bool enabled);
    void FallbackToSoftwareIfNeeded();

    HINSTANCE m_hInstance;
    HWND m_hMsgWnd = nullptr;
    HICON m_hAppIcon = nullptr;
    BrightnessController m_controller;
    SettingsStore m_settings;
    std::unique_ptr<PopupView> m_popup;
    UINT m_msgTaskbarCreated = 0;
    BrightnessMode m_brightnessMode = BrightnessMode::Hardware;
    bool m_ddcFallbackBalloonShown = false;
    bool m_trayIconAdded = false;
};
