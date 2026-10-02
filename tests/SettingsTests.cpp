#include "app/SettingsStore.h"
#include "platform/Win32Helpers.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Redirect this process's HKCU to a disposable subtree, removed on exit.
class TestRegistry {
public:
    TestRegistry() {
        m_path = L"Software\\trenches-tests-" + std::to_wstring(GetCurrentProcessId()) +
                 L"-" + std::to_wstring(GetTickCount64());
        Check(RegCreateKeyExW(HKEY_CURRENT_USER, m_path.c_str(), 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr,
                             m_root.Put(), nullptr) == ERROR_SUCCESS, "create test registry");
        Check(RegOverridePredefKey(HKEY_CURRENT_USER, m_root.Get()) == ERROR_SUCCESS,
              "redirect test registry");
    }
    ~TestRegistry() {
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        m_root.Reset();
        RegDeleteTreeW(HKEY_CURRENT_USER, m_path.c_str());
        RegDeleteKeyW(HKEY_CURRENT_USER, m_path.c_str());
    }
    void Write(const wchar_t* path, const wchar_t* name, DWORD type,
               const void* data, DWORD bytes) {
        RegistryKey key;
        Check(RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_SET_VALUE,
                             nullptr, key.Put(), nullptr) == ERROR_SUCCESS, "create value key");
        Check(RegSetValueExW(key.Get(), name, 0, type,
                            static_cast<const BYTE*>(data), bytes) == ERROR_SUCCESS, "write test value");
    }
private:
    std::wstring m_path;
    RegistryKey m_root;
};
}

int main() try {
    TestRegistry registry;
    SettingsStore store;
    constexpr auto settingsPath = L"Software\\trenches\\Settings";

    const wchar_t ids[] = L"first\0second\0";
    registry.Write(settingsPath, L"MonitorIds", REG_MULTI_SZ, ids, sizeof(ids));
    Check(store.Load().monitors.ids == std::vector<std::wstring>{L"first", L"second"},
          "read normal multi-string");

    const BYTE odd[] = { 'a', 0, 'b' };
    registry.Write(settingsPath, L"MonitorIds", REG_MULTI_SZ, odd, sizeof(odd));
    Check(store.Load().monitors.ids.empty(), "reject odd-byte multi-string");

    const wchar_t unterminated[] = { L'a', L'b' };
    registry.Write(settingsPath, L"MonitorIds", REG_MULTI_SZ, unterminated, sizeof(unterminated));
    const auto repaired = store.Load().monitors.ids;
    Check(repaired.empty() || repaired == std::vector<std::wstring>{L"ab"},
          "handle missing multi-string terminators safely");

    const DWORD overflow = (std::numeric_limits<DWORD>::max)();
    registry.Write(settingsPath, L"Brightness", REG_DWORD, &overflow, sizeof(overflow));
    Check(store.Load().brightness == kMaxBrightness, "clamp unsigned brightness before conversion");

    const std::vector<wchar_t> full(MAX_PATH * 2, L'x');
    registry.Write(L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"trenches",
                   REG_SZ, full.data(), static_cast<DWORD>(full.size() * sizeof(wchar_t)));
    Check(!store.IsAutostartEnabled(), "reject full unterminated autostart command");

    std::puts("SettingsTests passed");
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
