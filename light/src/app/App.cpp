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
    FallbackToSoftwareIfNeeded();
    m_controller.SetBrightnessMode(m_brightnessMode);
    m_controller.SetEnabled(m_settings.LoadBrightnessEnabled());
    m_controller.SetBrightness(m_settings.LoadBrightness(m_controller.GetBrightness()));

    if (!CreateMsgWindow()) {
        return false;
    }

    m_popup = std::make_unique<PopupView>(m_hInstance, m_controller);
    if (!m_popup->Register() || !m_popup->Create()) {
        m_popup.reset();
        return false;
    }

    SetWindowLongPtr(m_popup->GetHWnd(), GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(m_hMsgWnd));
    m_msgTaskbarCreated = RegisterWindowMessage(L"TaskbarCreated");
    AddTrayIcon();

    if (m_brightnessMode == BrightnessMode::Software && !m_controller.IsHardwareAvailable()) {
        ShowDdcFallbackBalloonOnce();
    }

    return true;
}

int App::Run() {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
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
    swprintf_s(nid.szTip, L"Brightness: %d%% [%s, %s]", m_controller.GetBrightness(), ModeLabel(m_brightnessMode),
               m_controller.IsEnabled() ? L"On" : L"Off");

    if (Shell_NotifyIcon(NIM_ADD, &nid)) {
        m_trayIconAdded = true;
        return;
    }

    if (m_trayIconAdded && Shell_NotifyIcon(NIM_MODIFY, &nid)) {
        return;
    }
}

void App::RemoveTrayIcon() {
    if (m_hMsgWnd && m_trayIconAdded) {
        NOTIFYICONDATA nid = {sizeof(nid)};
        nid.hWnd = m_hMsgWnd;
        nid.uID = kTrayIconId;
        Shell_NotifyIcon(NIM_DELETE, &nid);
        m_trayIconAdded = false;
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
    swprintf_s(nid.szTip, L"Brightness: %d%% [%s, %s]", percent, ModeLabel(m_brightnessMode), m_controller.IsEnabled() ? L"On" : L"Off");
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

    const UINT enabledState = m_controller.IsEnabled() ? MF_CHECKED : MF_UNCHECKED;
    const UINT autoState = m_settings.IsAutostartEnabled() ? MF_CHECKED : MF_UNCHECKED;
    AppendMenu(hMenu, MF_STRING | enabledState, ID_MENU_ENABLED, L"Brightness enabled");
    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
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
    FallbackToSoftwareIfNeeded();
    m_controller.SetBrightnessMode(m_brightnessMode);
    m_settings.SaveBrightnessMode(m_brightnessMode);
    UpdateTrayIcon(m_controller.GetBrightness());
    if (m_popup) {
        m_popup->Refresh();
    }
}

void App::SetBrightnessEnabled(bool enabled) {
    m_controller.SetEnabled(enabled);
    m_settings.SaveBrightnessEnabled(enabled);
    UpdateTrayIcon(m_controller.GetBrightness());
    if (m_popup) {
        m_popup->Refresh();
    }
}

void App::FallbackToSoftwareIfNeeded() {
    if (m_brightnessMode == BrightnessMode::Hardware && !m_controller.IsHardwareAvailable()) {
        m_brightnessMode = BrightnessMode::Software;
        ShowDdcFallbackBalloonOnce();
    }
}

LRESULT App::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == m_msgTaskbarCreated) {
        AddTrayIcon();
        return 0;
    }

    switch (message) {
    case WM_USER_SHELLICON:
        switch (LOWORD(lParam)) {
        case WM_LBUTTONUP:
            if (m_popup) {
                POINT pt;
                GetCursorPos(&pt);
                m_popup->Toggle(pt);
            }
            break;
        case WM_RBUTTONUP: {
            POINT pt;
            GetCursorPos(&pt);
            ShowContextMenu(pt);
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

    case WM_DISPLAYCHANGE:
        m_controller.RefreshMonitors();
        FallbackToSoftwareIfNeeded();
        m_controller.SetBrightnessMode(m_brightnessMode);
        m_settings.SaveBrightnessMode(m_brightnessMode);
        UpdateTrayIcon(m_controller.GetBrightness());
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_MENU_ENABLED:
            SetBrightnessEnabled(!m_controller.IsEnabled());
            break;
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
