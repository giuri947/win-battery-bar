#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

enum class DeviceKind : int {
    Laptop = 0,
    Mouse = 1,
    Headphones = 2,
    Earbuds = 3,
    Keyboard = 4,
    Gamepad = 5,
    Bluetooth = 6
};

enum class DisplayStyle : int {
    Full = 0,         // Classic 72x24 BatteryBar capsules (icon + %)
    Compact = 1,      // Tighter 64x22 BatteryBar capsules
    UltraCompact = 2  // Very compact 25x26 icon + 10%-step mini battery bar
};

enum class NetUnitMode : int {
    BytesPerSec = 0,  // B/s, KB/s, MB/s, GB/s
    BitsPerSec = 1    // bps, Kbps, Mbps, Gbps
};

struct BatteryDevice {
    std::wstring id;            // Stable key: "laptop", "bt:aabbccddeeff", "ble:aabbccddeeff"
    std::wstring name;          // Clean display name, e.g. "MX Master 3", "WH-1000XM4", "Laptop"
    std::wstring sourceLabel;   // Tooltip detail: "Bluetooth LE", "Bluetooth", "AC Power", etc.
    std::wstring extraStatus;   // e.g. "Charging", "3h 42m remaining", "Plugged in"
    DeviceKind kind = DeviceKind::Bluetooth;
    int percent = -1;           // 0..100, or -1 if unknown
    bool connected = true;
    bool charging = false;
    bool pluggedIn = false;
    uint64_t updatedTickMs = 0;
};

struct NetworkStats {
    bool available = false;
    double downBytesPerSec = 0.0;
    double upBytesPerSec = 0.0;
    uint64_t totalDownBytes = 0;
    uint64_t totalUpBytes = 0;
    std::wstring adapterSummary;
    uint64_t updatedTickMs = 0;
};

struct AppSettings {
    DisplayStyle displayStyle = DisplayStyle::Full;
    bool showLaptop = true;
    bool showDisconnected = false;
    bool showNetwork = false;
    NetUnitMode netUnits = NetUnitMode::BytesPerSec;
    bool autoStart = false;
    int lowBatteryThreshold = 20;  // 10..35: turns capsule/icon vivid red (no notifications)
    int pollIntervalSec = 30;
    std::vector<std::wstring> hiddenIds;             // Optional per-device hide list
    std::map<std::wstring, DeviceKind> iconOverrides; // Optional manual per-device icon override
};

inline int DeviceSortOrder(DeviceKind kind) {
    switch (kind) {
        case DeviceKind::Laptop:     return 0;
        case DeviceKind::Mouse:      return 1;
        case DeviceKind::Headphones: return 2;
        case DeviceKind::Earbuds:    return 3;
        case DeviceKind::Keyboard:   return 4;
        case DeviceKind::Gamepad:    return 5;
        case DeviceKind::Bluetooth:  return 6;
    }
    return 7;
}

inline const wchar_t* DeviceKindLabel(DeviceKind kind) {
    switch (kind) {
        case DeviceKind::Laptop:     return L"Laptop";
        case DeviceKind::Mouse:      return L"Mouse";
        case DeviceKind::Headphones: return L"Headphones";
        case DeviceKind::Earbuds:    return L"Earbuds";
        case DeviceKind::Keyboard:   return L"Keyboard";
        case DeviceKind::Gamepad:    return L"Controller";
        case DeviceKind::Bluetooth:  return L"Bluetooth";
    }
    return L"Device";
}

inline const wchar_t* DisplayStyleLabel(DisplayStyle style) {
    switch (style) {
        case DisplayStyle::Full:         return L"Full";
        case DisplayStyle::Compact:      return L"Compact";
        case DisplayStyle::UltraCompact: return L"Ultra-Compact";
    }
    return L"Full";
}
