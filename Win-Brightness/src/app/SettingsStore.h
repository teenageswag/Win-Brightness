#pragma once

#include "../brightness/BrightnessTypes.h"
#include <windows.h>

struct AppSettings {
    int brightness = kDefaultBrightness;
    BrightnessMode mode = BrightnessMode::Software;
    bool enabled = true;
    MonitorSelection monitors;
};

class SettingsStore {
public:
    AppSettings Load() const;
    void Save(const AppSettings& settings) const;

    bool IsAutostartEnabled() const;
    void SetAutostartEnabled(bool enabled) const;

private:
    bool TryReadDword(const wchar_t* valueName, DWORD& value) const;
    bool WriteDword(const wchar_t* valueName, DWORD value) const;
    std::vector<std::wstring> ReadStringList(const wchar_t* valueName) const;
    bool WriteStringList(const wchar_t* valueName, const std::vector<std::wstring>& values) const;
};
