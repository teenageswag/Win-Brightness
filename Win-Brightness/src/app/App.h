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
    void UpdateTrayIcon();
    void ShowContextMenu(POINT pt);
    void TogglePopup(POINT monitorPoint, bool keyboardInvoked);
    void SyncPopup();
    void SetBrightness(int percent);
    void SetBrightnessMode(BrightnessMode mode);
    void SetEnabled(bool enabled);
    void SetMonitorSelection(MonitorSelection selection);
    void SetAutostartEnabled(bool enabled);
    void NormalizeMonitorSelection();
    POINT GetTrayIconPosition() const;
    POINT GetActiveMonitorPoint() const;

    HINSTANCE m_hInstance;
    HANDLE m_instanceMutex = nullptr;
    HWND m_hMsgWnd = nullptr;
    HICON m_hAppIcon = nullptr;
    BrightnessController m_controller;
    SettingsStore m_settings;
    AppSettings m_state;
    std::unique_ptr<PopupView> m_popup;
    UINT m_msgTaskbarCreated = 0;
    bool m_autostartEnabled = false;
    bool m_hotkeyRegistered = false;
    bool m_trayIconAdded = false;
    bool m_trayUsesVersion4 = false;
};
