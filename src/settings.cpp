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
        wchar_t path[MAX_PATH] = {};
        DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            return {};
        }
        return std::wstring(L"\"") + path + L"\"";
    }
}

namespace SettingsStore {

AppSettings Load() {
    AppSettings s;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD v = 0;
        if (ReadDword(key, L"ShowLaptop", v))       s.showLaptop = (v != 0);
        if (ReadDword(key, L"ShowDisconnected", v)) s.showDisconnected = (v != 0);
        if (ReadDword(key, L"CompactMode", v))      s.compactMode = (v != 0);
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
        RegCloseKey(key);
    }
    s.autoStart = IsAutoStartEnabled();
    return s;
}

void Save(const AppSettings& s) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        WriteDword(key, L"ShowLaptop", s.showLaptop ? 1 : 0);
        WriteDword(key, L"ShowDisconnected", s.showDisconnected ? 1 : 0);
        WriteDword(key, L"CompactMode", s.compactMode ? 1 : 0);
        WriteDword(key, L"PollIntervalSec", static_cast<DWORD>(s.pollIntervalSec));

        std::wstring hidden;
        for (size_t i = 0; i < s.hiddenIds.size(); ++i) {
            if (i > 0) hidden += L'|';
            hidden += s.hiddenIds[i];
        }
        WriteString(key, L"HiddenIds", hidden);
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
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunSubKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return false;
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

} // namespace SettingsStore
