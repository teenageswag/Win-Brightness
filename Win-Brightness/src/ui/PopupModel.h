#pragma once

#include "../brightness/BrightnessTypes.h"
#include <functional>

struct PopupState {
    int brightness = kDefaultBrightness;
    bool enabled = true;
    BrightnessMode mode = BrightnessMode::Software;
    MonitorSelection selection;
    std::vector<MonitorInfo> monitors;
    bool autostart = false;
    bool hotkeyAvailable = true;
    DWORD catalogError = ERROR_SUCCESS;
    LSTATUS settingsError = ERROR_SUCCESS;
    LSTATUS autostartError = ERROR_SUCCESS;
};

struct PopupActions {
    std::function<void(int)> setBrightness;
    std::function<void(const std::wstring&, int)> setMonitorBrightness;
    std::function<void(bool)> setEnabled;
    std::function<void(BrightnessMode)> setMode;
    std::function<void(MonitorSelection)> setSelection;
    std::function<void(bool)> setAutostart;
    std::function<void(HRESULT)> reportUiError;
    std::function<void(POINT)> showContextMenu;
};
