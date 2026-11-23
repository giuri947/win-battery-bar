#pragma once

#include "battery_model.h"
#include <windows.h>
#include <vector>

namespace Gdiplus {
    class Graphics;
    class Color;
}

struct TileHitBox {
    RECT rect = {};
    int deviceIndex = -1;       // >= 0 for battery device, -1 for empty placeholder or network tile
    bool isNetworkTile = false; // true for live network activity (up/down) tile
};

struct WidgetLayout {
    int width = 80;
    int height = 28;
    std::vector<TileHitBox> tiles;
};

struct RenderParams {
    DisplayStyle style = DisplayStyle::Full;
    int lowBatteryThreshold = 20;
    bool showNetwork = false;
    NetUnitMode netUnits = NetUnitMode::BytesPerSec;
    NetworkStats netStats;
    UINT dpi = 96;
    bool lightTaskbar = false;
    int hoveredTileIndex = -1; // Index into layout.tiles
    bool refreshFlash = false;
};

namespace Renderer {
    void InitGdiPlus();
    void ShutdownGdiPlus();

    // Shared crisp vector icon renderer (used by both taskbar widget and Settings Panel)
    void DrawDeviceIcon(
        Gdiplus::Graphics& g,
        DeviceKind kind,
        bool charging,
        float x,
        float y,
        float size,
        const Gdiplus::Color& color);

    WidgetLayout ComputeLayout(
        const std::vector<BatteryDevice>& devices,
        UINT dpi,
        int taskbarThickness,
        bool horizontal,
        DisplayStyle style,
        bool showNetwork);

    constexpr COLORREF kColorKeyRgb = RGB(1, 2, 3);

    // Renders crisp 32-bit premultiplied ARGB bitmap and commits directly via UpdateLayeredWindow.
    // If ptDst != nullptr, also updates window position atomically.
    bool RenderToLayeredWindow(
        HWND hwnd,
        const std::vector<BatteryDevice>& devices,
        const WidgetLayout& layout,
        const RenderParams& params,
        const POINT* ptDst = nullptr);

    // Fallback renderer using SetLayeredWindowAttributes(LWA_COLORKEY) + WM_PAINT if ULW rejects WS_CHILD.
    bool RenderToPaintDC(
        HDC targetDC,
        const std::vector<BatteryDevice>& devices,
        const WidgetLayout& layout,
        const RenderParams& params);
}
