#pragma once

#include <windows.h>
#include <shellapi.h>
#include "BrightnessController.h"
#include "PopupView.h"
#include "resources.h"

#define ID_MENU_AUTOSTART 1002
#define ID_MENU_MODE_HARDWARE 1101
#define ID_MENU_MODE_SOFTWARE 1102

class App
{
public:
    App(HINSTANCE hInstance);
    ~App();

    bool Init();
    int Run();

    LRESULT HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    bool CreateMsgWindow();
    void AddTrayIcon();
    void RemoveTrayIcon();
    void UpdateTrayIcon(int percent);
    void ShowDdcFallbackBalloonOnce();
    void ShowContextMenu(POINT pt);
    bool IsAutostartEnabled() const;
    void SetAutostartEnabled(bool enabled);
    BrightnessMode LoadBrightnessMode() const;
    void SaveBrightnessMode(BrightnessMode mode) const;
    int LoadBrightness() const;
    void SaveBrightness(int percent) const;
    void SetBrightnessMode(BrightnessMode mode);
    void FallbackToSoftwareIfNeeded();

    HINSTANCE m_hInstance;
    HWND m_hMsgWnd = nullptr;
    HICON m_hAppIcon = nullptr;
    BrightnessController m_controller;
    PopupView *m_pPopup = nullptr;
    UINT m_msgTaskbarCreated = 0;
    BrightnessMode m_brightnessMode = BrightnessMode::Hardware;
    bool m_ddcFallbackBalloonShown = false;
};
