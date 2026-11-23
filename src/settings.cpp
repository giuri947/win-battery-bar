#include "settings.h"

#include <windows.h>
#include <algorithm>
#include <sstream>

namespace {
    constexpr const wchar_t* kRegSubKey = L"Software\\WinBatteryBar";
    constexpr const wchar_t* kRunSubKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr const wchar_t* kAppName   = L"WinBatteryBar";

    bool ReadDword(HKEY key, const wchar_t* name, DWORD& outVal) {
        DWORD type = 0;
        DWORD size = sizeof(DWORD);
        LONG rc = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(&outVal), &size);
        return rc == ERROR_SUCCESS && type == REG_DWORD;
    }

    void WriteDword(HKEY key, const wchar_t* name, DWORD val) {
        RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(DWORD));
    }

    std::wstring ReadString(HKEY key, const wchar_t* name) {
        DWORD type = 0;
        DWORD bytes = 0;
        if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS) {
            return {};
        }
        if (type != REG_SZ || bytes < sizeof(wchar_t)) {
            return {};
        }
        std::wstring buf(bytes / sizeof(wchar_t), L'\0');
        if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &bytes) != ERROR_SUCCESS) {
            return {};
        }
        while (!buf.empty() && buf.back() == L'\0') {
            buf.pop_back();
        }
        return buf;
    }

    void WriteString(HKEY key, const wchar_t* name, const std::wstring& value) {
        DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
        RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    }

    std::wstring GetExeQuotedPath() {
        std::wstring buf(MAX_PATH, L'\0');
        while (buf.size() <= 32768) {
            DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
            if (len == 0) {
                return {};
            }
            if (len < buf.size() - 1) {
                buf.resize(len);
                return std::wstring(L"\"") + buf + L"\"";
            }
            buf.resize(buf.size() * 2);
        }
        return {};
    }
}

namespace SettingsStore {

AppSettings Load() {
    AppSettings s;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD v = 0;
        if (ReadDword(key, L"DisplayStyle", v) && v <= 2) {
            s.displayStyle = static_cast<DisplayStyle>(v);
        } else if (ReadDword(key, L"CompactMode", v) && v != 0) {
            s.displayStyle = DisplayStyle::Compact;
        }
        if (ReadDword(key, L"ShowLaptop", v))       s.showLaptop = (v != 0);
        if (ReadDword(key, L"ShowDisconnected", v)) s.showDisconnected = (v != 0);
        if (ReadDword(key, L"ShowNetwork", v))      s.showNetwork = (v != 0);
        if (ReadDword(key, L"NetUnits", v) && v <= 1) {
            s.netUnits = static_cast<NetUnitMode>(v);
        }
        if (ReadDword(key, L"LowBatteryThreshold", v) && v >= 10 && v <= 35) {
            s.lowBatteryThreshold = static_cast<int>(v);
        }
        if (ReadDword(key, L"PollIntervalSec", v) && v >= 5 && v <= 300) {
            s.pollIntervalSec = static_cast<int>(v);
        }
        std::wstring hidden = ReadString(key, L"HiddenIds");
        std::wstringstream ss(hidden);
        std::wstring item;
        while (std::getline(ss, item, L'|')) {
            if (!item.empty()) {
                s.hiddenIds.push_back(item);
            }
        }
        std::wstring overrides = ReadString(key, L"IconOverrides");
        std::wstringstream oss(overrides);
        std::wstring pairStr;
        while (std::getline(oss, pairStr, L'|')) {
            size_t eq = pairStr.rfind(L'=');
            if (eq != std::wstring::npos && eq > 0 && eq + 1 < pairStr.size()) {
                std::wstring id = pairStr.substr(0, eq);
                int kv = _wtoi(pairStr.substr(eq + 1).c_str());
                if (kv >= 0 && kv <= 6) {
                    s.iconOverrides[id] = static_cast<DeviceKind>(kv);
                }
            }
        }
        RegCloseKey(key);
    }
    s.autoStart = IsAutoStartEnabled();
    return s;
}

void Save(const AppSettings& s) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        WriteDword(key, L"DisplayStyle", static_cast<DWORD>(s.displayStyle));
        WriteDword(key, L"CompactMode", s.displayStyle == DisplayStyle::Compact ? 1 : 0);
        WriteDword(key, L"ShowLaptop", s.showLaptop ? 1 : 0);
        WriteDword(key, L"ShowDisconnected", s.showDisconnected ? 1 : 0);
        WriteDword(key, L"ShowNetwork", s.showNetwork ? 1 : 0);
        WriteDword(key, L"NetUnits", static_cast<DWORD>(s.netUnits));
        WriteDword(key, L"LowBatteryThreshold", static_cast<DWORD>(std::clamp(s.lowBatteryThreshold, 10, 35)));
        WriteDword(key, L"PollIntervalSec", static_cast<DWORD>(std::clamp(s.pollIntervalSec, 5, 300)));

        std::wstring hidden;
        for (size_t i = 0; i < s.hiddenIds.size(); ++i) {
            if (i > 0) hidden += L'|';
            hidden += s.hiddenIds[i];
        }
        WriteString(key, L"HiddenIds", hidden);

        std::wstring overrides;
        bool first = true;
        for (const auto& kv : s.iconOverrides) {
            if (!first) overrides += L'|';
            first = false;
            overrides += kv.first + L"=" + std::to_wstring(static_cast<int>(kv.second));
        }
        WriteString(key, L"IconOverrides", overrides);
        RegCloseKey(key);
    }
}

bool IsAutoStartEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunSubKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    std::wstring val = ReadString(key, kAppName);
    RegCloseKey(key);
    return !val.empty();
}

bool SetAutoStartEnabled(bool enable) {
    HKEY key = nullptr;
    LONG openRc = enable
        ? RegCreateKeyExW(HKEY_CURRENT_USER, kRunSubKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)
        : RegOpenKeyExW(HKEY_CURRENT_USER, kRunSubKey, 0, KEY_SET_VALUE, &key);
    if (openRc != ERROR_SUCCESS) {
        return !enable && openRc == ERROR_FILE_NOT_FOUND;
    }
    bool ok = false;
    if (enable) {
        std::wstring cmd = GetExeQuotedPath();
        if (!cmd.empty()) {
            DWORD bytes = static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t));
            ok = (RegSetValueExW(key, kAppName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), bytes) == ERROR_SUCCESS);
        }
    } else {
        LONG rc = RegDeleteValueW(key, kAppName);
        ok = (rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(key);
    return ok;
}

bool IsDeviceHidden(const AppSettings& s, const std::wstring& id) {
    return std::find(s.hiddenIds.begin(), s.hiddenIds.end(), id) != s.hiddenIds.end();
}

void ToggleDeviceHidden(AppSettings& s, const std::wstring& id) {
    auto it = std::find(s.hiddenIds.begin(), s.hiddenIds.end(), id);
    if (it != s.hiddenIds.end()) {
        s.hiddenIds.erase(it);
    } else {
        s.hiddenIds.push_back(id);
    }
}

std::optional<DeviceKind> GetIconOverride(const AppSettings& s, const std::wstring& id) {
    auto it = s.iconOverrides.find(id);
    if (it == s.iconOverrides.end()) return std::nullopt;
    return it->second;
}

void CycleIconOverride(AppSettings& s, const std::wstring& id) {
    // Cycle: Auto (absent) -> Mouse -> Headphones -> Earbuds -> Keyboard -> Gamepad -> Laptop -> Bluetooth -> Auto
    static constexpr DeviceKind kOrder[] = {
        DeviceKind::Mouse,
        DeviceKind::Headphones,
        DeviceKind::Earbuds,
        DeviceKind::Keyboard,
        DeviceKind::Gamepad,
        DeviceKind::Laptop,
        DeviceKind::Bluetooth
    };
    auto it = s.iconOverrides.find(id);
    if (it == s.iconOverrides.end()) {
        s.iconOverrides[id] = kOrder[0];
        return;
    }
    for (size_t i = 0; i < sizeof(kOrder) / sizeof(kOrder[0]); ++i) {
        if (kOrder[i] == it->second) {
            if (i + 1 < sizeof(kOrder) / sizeof(kOrder[0])) {
                it->second = kOrder[i + 1];
            } else {
                s.iconOverrides.erase(it);
            }
            return;
        }
    }
    s.iconOverrides.erase(it);
}

} // namespace SettingsStore
