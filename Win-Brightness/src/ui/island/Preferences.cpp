#include "Preferences.h"
#include "../../platform/Win32Helpers.h"

namespace island {
namespace {
constexpr wchar_t kInterfaceKey[] = L"Software\\trenches\\Interface";
bool Read(const wchar_t* name, DWORD& value) {
    DWORD bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kInterfaceKey, name, RRF_RT_REG_DWORD,
                       nullptr, &value, &bytes) == ERROR_SUCCESS && bytes == sizeof(value);
}
}
Preferences LoadPreferences() {
    Preferences preferences;
    DWORD value = 0;
    if (Read(L"Theme", value) && value <= static_cast<DWORD>(Theme::Light)) preferences.theme = static_cast<Theme>(value);
    if (Read(L"Translucent", value) && value <= 1) preferences.translucent = value != 0;
    if (Read(L"Animations", value) && value <= 1) preferences.animations = value != 0;
    return preferences;
}
LSTATUS SavePreferences(const Preferences& preferences) {
    RegistryKey key;
    LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER, kInterfaceKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                    KEY_SET_VALUE, nullptr, key.Put(), nullptr);
    if (result != ERROR_SUCCESS) return result;
    const std::pair<const wchar_t*, DWORD> values[] = {
        {L"Theme", static_cast<DWORD>(preferences.theme)},
        {L"Translucent", preferences.translucent ? 1u : 0u},
        {L"Animations", preferences.animations ? 1u : 0u}
    };
    for (const auto& [name, value] : values) {
        result = RegSetValueExW(key.Get(), name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        if (result != ERROR_SUCCESS) return result;
    }
    return ERROR_SUCCESS;
}
} // namespace island
