#pragma once

#include "battery_model.h"
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

struct SettingsPanelCallbacks {
    std::function<void(const AppSettings&)> onSettingsChanged;
    std::function<void(bool)> onToggleAutoStart;
    std::function<void()> onRefreshNow;
    std::function<void()> onCopyDiagnostics;
    std::function<void()> onExitApp;
};

class SettingsWindow {
public:
    SettingsWindow() = default;
    ~SettingsWindow();

    bool Create(HINSTANCE hInst);
    void Destroy();

    void Show(
        const AppSettings& settings,
        const std::vector<BatteryDevice>& allDevices,
        const RECT& anchorScreenRect,
        UINT dpi,
        bool lightTheme,
        bool demoMode,
        bool keepOpenForDemo,
        SettingsPanelCallbacks callbacks);

    void UpdateState(
        const AppSettings& settings,
        const std::vector<BatteryDevice>& allDevices,
        UINT dpi,
        bool lightTheme);

    void Hide();
    bool IsVisible() const { return m_visible; }
    HWND Handle() const { return m_hwnd; }

private:
    enum class HitAction {
        None = 0,
        CloseBtn,
        StyleFull,
        StyleCompact,
        StyleUltraCompact,
        ToggleLaptop,
        ToggleDisconnected,
        ToggleNetwork,
        NetUnitBytes,
        NetUnitBits,
        LowThresh15,
        LowThresh20,
        LowThresh25,
        LowThresh30,
        Poll15,
        Poll30,
        Poll60,
        ToggleDeviceVis,
        CycleDeviceIcon,
        ToggleAutoStart,
        BtnRefresh,
        BtnDiagnostics,
        BtnExit
    };

    struct HitZone {
        RECT rect = {};
        HitAction action = HitAction::None;
        int param = -1; // Device index for device actions
    };

    void Render(bool reposition);
    int HitTest(int x, int y) const;
    void HandleClick(const HitZone& zone);

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND m_hwnd = nullptr;
    bool m_visible = false;
    bool m_lightTheme = false;
    bool m_demoMode = false;
    bool m_keepOpenForDemo = false;
    bool m_trackingMouse = false;
    UINT m_dpi = 96;
    RECT m_anchorRect = {};
    int m_hoveredZone = -1;
    int m_lastRenderedH = 0;

    AppSettings m_settings;
    std::vector<BatteryDevice> m_devices;
    std::vector<HitZone> m_zones;
    SettingsPanelCallbacks m_cb;
};
