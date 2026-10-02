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

    SettingsResult ResultFromStatus(LSTATUS status) {
        if (status != ERROR_SUCCESS) return std::unexpected(status);
        return {};
    }

    std::expected<std::wstring, LSTATUS> ExecutablePath() {
        std::vector<wchar_t> buffer(MAX_PATH);
        for (;;) {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0) return std::unexpected(static_cast<LSTATUS>(GetLastError()));
            if (length < buffer.size()) return std::wstring(buffer.data(), length);
            if (buffer.size() >= 32768) return std::unexpected(ERROR_FILENAME_EXCED_RANGE);
            buffer.resize((std::min)(buffer.size() * 2, size_t{32768}));
        }
    }

    std::expected<std::wstring, LSTATUS> StartupCommand() {
        auto path = ExecutablePath();
        if (!path) return std::unexpected(path.error());
        std::wstring command = L"\"" + *path + L"\"";
        // Run/RunOnce command lines are limited to 260 characters even when
        // GetModuleFileNameW and the filesystem support a longer path.
        if (command.size() <= MAX_PATH) return command;

        const DWORD required = GetShortPathNameW(path->c_str(), nullptr, 0);
        if (required == 0 || required > 32768) return std::unexpected(ERROR_FILENAME_EXCED_RANGE);
        std::vector<wchar_t> shortPath(required);
        const DWORD length = GetShortPathNameW(path->c_str(), shortPath.data(), required);
        if (length == 0 || length >= required) return std::unexpected(ERROR_FILENAME_EXCED_RANGE);
        command = L"\"" + std::wstring(shortPath.data(), length) + L"\"";
        if (command.size() > MAX_PATH) return std::unexpected(ERROR_FILENAME_EXCED_RANGE);
        return command;
    }
} // namespace

bool SettingsStore::TryReadDword(const wchar_t* valueName, DWORD& value) const {
    DWORD type = 0;
    DWORD size = sizeof(value);
    return RegGetValue(HKEY_CURRENT_USER, kSettingsKey, valueName, RRF_RT_REG_DWORD, &type, &value, &size) == ERROR_SUCCESS &&
           type == REG_DWORD;
}

SettingsResult SettingsStore::WriteDword(HKEY key, const wchar_t* valueName, DWORD value) const {
    return ResultFromStatus(RegSetValueExW(
        key, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)));
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

SettingsResult SettingsStore::WriteStringList(HKEY key, const wchar_t* valueName, const std::vector<std::wstring>& values) const {
    std::vector<wchar_t> buffer;
    for (const std::wstring& value : values) {
        buffer.insert(buffer.end(), value.begin(), value.end());
        buffer.push_back(L'\0');
    }
    buffer.push_back(L'\0');
    if (values.empty()) {
        buffer.push_back(L'\0');
    }

    if (buffer.size() > MAXDWORD / sizeof(wchar_t)) return std::unexpected(ERROR_INVALID_DATA);
    return ResultFromStatus(RegSetValueExW(
               key, valueName, 0, REG_MULTI_SZ, reinterpret_cast<const BYTE*>(buffer.data()),
               static_cast<DWORD>(buffer.size() * sizeof(wchar_t))));
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

SettingsResult SettingsStore::Save(const AppSettings& settings, const AppSettings* previous) const try {
    const bool brightness = !previous || settings.brightness != previous->brightness;
    const bool mode = !previous || settings.mode != previous->mode;
    const bool enabled = !previous || settings.enabled != previous->enabled;
    const bool all = !previous || settings.monitors.all != previous->monitors.all;
    const bool ids = !previous || settings.monitors.ids != previous->monitors.ids;
    if (!brightness && !mode && !enabled && !all && !ids) return {};

    RegistryKey key;
    const LSTATUS opened = RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0,
                                          KEY_SET_VALUE, nullptr, key.Put(), nullptr);
    if (opened != ERROR_SUCCESS) return std::unexpected(opened);
    if (brightness) {
        if (auto result = WriteDword(key.Get(), kBrightnessValue, static_cast<DWORD>(ClampBrightness(settings.brightness))); !result) return result;
    }
    if (mode) {
        if (auto result = WriteDword(key.Get(), kModeValue, static_cast<DWORD>(settings.mode)); !result) return result;
    }
    if (enabled) {
        if (auto result = WriteDword(key.Get(), kEnabledValue, settings.enabled ? 1u : 0u); !result) return result;
    }
    if (all) {
        if (auto result = WriteDword(key.Get(), kAllMonitorsValue, settings.monitors.all ? 1u : 0u); !result) return result;
    }
    return ids ? WriteStringList(key.Get(), kMonitorIdsValue, settings.monitors.ids) : SettingsResult{};
} catch (const std::bad_alloc&) {
    return std::unexpected(ERROR_NOT_ENOUGH_MEMORY);
}

bool SettingsStore::IsAutostartEnabled() const try {
    RegistryKey key;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, key.Put()) != ERROR_SUCCESS) {
        return false;
    }

    constexpr DWORD flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND | RRF_ZEROONFAILURE;
    DWORD type = 0;
    DWORD size = 0;
    if (RegGetValueW(key.Get(), nullptr, kRunValue, flags, &type, nullptr, &size) != ERROR_SUCCESS ||
        size % sizeof(wchar_t) != 0 || size > 64 * 1024) {
        return false;
    }
    std::vector<wchar_t> value(size / sizeof(wchar_t) + 1, L'\0');
    size = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    if (RegGetValueW(key.Get(), nullptr, kRunValue, flags, &type, value.data(), &size) != ERROR_SUCCESS ||
        size % sizeof(wchar_t) != 0) {
        return false;
    }
    if (type == REG_EXPAND_SZ) {
        const DWORD required = ExpandEnvironmentStringsW(value.data(), nullptr, 0);
        if (required == 0 || required > 32768) return false;
        std::vector<wchar_t> expanded(required);
        const DWORD copied = ExpandEnvironmentStringsW(value.data(), expanded.data(), required);
        if (copied == 0 || copied > required) return false;
        value = std::move(expanded);
    }
    const auto expectedCommand = StartupCommand();
    return expectedCommand && _wcsicmp(value.data(), expectedCommand->c_str()) == 0;
} catch (const std::bad_alloc&) {
    return false;
}

SettingsResult SettingsStore::SetAutostartEnabled(bool enabled) const try {
    RegistryKey key;
    const LSTATUS opened = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                                          KEY_SET_VALUE, nullptr, key.Put(), nullptr);
    if (opened != ERROR_SUCCESS) return std::unexpected(opened);

    if (!enabled) {
        const LSTATUS deleted = RegDeleteValueW(key.Get(), kRunValue);
        return ResultFromStatus(deleted == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : deleted);
    }

    const auto command = StartupCommand();
    if (!command) return std::unexpected(command.error());
    return ResultFromStatus(RegSetValueExW(
        key.Get(), kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command->c_str()),
        static_cast<DWORD>((command->size() + 1) * sizeof(wchar_t))));
} catch (const std::bad_alloc&) {
    return std::unexpected(ERROR_NOT_ENOUGH_MEMORY);
}
