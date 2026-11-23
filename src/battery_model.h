#pragma once

#include <cstdint>
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

struct AppSettings {
    bool showLaptop = true;
    bool showDisconnected = false;
    bool autoStart = false;
    bool compactMode = false;   // Compact slightly tighter capsules if user prefers
    int pollIntervalSec = 30;
    std::vector<std::wstring> hiddenIds; // Optional per-device hide list
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
