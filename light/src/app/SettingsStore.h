#pragma once
#include "../brightness/BrightnessTypes.h"
#include <windows.h>

class SettingsStore {
public:
    BrightnessMode LoadBrightnessMode() const;
    void SaveBrightnessMode(BrightnessMode mode) const;

    int LoadBrightness(int fallbackPercent) const;
    void SaveBrightness(int percent) const;

    bool LoadBrightnessEnabled() const;
    void SaveBrightnessEnabled(bool enabled) const;

    bool IsAutostartEnabled() const;
    void SetAutostartEnabled(bool enabled) const;

private:
    bool TryReadDword(const wchar_t* subKey, const wchar_t* valueName, DWORD& value) const;
};
