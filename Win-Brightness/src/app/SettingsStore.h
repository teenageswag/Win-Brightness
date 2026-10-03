#pragma once

#include "../brightness/BrightnessTypes.h"
#include <windows.h>
#include <expected>

using SettingsResult = std::expected<void, LSTATUS>;

struct AppSettings {
    int brightness = kDefaultBrightness;
    BrightnessMode mode = BrightnessMode::Software;
    bool enabled = true;
    MonitorSelection monitors;
    MonitorBrightnessValues monitorBrightness;
};

class SettingsStore {
public:
    AppSettings Load() const;
    SettingsResult Save(const AppSettings& settings, const AppSettings* previous = nullptr) const;

    bool IsAutostartEnabled() const;
    SettingsResult SetAutostartEnabled(bool enabled) const;

private:
    bool TryReadDword(const wchar_t* valueName, DWORD& value) const;
    SettingsResult WriteDword(HKEY key, const wchar_t* valueName, DWORD value) const;
    std::vector<std::wstring> ReadStringList(const wchar_t* valueName) const;
    SettingsResult WriteStringList(HKEY key, const wchar_t* valueName, const std::vector<std::wstring>& values) const;
};
