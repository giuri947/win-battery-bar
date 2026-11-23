#pragma once

#include "battery_model.h"
#include <windows.h>
#include <vector>

struct TileHitBox {
    RECT rect = {};
    int deviceIndex = -1;
};

struct WidgetLayout {
    int width = 80;
    int height = 28;
    std::vector<TileHitBox> tiles;
};

namespace Renderer {
    void InitGdiPlus();
    void ShutdownGdiPlus();

    WidgetLayout ComputeLayout(
        const std::vector<BatteryDevice>& devices,
        UINT dpi,
        int taskbarThickness,
        bool horizontal,
        bool compactMode);

    constexpr COLORREF kColorKeyRgb = RGB(1, 2, 3);

    // Renders crisp 32-bit premultiplied ARGB bitmap and commits directly via UpdateLayeredWindow.
    // If ptDst != nullptr, also updates window position atomically.
    bool RenderToLayeredWindow(
        HWND hwnd,
        const std::vector<BatteryDevice>& devices,
        const WidgetLayout& layout,
        UINT dpi,
        bool lightTaskbar,
        int hoveredIndex,
        bool refreshFlash,
        const POINT* ptDst = nullptr);

    // Fallback renderer using SetLayeredWindowAttributes(LWA_COLORKEY) + WM_PAINT if ULW rejects WS_CHILD.
    bool RenderToPaintDC(
        HDC targetDC,
        const std::vector<BatteryDevice>& devices,
        const WidgetLayout& layout,
        UINT dpi,
        bool lightTaskbar,
        int hoveredIndex,
        bool refreshFlash);
}
