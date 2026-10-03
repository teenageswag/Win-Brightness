#pragma once
#include "../brightness/BrightnessController.h"
#include "../brightness/SoftwareBrightness.h"
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
    void SetInterfacePreferences(island::Preferences preferences);
    void ReportInterfaceError(const wchar_t* message, HRESULT error);
    void TogglePopup(POINT monitorPoint, bool keyboardInvoked);
    void SyncPopup();
    void ApplySoftwareBrightness();
    void SaveSettings();
    void ReportPersistenceResult(const SettingsResult& result, bool autostart);
    void SetBrightness(int percent);
    void SetMonitorBrightness(const std::wstring& id, int percent);
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
    SoftwareBrightness m_software;
    SettingsStore m_settings;
    AppSettings m_state;
    AppSettings m_savedState;
    std::unique_ptr<PopupView> m_popup;
    island::Preferences m_interfacePreferences;
    UINT m_msgTaskbarCreated = 0;
    bool m_autostartEnabled = false;
    bool m_hotkeyRegistered = false;
    bool m_trayIconAdded = false;
    bool m_trayUsesVersion4 = false;
    LSTATUS m_settingsError = ERROR_SUCCESS;
    LSTATUS m_autostartError = ERROR_SUCCESS;
};
