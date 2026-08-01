#include "App.h"
#include "../resources/resources.h"
#include <cwchar>

namespace {
    constexpr UINT kTrayIconId = 1;
    const wchar_t* ModeLabel(BrightnessMode mode) { return mode == BrightnessMode::Software ? L"Software" : L"Hardware"; }

    LRESULT CALLBACK StaticAppWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        App* app = nullptr;
        if (message == WM_NCCREATE) {
            auto* createStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
            app = static_cast<App*>(createStruct->lpCreateParams);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        } else {
            app = reinterpret_cast<App*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
        }
    
        return app ? app->HandleMessage(hWnd, message, wParam, lParam) : DefWindowProc(hWnd, message, wParam, lParam);
    }
} // namespace

App::App(HINSTANCE hInstance) : m_hInstance(hInstance) {
    m_hAppIcon = static_cast<HICON>(
        LoadImage(m_hInstance, MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
}

App::~App() {
    RemoveTrayIcon();
    m_popup.reset();

    if (m_hMsgWnd) {
        DestroyWindow(m_hMsgWnd);
        m_hMsgWnd = nullptr;
    }

    if (m_hAppIcon) {
        DestroyIcon(m_hAppIcon);
        m_hAppIcon = nullptr;
    }
}

bool App::Init() {
    if (!m_controller.Init()) {
        return false;
    }

    m_brightnessMode = m_settings.LoadBrightnessMode();
    m_isEnabled = m_settings.LoadEnabled();
    const bool didFallback = FallbackToSoftwareIfNeeded();
    m_controller.SetBrightnessMode(m_brightnessMode);
    m_controller.SetBrightness(m_settings.LoadBrightness(m_controller.GetBrightness()));

    if (!CreateMsgWindow()) {
        return false;
    }

    m_popup = std::make_unique<PopupView>(m_hInstance, m_controller);
    if (!m_popup->Register() || !m_popup->Create()) {
        m_popup.reset();
        return false;
    }

    // Sync enabled state to popup (without triggering brightness changes on startup)
    if (!m_isEnabled && m_popup) {
        m_popup->SetEnabled(false);
    }

    SetWindowLongPtr(m_popup->GetHWnd(), GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(m_hMsgWnd));
    m_msgTaskbarCreated = RegisterWindowMessage(L"TaskbarCreated");
    AddTrayIcon();

    if (didFallback) {
        ShowDdcFallbackBalloonOnce();
    }

    return true;
}

int App::Run() {
    MSG msg{};
    while (true) {
        const BOOL result = GetMessage(&msg, nullptr, 0, 0);
        if (result > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        return result == 0 ? static_cast<int>(msg.wParam) : 1;
    }
}

bool App::CreateMsgWindow() {
    WNDCLASSEX wcex = {sizeof(wcex)};
    wcex.lpfnWndProc = StaticAppWndProc;
    wcex.hInstance = m_hInstance;
    wcex.lpszClassName = L"ScreenBrightnessMessageWindowClass";

    if (!RegisterClassEx(&wcex) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    m_hMsgWnd = CreateWindowEx(
        0,
        L"ScreenBrightnessMessageWindowClass", L"Brightness Message Handler",
        0, 0, 0, 0, 0,
        nullptr, nullptr, m_hInstance, this);

    return m_hMsgWnd != nullptr;
}

void App::AddTrayIcon() {
    if (!m_hMsgWnd || !m_hAppIcon) {
        return;
    }

    NOTIFYICONDATA nid = {sizeof(nid)};
    nid.hWnd = m_hMsgWnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER_SHELLICON;
    nid.hIcon = m_hAppIcon;
    swprintf_s(nid.szTip, L"Brightness: %d%% [%s]", m_controller.GetBrightness(), ModeLabel(m_brightnessMode));

    bool iconUpdated = false;
    if (Shell_NotifyIcon(NIM_ADD, &nid)) {
        m_trayIconAdded = true;
        iconUpdated = true;
    } else if (m_trayIconAdded && Shell_NotifyIcon(NIM_MODIFY, &nid)) {
        iconUpdated = true;
    }

    if (iconUpdated) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        m_trayUsesVersion4 = Shell_NotifyIcon(NIM_SETVERSION, &nid) != FALSE;
    }
}

void App::RemoveTrayIcon() {
    if (m_hMsgWnd && m_trayIconAdded) {
        NOTIFYICONDATA nid = {sizeof(nid)};
        nid.hWnd = m_hMsgWnd;
        nid.uID = kTrayIconId;
        Shell_NotifyIcon(NIM_DELETE, &nid);
        m_trayIconAdded = false;
        m_trayUsesVersion4 = false;
    }
}

void App::UpdateTrayIcon(int percent) {
    if (!m_hMsgWnd || !m_trayIconAdded) {
        return;
    }

    NOTIFYICONDATA nid = {sizeof(nid)};
    nid.hWnd = m_hMsgWnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_TIP;
    if (m_isEnabled) {
        swprintf_s(nid.szTip, L"Brightness: %d%% [%s]", percent, ModeLabel(m_brightnessMode));
    } else {
        swprintf_s(nid.szTip, L"Brightness: OFF [%s]", ModeLabel(m_brightnessMode));
    }
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void App::ShowDdcFallbackBalloonOnce() {
    if (!m_hMsgWnd || !m_trayIconAdded || m_ddcFallbackBalloonShown) {
        return;
    }

    m_ddcFallbackBalloonShown = true;

    NOTIFYICONDATA nid = {sizeof(nid)};
    nid.hWnd = m_hMsgWnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    swprintf_s(nid.szInfoTitle, L"Brightness");
    swprintf_s(nid.szInfo, L"Hardware brightness is unavailable. Software mode is active.");
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void App::ShowContextMenu(POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    HMENU hModeMenu = CreatePopupMenu();
    if (!hMenu || !hModeMenu) {
        if (hModeMenu) {
            DestroyMenu(hModeMenu);
        }
        if (hMenu) {
            DestroyMenu(hMenu);
        }
        return;
    }

    const UINT autoState = m_settings.IsAutostartEnabled() ? MF_CHECKED : MF_UNCHECKED;
    AppendMenu(hMenu, MF_STRING | autoState, ID_MENU_AUTOSTART, L"Run at startup");
    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hModeMenu, MF_STRING | (m_brightnessMode == BrightnessMode::Hardware ? MF_CHECKED : MF_UNCHECKED), ID_MENU_MODE_HARDWARE, L"Hardware");
    AppendMenu(hModeMenu, MF_STRING | (m_brightnessMode == BrightnessMode::Software ? MF_CHECKED : MF_UNCHECKED), ID_MENU_MODE_SOFTWARE, L"Software");
    AppendMenu(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hModeMenu), L"Brightness mode");
    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hMenu, MF_STRING, ID_MENU_EXIT, L"Exit");

    SetForegroundWindow(m_hMsgWnd);
    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hMsgWnd, nullptr);
    PostMessage(m_hMsgWnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

void App::SetBrightnessMode(BrightnessMode mode) {
    m_brightnessMode = mode;
    const bool didFallback = FallbackToSoftwareIfNeeded();
    m_controller.SetBrightnessMode(m_brightnessMode);
    m_settings.SaveBrightnessMode(m_brightnessMode);
    UpdateTrayIcon(m_controller.GetBrightness());

    if (didFallback) {
        ShowDdcFallbackBalloonOnce();
    }

    if (m_popup) {
        m_popup->UpdateFromController();
    }
}

void App::SetEnabled(bool enabled) {
    m_isEnabled = enabled;
    m_settings.SaveEnabled(enabled);
    UpdateTrayIcon(m_controller.GetBrightness());
}

bool App::FallbackToSoftwareIfNeeded() {
    if (m_brightnessMode == BrightnessMode::Hardware && !m_controller.IsHardwareAvailable()) {
        m_brightnessMode = BrightnessMode::Software;
        return true;
    }

    return false;
}

POINT App::GetTrayIconPosition() const {
    NOTIFYICONIDENTIFIER identifier = {sizeof(identifier)};
    identifier.hWnd = m_hMsgWnd;
    identifier.uID = kTrayIconId;

    RECT rect{};
    if (SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &rect))) {
        return POINT{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
    }

    POINT pt{};
    GetCursorPos(&pt);
    return pt;
}

LRESULT App::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == m_msgTaskbarCreated) {
        AddTrayIcon();
        return 0;
    }

    switch (message) {
    case WM_USER_SHELLICON:
        switch (LOWORD(lParam)) {
        case NIN_SELECT:
            if (m_trayUsesVersion4 && m_popup) {
                m_popup->Toggle(GetTrayIconPosition(), false);
            }
            break;
        case NIN_KEYSELECT:
            if (m_trayUsesVersion4 && m_popup) {
                m_popup->Toggle(GetTrayIconPosition(), true);
            }
            break;
        case WM_LBUTTONUP:
            if (!m_trayUsesVersion4 && m_popup) {
                m_popup->Toggle(GetTrayIconPosition(), false);
            }
            break;
        case WM_CONTEXTMENU:
            if (m_trayUsesVersion4) {
                ShowContextMenu(GetTrayIconPosition());
            }
            break;
        case WM_RBUTTONUP: {
            if (!m_trayUsesVersion4) {
                ShowContextMenu(GetTrayIconPosition());
            }
            break;
        }
        }
        return 0;

    case WM_USER_BRIGHTNESS_CHANGED: {
        const int newPercent = ClampBrightness(static_cast<int>(wParam));
        m_settings.SaveBrightness(newPercent);
        UpdateTrayIcon(newPercent);
        return 0;
    }

    case WM_USER_ENABLED_CHANGED: {
        const bool enabled = wParam != 0;
        SetEnabled(enabled);
        return 0;
    }

    case WM_DISPLAYCHANGE:
        m_controller.RefreshMonitors();
        if (FallbackToSoftwareIfNeeded()) {
            ShowDdcFallbackBalloonOnce();
        }
        m_controller.SetBrightnessMode(m_brightnessMode);
        UpdateTrayIcon(m_controller.GetBrightness());
        if (m_popup) {
            m_popup->UpdateFromController();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_MENU_AUTOSTART:
            m_settings.SetAutostartEnabled(!m_settings.IsAutostartEnabled());
            break;
        case ID_MENU_MODE_HARDWARE:
            SetBrightnessMode(BrightnessMode::Hardware);
            break;
        case ID_MENU_MODE_SOFTWARE:
            SetBrightnessMode(BrightnessMode::Software);
            break;
        case ID_MENU_EXIT:
            if (m_popup) {
                m_popup->Hide();
            }
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
