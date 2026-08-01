#include "SettingsStore.h"
#include "../platform/Win32Helpers.h"
#include <cwchar>

namespace {
    constexpr const wchar_t* kSettingsKey = L"Software\\Win-Brightness\\Settings";
    constexpr const wchar_t* kLegacySettingsKey = L"Software\\Win-Brightness";
    constexpr const wchar_t* kModeValue = L"DimmingMode";
    constexpr const wchar_t* kBrightnessValue = L"Brightness";
    constexpr const wchar_t* kEnabledValue = L"Enabled";
    constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr const wchar_t* kRunValue = L"Win-Brightness";
} // namespace

bool SettingsStore::TryReadDword(const wchar_t* subKey, const wchar_t* valueName, DWORD& value) const {
    DWORD type = 0;
    DWORD size = sizeof(value);
    return RegGetValue(HKEY_CURRENT_USER, subKey, valueName, RRF_RT_REG_DWORD, &type, &value, &size) == ERROR_SUCCESS && type == REG_DWORD;
}

bool SettingsStore::WriteDword(const wchar_t* subKey, const wchar_t* valueName, DWORD value) const {
    RegistryKey key;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, subKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, key.Put(), nullptr) != ERROR_SUCCESS) {
        return false;
    }

    return RegSetValueEx(key.Get(), valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
}

BrightnessMode SettingsStore::LoadBrightnessMode() const {
    DWORD value = static_cast<DWORD>(BrightnessMode::Hardware);
    if (!TryReadDword(kSettingsKey, kModeValue, value)) {
        TryReadDword(kLegacySettingsKey, kModeValue, value);
    }

    return value == static_cast<DWORD>(BrightnessMode::Software) ? BrightnessMode::Software : BrightnessMode::Hardware;
}

void SettingsStore::SaveBrightnessMode(BrightnessMode mode) const {
    WriteDword(kSettingsKey, kModeValue, static_cast<DWORD>(mode));
}

int SettingsStore::LoadBrightness(int fallbackPercent) const {
    DWORD value = static_cast<DWORD>(ClampBrightness(fallbackPercent));
    TryReadDword(kSettingsKey, kBrightnessValue, value);
    return ClampBrightness(static_cast<int>(value));
}

void SettingsStore::SaveBrightness(int percent) const {
    WriteDword(kSettingsKey, kBrightnessValue, static_cast<DWORD>(ClampBrightness(percent)));
}

bool SettingsStore::IsAutostartEnabled() const {
    RegistryKey key;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, key.Put()) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t value[MAX_PATH * 2] = {};
    DWORD type = 0;
    DWORD size = sizeof(value);
    const LONG result = RegQueryValueEx(key.Get(), kRunValue, nullptr, &type, reinterpret_cast<LPBYTE>(value), &size);
    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return false;
    }

    wchar_t exePath[MAX_PATH] = {};
    const DWORD pathLength = GetModuleFileName(nullptr, exePath, MAX_PATH);
    if (pathLength == 0 || pathLength >= MAX_PATH) {
        return false;
    }

    wchar_t expectedCommand[MAX_PATH + 4] = {};
    swprintf_s(expectedCommand, L"\"%s\"", exePath);
    return _wcsicmp(value, expectedCommand) == 0;
}

void SettingsStore::SetAutostartEnabled(bool enabled) const {
    RegistryKey key;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, key.Put(), nullptr) != ERROR_SUCCESS) {
        return;
    }

    if (enabled) {
        wchar_t exePath[MAX_PATH] = {};
        const DWORD pathLength = GetModuleFileName(nullptr, exePath, MAX_PATH);
        if (pathLength == 0 || pathLength >= MAX_PATH) {
            return;
        }

        wchar_t command[MAX_PATH + 4] = {};
        swprintf_s(command, L"\"%s\"", exePath);
        RegSetValueEx(key.Get(), kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command), static_cast<DWORD>((wcslen(command) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValue(key.Get(), kRunValue);
    }
}

bool SettingsStore::LoadEnabled() const {
    DWORD value = 1;
    TryReadDword(kSettingsKey, kEnabledValue, value);
    return value != 0;
}

void SettingsStore::SaveEnabled(bool enabled) const {
    WriteDword(kSettingsKey, kEnabledValue, enabled ? 1 : 0);
}
