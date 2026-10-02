#include "SettingsStore.h"
#include "../platform/Win32Helpers.h"
#include <cwchar>
#include <vector>

namespace {
    constexpr const wchar_t* kSettingsKey = L"Software\\trenches\\Settings";
    constexpr const wchar_t* kBrightnessValue = L"Brightness";
    constexpr const wchar_t* kModeValue = L"Mode";
    constexpr const wchar_t* kEnabledValue = L"Enabled";
    constexpr const wchar_t* kAllMonitorsValue = L"AllMonitors";
    constexpr const wchar_t* kMonitorIdsValue = L"MonitorIds";
    constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr const wchar_t* kRunValue = L"trenches";
} // namespace

bool SettingsStore::TryReadDword(const wchar_t* valueName, DWORD& value) const {
    DWORD type = 0;
    DWORD size = sizeof(value);
    return RegGetValue(HKEY_CURRENT_USER, kSettingsKey, valueName, RRF_RT_REG_DWORD, &type, &value, &size) == ERROR_SUCCESS &&
           type == REG_DWORD;
}

bool SettingsStore::WriteDword(const wchar_t* valueName, DWORD value) const {
    RegistryKey key;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, key.Put(), nullptr) != ERROR_SUCCESS) {
        return false;
    }

    return RegSetValueEx(
               key.Get(), valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
}

std::vector<std::wstring> SettingsStore::ReadStringList(const wchar_t* valueName) const {
    DWORD size = 0;
    if (RegGetValue(HKEY_CURRENT_USER, kSettingsKey, valueName, RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS ||
        size < sizeof(wchar_t) || size % sizeof(wchar_t) != 0 || size > 64 * 1024) {
        return {};
    }

    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 2, L'\0');
    DWORD capacity = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, valueName, RRF_RT_REG_MULTI_SZ,
                     nullptr, buffer.data(), &capacity) != ERROR_SUCCESS ||
        capacity % sizeof(wchar_t) != 0) {
        return {};
    }

    std::vector<std::wstring> values;
    const wchar_t* current = buffer.data();
    const wchar_t* end = current + capacity / sizeof(wchar_t);
    while (current < end && *current != L'\0') {
        const wchar_t* terminator = std::find(current, end, L'\0');
        if (terminator == end) return {};
        values.emplace_back(current, terminator);
        current = terminator + 1;
    }
    return values;
}

bool SettingsStore::WriteStringList(const wchar_t* valueName, const std::vector<std::wstring>& values) const {
    RegistryKey key;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, key.Put(), nullptr) != ERROR_SUCCESS) {
        return false;
    }

    std::vector<wchar_t> buffer;
    for (const std::wstring& value : values) {
        buffer.insert(buffer.end(), value.begin(), value.end());
        buffer.push_back(L'\0');
    }
    buffer.push_back(L'\0');
    if (values.empty()) {
        buffer.push_back(L'\0');
    }

    return RegSetValueEx(
               key.Get(), valueName, 0, REG_MULTI_SZ, reinterpret_cast<const BYTE*>(buffer.data()),
               static_cast<DWORD>(buffer.size() * sizeof(wchar_t))) == ERROR_SUCCESS;
}

AppSettings SettingsStore::Load() const {
    AppSettings settings;

    DWORD value = 0;
    if (TryReadDword(kBrightnessValue, value)) {
        settings.brightness = static_cast<int>(std::clamp<DWORD>(value, kMinBrightness, kMaxBrightness));
    }

    if (TryReadDword(kModeValue, value)) {
        settings.mode = value == static_cast<DWORD>(BrightnessMode::Hardware)
                            ? BrightnessMode::Hardware
                            : BrightnessMode::Software;
    }

    if (TryReadDword(kEnabledValue, value)) {
        settings.enabled = value != 0;
    }

    if (TryReadDword(kAllMonitorsValue, value)) {
        settings.monitors.all = value != 0;
    }
    settings.monitors.ids = ReadStringList(kMonitorIdsValue);

    return settings;
}

void SettingsStore::Save(const AppSettings& settings) const {
    WriteDword(kBrightnessValue, static_cast<DWORD>(ClampBrightness(settings.brightness)));
    WriteDword(kModeValue, static_cast<DWORD>(settings.mode));
    WriteDword(kEnabledValue, settings.enabled ? 1u : 0u);
    WriteDword(kAllMonitorsValue, settings.monitors.all ? 1u : 0u);
    WriteStringList(kMonitorIdsValue, settings.monitors.ids);
}

bool SettingsStore::IsAutostartEnabled() const {
    RegistryKey key;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, key.Put()) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t value[MAX_PATH * 2] = {};
    DWORD type = 0;
    DWORD size = sizeof(value);
    const LSTATUS result = RegGetValueW(
        key.Get(), nullptr, kRunValue,
        RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND | RRF_ZEROONFAILURE,
        &type, value, &size);
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

    if (!enabled) {
        RegDeleteValue(key.Get(), kRunValue);
        return;
    }

    wchar_t exePath[MAX_PATH] = {};
    const DWORD pathLength = GetModuleFileName(nullptr, exePath, MAX_PATH);
    if (pathLength == 0 || pathLength >= MAX_PATH) {
        return;
    }

    wchar_t command[MAX_PATH + 4] = {};
    swprintf_s(command, L"\"%s\"", exePath);
    RegSetValueEx(
        key.Get(), kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command),
        static_cast<DWORD>((wcslen(command) + 1) * sizeof(wchar_t)));
}
