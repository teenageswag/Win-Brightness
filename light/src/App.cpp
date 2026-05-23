#include "App.h"

#include <algorithm>
#include <cwchar>

namespace
{

    static constexpr const wchar_t *kSettingsKey = L"Software\\LightBrightness\\Settings";
    static constexpr const wchar_t *kLegacySettingsKey = L"Software\\LightBrightness";
    static constexpr const wchar_t *kModeValue = L"DimmingMode";
    static constexpr const wchar_t *kBrightnessValue = L"Brightness";
    static constexpr const wchar_t *kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    static constexpr const wchar_t *kRunValue = L"LightBrightness";
    static constexpr UINT kTrayIconId = 1;

    int ClampBrightness(int percent)
    {
        return std::clamp(percent, 1, 100);
    }

    const wchar_t *ModeLabel(BrightnessMode mode)
    {
        return mode == BrightnessMode::Software ? L"Software" : L"Hardware";
    }

    bool TryReadDword(HKEY rootKey, const wchar_t *subKey, const wchar_t *valueName, DWORD &value)
    {
        DWORD type = 0;
        DWORD size = sizeof(value);
        return RegGetValue(rootKey,
                           subKey,
                           valueName,
                           RRF_RT_REG_DWORD,
                           &type,
                           &value,
                           &size) == ERROR_SUCCESS &&
               type == REG_DWORD;
    }

    LRESULT CALLBACK StaticAppWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        App *app = nullptr;
        if (message == WM_NCCREATE)
        {
            auto *createStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
            app = static_cast<App *>(createStruct->lpCreateParams);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        else
        {
            app = reinterpret_cast<App *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
        }

        return app ? app->HandleMessage(hWnd, message, wParam, lParam)
                   : DefWindowProc(hWnd, message, wParam, lParam);
    }

} // namespace

App::App(HINSTANCE hInstance) : m_hInstance(hInstance)
{
    m_hAppIcon = static_cast<HICON>(LoadImage(
        m_hInstance,
        MAKEINTRESOURCE(IDI_APP_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));
}

App::~App()
{
    RemoveTrayIcon();
    delete m_pPopup;
    m_pPopup = nullptr;

    if (m_hAppIcon)
    {
        DestroyIcon(m_hAppIcon);
        m_hAppIcon = nullptr;
    }
}

bool App::Init()
{
    if (!m_controller.Init())
    {
        return false;
    }

    m_brightnessMode = LoadBrightnessMode();
    FallbackToSoftwareIfNeeded();
    m_controller.SetBrightnessMode(m_brightnessMode);
    m_controller.SetBrightness(LoadBrightness());

    if (!CreateMsgWindow())
    {
        return false;
    }

    m_pPopup = new PopupView(m_hInstance, m_controller);
    if (!m_pPopup->Register() || !m_pPopup->Create())
    {
        return false;
    }

    SetWindowLongPtr(m_pPopup->GetHWnd(), GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(m_hMsgWnd));
    m_msgTaskbarCreated = RegisterWindowMessage(L"TaskbarCreated");
    AddTrayIcon();

    if (m_brightnessMode == BrightnessMode::Software && !m_controller.IsHardwareAvailable())
    {
        ShowDdcFallbackBalloonOnce();
    }

    return true;
}

int App::Run()
{
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

bool App::CreateMsgWindow()
{
    WNDCLASSEX wcex = {sizeof(wcex)};
    wcex.lpfnWndProc = StaticAppWndProc;
    wcex.hInstance = m_hInstance;
    wcex.lpszClassName = L"ScreenBrightnessMessageWindowClass";

    if (!RegisterClassEx(&wcex) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return false;
    }

    m_hMsgWnd = CreateWindowEx(
        0,
        L"ScreenBrightnessMessageWindowClass",
        L"Brightness Message Handler",
        0,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        m_hInstance,
        this);

    return m_hMsgWnd != nullptr;
}

void App::AddTrayIcon()
{
    if (!m_hMsgWnd || !m_hAppIcon)
    {
        return;
    }

    NOTIFYICONDATA nid = {sizeof(nid)};
    nid.hWnd = m_hMsgWnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER_SHELLICON;
    nid.hIcon = m_hAppIcon;
    swprintf_s(nid.szTip, L"Brightness: %d%% [%s]", m_controller.GetBrightness(), ModeLabel(m_brightnessMode));

    Shell_NotifyIcon(NIM_ADD, &nid);
}

void App::RemoveTrayIcon()
{
    if (m_hMsgWnd)
    {
        NOTIFYICONDATA nid = {sizeof(nid)};
        nid.hWnd = m_hMsgWnd;
        nid.uID = kTrayIconId;
        Shell_NotifyIcon(NIM_DELETE, &nid);
    }
}

void App::UpdateTrayIcon(int percent)
{
    if (!m_hMsgWnd)
    {
        return;
    }

    NOTIFYICONDATA nid = {sizeof(nid)};
    nid.hWnd = m_hMsgWnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_TIP;
    swprintf_s(nid.szTip, L"Brightness: %d%% [%s]", percent, ModeLabel(m_brightnessMode));
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void App::ShowDdcFallbackBalloonOnce()
{
    if (!m_hMsgWnd || m_ddcFallbackBalloonShown)
    {
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

void App::ShowContextMenu(POINT pt)
{
    HMENU hMenu = CreatePopupMenu();
    HMENU hModeMenu = CreatePopupMenu();
    if (!hMenu || !hModeMenu)
    {
        if (hModeMenu)
        {
            DestroyMenu(hModeMenu);
        }
        if (hMenu)
        {
            DestroyMenu(hMenu);
        }
        return;
    }

    const UINT autoState = IsAutostartEnabled() ? MF_CHECKED : MF_UNCHECKED;
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

bool App::IsAutostartEnabled() const
{
    HKEY hKey = nullptr;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    {
        return false;
    }

    wchar_t value[MAX_PATH * 2] = {};
    DWORD type = 0;
    DWORD size = sizeof(value);
    const LONG result = RegQueryValueEx(hKey,
                                        kRunValue,
                                        nullptr,
                                        &type,
                                        reinterpret_cast<LPBYTE>(value),
                                        &size);
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ);
}

void App::SetAutostartEnabled(bool enabled)
{
    HKEY hKey = nullptr;
    if (RegCreateKeyEx(HKEY_CURRENT_USER,
                       kRunKey,
                       0,
                       nullptr,
                       0,
                       KEY_SET_VALUE,
                       nullptr,
                       &hKey,
                       nullptr) != ERROR_SUCCESS)
    {
        return;
    }

    if (enabled)
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileName(nullptr, exePath, MAX_PATH);

        wchar_t command[MAX_PATH + 4] = {};
        swprintf_s(command, L"\"%s\"", exePath);
        RegSetValueEx(hKey,
                      kRunValue,
                      0,
                      REG_SZ,
                      reinterpret_cast<const BYTE *>(command),
                      static_cast<DWORD>((wcslen(command) + 1) * sizeof(wchar_t)));
    }
    else
    {
        RegDeleteValue(hKey, kRunValue);
    }

    RegCloseKey(hKey);
}

BrightnessMode App::LoadBrightnessMode() const
{
    DWORD value = static_cast<DWORD>(BrightnessMode::Hardware);
    if (!TryReadDword(HKEY_CURRENT_USER, kSettingsKey, kModeValue, value))
    {
        TryReadDword(HKEY_CURRENT_USER, kLegacySettingsKey, kModeValue, value);
    }

    return value == static_cast<DWORD>(BrightnessMode::Software)
               ? BrightnessMode::Software
               : BrightnessMode::Hardware;
}

void App::SaveBrightnessMode(BrightnessMode mode) const
{
    HKEY hKey = nullptr;
    if (RegCreateKeyEx(HKEY_CURRENT_USER,
                       kSettingsKey,
                       0,
                       nullptr,
                       0,
                       KEY_SET_VALUE,
                       nullptr,
                       &hKey,
                       nullptr) != ERROR_SUCCESS)
    {
        return;
    }

    const DWORD value = static_cast<DWORD>(mode);
    RegSetValueEx(hKey, kModeValue, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value));
    RegCloseKey(hKey);
}

int App::LoadBrightness() const
{
    DWORD value = static_cast<DWORD>(m_controller.GetBrightness());
    TryReadDword(HKEY_CURRENT_USER, kSettingsKey, kBrightnessValue, value);
    return ClampBrightness(static_cast<int>(value));
}

void App::SaveBrightness(int percent) const
{
    HKEY hKey = nullptr;
    if (RegCreateKeyEx(HKEY_CURRENT_USER,
                       kSettingsKey,
                       0,
                       nullptr,
                       0,
                       KEY_SET_VALUE,
                       nullptr,
                       &hKey,
                       nullptr) != ERROR_SUCCESS)
    {
        return;
    }

    const DWORD value = static_cast<DWORD>(ClampBrightness(percent));
    RegSetValueEx(hKey, kBrightnessValue, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value));
    RegCloseKey(hKey);
}

void App::SetBrightnessMode(BrightnessMode mode)
{
    m_brightnessMode = mode;
    FallbackToSoftwareIfNeeded();
    m_controller.SetBrightnessMode(m_brightnessMode);
    SaveBrightnessMode(m_brightnessMode);
    UpdateTrayIcon(m_controller.GetBrightness());
}

void App::FallbackToSoftwareIfNeeded()
{
    if (m_brightnessMode == BrightnessMode::Hardware && !m_controller.IsHardwareAvailable())
    {
        m_brightnessMode = BrightnessMode::Software;
        ShowDdcFallbackBalloonOnce();
    }
}

LRESULT App::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == m_msgTaskbarCreated)
    {
        AddTrayIcon();
        return 0;
    }

    switch (message)
    {
    case WM_USER_SHELLICON:
        switch (LOWORD(lParam))
        {
        case WM_LBUTTONUP:
            if (m_pPopup)
            {
                POINT pt;
                GetCursorPos(&pt);
                m_pPopup->Toggle(pt);
            }
            break;
        case WM_RBUTTONUP:
        {
            POINT pt;
            GetCursorPos(&pt);
            ShowContextMenu(pt);
            break;
        }
        }
        return 0;

    case WM_USER_BRIGHTNESS_CHANGED:
    {
        const int newPercent = ClampBrightness(static_cast<int>(wParam));
        SaveBrightness(newPercent);
        UpdateTrayIcon(newPercent);
        return 0;
    }

    case WM_USER_MODE_CHANGED:
        m_brightnessMode = m_controller.GetBrightnessMode();
        FallbackToSoftwareIfNeeded();
        m_controller.SetBrightnessMode(m_brightnessMode);
        SaveBrightnessMode(m_brightnessMode);
        UpdateTrayIcon(m_controller.GetBrightness());
        return 0;

    case WM_DISPLAYCHANGE:
        m_controller.RefreshMonitors();
        FallbackToSoftwareIfNeeded();
        m_controller.SetBrightnessMode(m_brightnessMode);
        SaveBrightnessMode(m_brightnessMode);
        UpdateTrayIcon(m_controller.GetBrightness());
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_MENU_AUTOSTART:
            SetAutostartEnabled(!IsAutostartEnabled());
            break;
        case ID_MENU_MODE_HARDWARE:
            SetBrightnessMode(BrightnessMode::Hardware);
            break;
        case ID_MENU_MODE_SOFTWARE:
            SetBrightnessMode(BrightnessMode::Software);
            break;
        case ID_MENU_EXIT:
            if (m_pPopup)
            {
                m_pPopup->Hide();
            }
            PostQuitMessage(0);
            break;
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}
