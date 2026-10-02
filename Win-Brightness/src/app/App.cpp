#include "App.h"
#include "../resources/resources.h"
#include <algorithm>
#include <cwchar>

namespace {
    constexpr UINT kTrayIconId = 1;
    constexpr int kToggleHotkeyId = 1;

    LRESULT CALLBACK StaticAppWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        App* app = nullptr;
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<LPCREATESTRUCT>(lParam);
            app = static_cast<App*>(create->lpCreateParams);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        } else {
            app = reinterpret_cast<App*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
        }

        return app ? app->HandleMessage(hWnd, message, wParam, lParam)
                   : DefWindowProc(hWnd, message, wParam, lParam);
    }
} // namespace

App::App(HINSTANCE hInstance) : m_hInstance(hInstance) {
    m_hAppIcon = static_cast<HICON>(LoadImage(
        m_hInstance, MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
}

App::~App() {
    if (m_hotkeyRegistered && m_hMsgWnd) {
        UnregisterHotKey(m_hMsgWnd, kToggleHotkeyId);
        m_hotkeyRegistered = false;
    }
    RemoveTrayIcon();
    m_popup.reset();
    m_software.Reset();
    m_controller.Cleanup();

    if (m_hMsgWnd) {
        DestroyWindow(m_hMsgWnd);
        m_hMsgWnd = nullptr;
    }
    if (m_hAppIcon) {
        DestroyIcon(m_hAppIcon);
        m_hAppIcon = nullptr;
    }
    if (m_instanceMutex) {
        ReleaseMutex(m_instanceMutex);
        CloseHandle(m_instanceMutex);
        m_instanceMutex = nullptr;
    }
}

bool App::Init() {
    m_instanceMutex = CreateMutex(nullptr, TRUE, L"Local\\trenches.SingleInstance");
    if (!m_instanceMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindow(L"TrenchesMessageWindow", nullptr)) {
            PostMessage(existing, WM_USER_SHOW_POPUP, 0, 0);
        }
        if (m_instanceMutex) {
            CloseHandle(m_instanceMutex);
            m_instanceMutex = nullptr;
        }
        return false;
    }

    m_state = m_settings.Load();
    m_savedState = m_state;
    m_autostartEnabled = m_settings.IsAutostartEnabled();

    m_controller.SetBrightnessMode(m_state.mode);
    m_controller.SetMonitorSelection(m_state.monitors);
    m_controller.SetEnabled(m_state.enabled);
    m_controller.SetBrightness(m_state.brightness);
    if (!CreateMsgWindow()) {
        return false;
    }
    if (!m_controller.Init(m_hMsgWnd)) {
        return false;
    }

    PopupActions actions;
    actions.setBrightness = [this](int brightness) { SetBrightness(brightness); };
    actions.setEnabled = [this](bool enabled) { SetEnabled(enabled); };
    actions.setMode = [this](BrightnessMode mode) { SetBrightnessMode(mode); };
    actions.setSelection = [this](MonitorSelection selection) { SetMonitorSelection(std::move(selection)); };
    actions.setAutostart = [this](bool enabled) { SetAutostartEnabled(enabled); };

    m_popup = std::make_unique<PopupView>(m_hInstance, std::move(actions));
    if (!m_popup->Register() || !m_popup->Create()) {
        m_popup.reset();
        return false;
    }
    SetWindowLongPtr(m_popup->GetHWnd(), GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(m_hMsgWnd));

    m_hotkeyRegistered = RegisterHotKey(
        m_hMsgWnd, kToggleHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, static_cast<UINT>('B')) != FALSE;
    m_msgTaskbarCreated = RegisterWindowMessage(L"TaskbarCreated");
    AddTrayIcon();
    SyncPopup();
    return true;
}

int App::Run() {
    MSG message{};
    while (true) {
        const BOOL result = GetMessage(&message, nullptr, 0, 0);
        if (result > 0) {
            TranslateMessage(&message);
            DispatchMessage(&message);
            continue;
        }
        return result == 0 ? static_cast<int>(message.wParam) : 1;
    }
}

bool App::CreateMsgWindow() {
    WNDCLASSEX windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = StaticAppWndProc;
    windowClass.hInstance = m_hInstance;
    windowClass.lpszClassName = L"TrenchesMessageWindow";
    if (!RegisterClassEx(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    m_hMsgWnd = CreateWindowEx(
        0, L"TrenchesMessageWindow", L"trenches",
        0, 0, 0, 0, 0, nullptr, nullptr, m_hInstance, this);
    return m_hMsgWnd != nullptr;
}

void App::AddTrayIcon() {
    if (!m_hMsgWnd || !m_hAppIcon) {
        return;
    }

    NOTIFYICONDATA icon{sizeof(icon)};
    icon.hWnd = m_hMsgWnd;
    icon.uID = kTrayIconId;
    icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    icon.uCallbackMessage = WM_USER_SHELLICON;
    icon.hIcon = m_hAppIcon;
    swprintf_s(
        icon.szTip,
        m_state.enabled ? L"trenches · %d%% · Ctrl+Alt+B" : L"trenches · Paused · Ctrl+Alt+B",
        m_state.brightness);

    bool updated = false;
    if (Shell_NotifyIcon(NIM_ADD, &icon)) {
        m_trayIconAdded = true;
        updated = true;
    } else if (m_trayIconAdded && Shell_NotifyIcon(NIM_MODIFY, &icon)) {
        updated = true;
    }

    if (updated) {
        icon.uVersion = NOTIFYICON_VERSION_4;
        m_trayUsesVersion4 = Shell_NotifyIcon(NIM_SETVERSION, &icon) != FALSE;
    }
}

void App::RemoveTrayIcon() {
    if (!m_hMsgWnd || !m_trayIconAdded) {
        return;
    }

    NOTIFYICONDATA icon{sizeof(icon)};
    icon.hWnd = m_hMsgWnd;
    icon.uID = kTrayIconId;
    Shell_NotifyIcon(NIM_DELETE, &icon);
    m_trayIconAdded = false;
    m_trayUsesVersion4 = false;
}

void App::UpdateTrayIcon() {
    if (!m_hMsgWnd || !m_trayIconAdded) {
        return;
    }

    size_t targetCount = 0;
    for (const MonitorInfo& monitor : m_controller.GetMonitors()) {
        if (m_state.monitors.Contains(monitor.id)) {
            ++targetCount;
        }
    }

    NOTIFYICONDATA icon{sizeof(icon)};
    icon.hWnd = m_hMsgWnd;
    icon.uID = kTrayIconId;
    icon.uFlags = NIF_TIP;
    if (m_state.enabled) {
        swprintf_s(icon.szTip, L"trenches · %d%% · %zu display%s",
                   m_state.brightness, targetCount, targetCount == 1 ? L"" : L"s");
    } else {
        swprintf_s(icon.szTip, L"trenches · Paused · Ctrl+Alt+B");
    }
    Shell_NotifyIcon(NIM_MODIFY, &icon);
}

void App::ShowContextMenu(POINT point) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    AppendMenu(menu, MF_STRING, ID_MENU_SHOW, L"Open brightness");
    AppendMenu(menu, MF_STRING, ID_MENU_TOGGLE_ENABLED, m_state.enabled ? L"Pause dimming" : L"Resume dimming");
    AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(menu, MF_STRING | (m_autostartEnabled ? MF_CHECKED : MF_UNCHECKED), ID_MENU_AUTOSTART, L"Start with Windows");
    AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(menu, MF_STRING, ID_MENU_EXIT, L"Exit");
    SetMenuDefaultItem(menu, ID_MENU_SHOW, FALSE);

    SetForegroundWindow(m_hMsgWnd);
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, 0, m_hMsgWnd, nullptr);
    PostMessage(m_hMsgWnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void App::TogglePopup(POINT monitorPoint, bool keyboardInvoked) {
    if (!m_popup) {
        return;
    }
    SyncPopup();
    m_popup->Toggle(monitorPoint, keyboardInvoked);
}

void App::SyncPopup() {
    if (!m_popup) {
        return;
    }

    PopupState popupState;
    popupState.brightness = m_state.brightness;
    popupState.enabled = m_state.enabled;
    popupState.mode = m_state.mode;
    popupState.selection = m_state.monitors;
    popupState.monitors = m_controller.GetMonitors();
    popupState.catalogError = m_controller.GetCatalogError();
    popupState.settingsError = m_settingsError;
    popupState.autostartError = m_autostartError;
    popupState.autostart = m_autostartEnabled;
    popupState.hotkeyAvailable = m_hotkeyRegistered;
    m_popup->SetState(std::move(popupState));
}

void App::ApplySoftwareBrightness() {
    if (!m_state.enabled || m_state.mode != BrightnessMode::Software) {
        m_software.Reset();
        return;
    }

    auto monitors = m_controller.GetMonitors();
    std::erase_if(monitors, [this](const MonitorInfo& monitor) {
        return !m_state.monitors.Contains(monitor.id);
    });
    m_software.ApplyBrightness(m_state.brightness, monitors);
    // Keep the controls above the newly positioned topmost overlays.
    if (m_popup && m_popup->IsVisible()) {
        SetWindowPos(m_popup->GetHWnd(), HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void App::SaveSettings() {
    const auto result = m_settings.Save(m_state, &m_savedState);
    if (result) m_savedState = m_state;
    ReportPersistenceResult(result, false);
}

void App::ReportPersistenceResult(const SettingsResult& result, bool autostart) {
    LSTATUS& previous = autostart ? m_autostartError : m_settingsError;
    const LSTATUS error = result ? ERROR_SUCCESS : result.error();
    const bool newlyFailed = error != ERROR_SUCCESS && error != previous;
    previous = error;
    if (newlyFailed && m_trayIconAdded) {
        NOTIFYICONDATAW icon{sizeof(icon)};
        icon.hWnd = m_hMsgWnd;
        icon.uID = kTrayIconId;
        icon.uFlags = NIF_INFO;
        icon.dwInfoFlags = NIIF_ERROR;
        swprintf_s(icon.szInfoTitle, L"trenches");
        swprintf_s(icon.szInfo, autostart
            ? L"Unable to update Windows startup. Windows error %ld."
            : L"Unable to save settings. Windows error %ld.", error);
        Shell_NotifyIconW(NIM_MODIFY, &icon);
    }
}

void App::SetBrightness(int percent) {
    m_state.brightness = ClampBrightness(percent);
    m_controller.SetBrightness(m_state.brightness);
    ApplySoftwareBrightness();
    SaveSettings();
    UpdateTrayIcon();
    SyncPopup();
}

void App::SetBrightnessMode(BrightnessMode mode) {
    m_state.mode = mode;
    m_controller.SetBrightnessMode(mode);
    ApplySoftwareBrightness();
    SaveSettings();
    UpdateTrayIcon();
    SyncPopup();
}

void App::SetEnabled(bool enabled) {
    m_state.enabled = enabled;
    m_controller.SetEnabled(enabled);
    ApplySoftwareBrightness();
    SaveSettings();
    UpdateTrayIcon();
    SyncPopup();
}

void App::SetMonitorSelection(MonitorSelection selection) {
    if (!selection.all && selection.ids.empty()) {
        const std::vector<MonitorInfo> monitors = m_controller.GetMonitors();
        if (!monitors.empty()) {
            selection.ids.push_back(monitors.front().id);
        }
    }

    m_state.monitors = std::move(selection);
    m_controller.SetMonitorSelection(m_state.monitors);
    ApplySoftwareBrightness();
    SaveSettings();
    UpdateTrayIcon();
    SyncPopup();
}

void App::SetAutostartEnabled(bool enabled) {
    ReportPersistenceResult(m_settings.SetAutostartEnabled(enabled), true);
    m_autostartEnabled = m_settings.IsAutostartEnabled();
    SyncPopup();
}

void App::NormalizeMonitorSelection() {
    if (m_state.monitors.all || !m_state.monitors.ids.empty()) {
        return;
    }

    const std::vector<MonitorInfo> monitors = m_controller.GetMonitors();
    if (!monitors.empty()) {
        m_state.monitors.ids.push_back(monitors.front().id);
        m_controller.SetMonitorSelection(m_state.monitors);
        SaveSettings();
    }
}

POINT App::GetTrayIconPosition() const {
    NOTIFYICONIDENTIFIER identifier{sizeof(identifier)};
    identifier.hWnd = m_hMsgWnd;
    identifier.uID = kTrayIconId;

    RECT rect{};
    if (SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &rect))) {
        return {(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
    }

    POINT point{};
    GetCursorPos(&point);
    return point;
}

POINT App::GetActiveMonitorPoint() const {
    const HWND foreground = GetForegroundWindow();
    RECT rect{};
    if (foreground && GetWindowRect(foreground, &rect)) {
        return {(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
    }

    POINT point{};
    GetCursorPos(&point);
    return point;
}

LRESULT App::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == m_msgTaskbarCreated) {
        AddTrayIcon();
        return 0;
    }

    switch (message) {
    case WM_USER_SHOW_POPUP:
        if (!m_popup || !m_popup->IsVisible()) TogglePopup(GetActiveMonitorPoint(), false);
        return 0;

    case WM_USER_SHELLICON:
        switch (LOWORD(lParam)) {
        case NIN_SELECT:
            if (m_trayUsesVersion4) TogglePopup(GetTrayIconPosition(), false);
            break;
        case NIN_KEYSELECT:
            if (m_trayUsesVersion4) TogglePopup(GetTrayIconPosition(), true);
            break;
        case WM_LBUTTONUP:
            if (!m_trayUsesVersion4) TogglePopup(GetTrayIconPosition(), false);
            break;
        case WM_CONTEXTMENU:
            if (m_trayUsesVersion4) ShowContextMenu(GetTrayIconPosition());
            break;
        case WM_RBUTTONUP:
            if (!m_trayUsesVersion4) ShowContextMenu(GetTrayIconPosition());
            break;
        }
        return 0;

    case WM_HOTKEY:
        if (static_cast<int>(wParam) == kToggleHotkeyId) {
            TogglePopup(GetActiveMonitorPoint(), true);
        }
        return 0;

    case WM_DISPLAYCHANGE:
        m_controller.RequestMonitorRefresh();
        return 0;

    case WM_POWERBROADCAST:
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
            m_controller.RequestMonitorRefresh();
        }
        return TRUE;

    case BrightnessController::kMonitorsChangedMessage:
        NormalizeMonitorSelection();
        ApplySoftwareBrightness();
        UpdateTrayIcon();
        SyncPopup();
        return 0;

    case BrightnessController::kHardwareStatusMessage:
        SyncPopup();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_MENU_SHOW:
            if (!m_popup || !m_popup->IsVisible()) TogglePopup(GetActiveMonitorPoint(), false);
            break;
        case ID_MENU_TOGGLE_ENABLED:
            SetEnabled(!m_state.enabled);
            break;
        case ID_MENU_AUTOSTART:
            SetAutostartEnabled(!m_autostartEnabled);
            break;
        case ID_MENU_EXIT:
            if (m_popup) m_popup->Hide();
            PostQuitMessage(0);
            break;
        }
        return 0;

    case WM_DESTROY:
        if (hWnd == m_hMsgWnd) {
            m_hMsgWnd = nullptr;
            PostQuitMessage(0);
        }
        return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}
