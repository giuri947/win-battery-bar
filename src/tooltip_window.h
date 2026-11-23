#pragma once

#include "battery_model.h"
#include <windows.h>

class TooltipWindow {
public:
    TooltipWindow() = default;
    ~TooltipWindow();

    bool Create(HINSTANCE hInst);
    void Destroy();

    void ShowForDevice(
        const BatteryDevice& dev,
        const RECT& anchorScreenRect,
        UINT dpi,
        bool lightTheme,
        bool horizontal = true);

    void ShowEmpty(
        const RECT& anchorScreenRect,
        UINT dpi,
        bool lightTheme,
        bool horizontal = true);

    void Hide();
    bool IsVisible() const { return m_visible; }

private:
    void RenderAndPosition(
        const std::wstring& title,
        const std::wstring& pctText,
        const std::wstring& subtitle,
        const std::wstring& footer,
        int percent,
        bool connected,
        bool charging,
        const RECT& anchorScreenRect,
        UINT dpi,
        bool lightTheme,
        bool horizontal);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND m_hwnd = nullptr;
    bool m_visible = false;
};
