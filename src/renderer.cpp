#include "renderer.h"
#include "network_monitor.h"

#include <windows.h>
#include <objidl.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace Gdiplus {
    using std::min;
    using std::max;
}
#include <gdiplus.h>

namespace {

ULONG_PTR g_gdiplusToken = 0;

float Sc(float px, UINT dpi) {
    return px * (static_cast<float>(dpi) / 96.0f);
}

int ScI(float px, UINT dpi) {
    return static_cast<int>(std::lround(Sc(px, dpi)));
}

void AddRoundedRect(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float r) {
    float maxR = std::min(w, h) * 0.5f;
    r = std::min(r, maxR);
    if (r <= 0.1f) {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    float d = r * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void AddRightRoundedRect(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float r) {
    float maxR = std::min(w, h * 0.5f);
    r = std::min(r, maxR);
    if (r <= 0.1f) {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    float d = r * 2.0f;
    path.AddLine(x, y, x + w - r, y);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddLine(x + w - r, y + h, x, y + h);
    path.CloseFigure();
}

bool IsLowBattery(const BatteryDevice& dev, int threshold) {
    return dev.connected && dev.percent >= 0 && dev.percent <= threshold;
}

void GetFillColors(const BatteryDevice& dev, int lowThreshold, Gdiplus::Color& topCol, Gdiplus::Color& botCol) {
    using namespace Gdiplus;
    if (!dev.connected || dev.percent < 0) {
        topCol = Color(190, 96, 103, 115);
        botCol = Color(190, 70, 76, 86);
        return;
    }
    if (dev.percent <= lowThreshold) {
        // Vivid alert crimson red (clear low-battery visual without popups)
        topCol = Color(252, 236, 54, 58);
        botCol = Color(252, 182, 26, 30);
    } else if (dev.percent <= std::max(40, lowThreshold + 15)) {
        // Warm amber/gold (high white-text legibility)
        topCol = Color(248, 224, 148, 16);
        botCol = Color(248, 178, 108, 6);
    } else if (dev.charging) {
        // Vibrant charging emerald
        topCol = Color(248, 34, 186, 120);
        botCol = Color(248, 20, 136, 86);
    } else {
        // Classic BatteryBar rich green
        topCol = Color(248, 36, 170, 94);
        botCol = Color(248, 22, 128, 68);
    }
}

void DrawBatteryCapsuleTile(
    Gdiplus::Graphics& g,
    const BatteryDevice& dev,
    const RECT& tileRect,
    const RenderParams& params,
    bool hovered)
{
    using namespace Gdiplus;

    UINT dpi = params.dpi;
    bool lightTaskbar = params.lightTaskbar;
    bool isLow = IsLowBattery(dev, params.lowBatteryThreshold);

    float nubW = Sc(3.0f, dpi);
    float x = static_cast<float>(tileRect.left);
    float y = static_cast<float>(tileRect.top);
    float bodyW = static_cast<float>(tileRect.right - tileRect.left) - nubW;
    float h = static_cast<float>(tileRect.bottom - tileRect.top);
    float radius = Sc(3.5f, dpi);

    // Subtle hover outer aura
    if (hovered) {
        GraphicsPath glow;
        AddRoundedRect(glow, x - Sc(1.5f, dpi), y - Sc(1.5f, dpi),
            bodyW + Sc(3.0f, dpi), h + Sc(3.0f, dpi), radius + Sc(1.5f, dpi));
        SolidBrush glowBrush(lightTaskbar ? Color(36, 0, 0, 0) : Color(36, 255, 255, 255));
        g.FillPath(&glowBrush, &glow);
    }

    // Battery body interior: dark crimson tint when low so even 0-10% reads clearly as red alert
    GraphicsPath bodyPath;
    AddRoundedRect(bodyPath, x, y, bodyW, h, radius);

    Color bgTop, bgBot;
    if (isLow) {
        bgTop = lightTaskbar ? Color(244, 78, 22, 26) : Color(238, 66, 16, 20);
        bgBot = lightTaskbar ? Color(244, 54, 14, 18) : Color(238, 44, 10, 14);
    } else {
        bgTop = lightTaskbar ? Color(238, 38, 42, 50) : Color(225, 24, 27, 34);
        bgBot = lightTaskbar ? Color(238, 26, 30, 37) : Color(225, 16, 18, 23);
    }
    LinearGradientBrush bgBrush(RectF(x, y, bodyW, h), bgTop, bgBot, LinearGradientModeVertical);
    g.FillPath(&bgBrush, &bodyPath);

    // Proportional battery fill inside the capsule (inset by 2px)
    float inset = std::max(1.5f, Sc(2.0f, dpi));
    float innerX = x + inset;
    float innerY = y + inset;
    float innerW = std::max(1.0f, bodyW - inset * 2.0f);
    float innerH = std::max(1.0f, h - inset * 2.0f);
    float innerR = std::max(1.5f, radius - inset * 0.65f);

    int pct = std::clamp(dev.percent, 0, 100);
    if (dev.percent >= 0 && pct > 0) {
        float fillW = innerW * (static_cast<float>(pct) / 100.0f);
        fillW = std::max(Sc(3.5f, dpi), std::min(innerW, fillW));

        Color topCol, botCol;
        GetFillColors(dev, params.lowBatteryThreshold, topCol, botCol);

        // Clip fill cleanly to rounded inner shell so low percentages keep crisp rounded left corners
        GraphicsState st = g.Save();
        GraphicsPath innerClip;
        AddRoundedRect(innerClip, innerX, innerY, innerW, innerH, innerR);
        g.SetClip(&innerClip);

        LinearGradientBrush fillBrush(RectF(innerX, innerY, fillW, innerH), topCol, botCol, LinearGradientModeVertical);
        g.FillRectangle(&fillBrush, innerX, innerY, fillW, innerH);

        // Subtle top glossy highlight line (signature BatteryBar polish)
        SolidBrush sheen(Color(44, 255, 255, 255));
        g.FillRectangle(&sheen, innerX, innerY, fillW, std::max(1.0f, Sc(1.2f, dpi)));

        // Subtle vertical end-cap separator when partially filled
        if (pct < 99) {
            SolidBrush edge(Color(40, 255, 255, 255));
            g.FillRectangle(&edge, innerX + fillW - 1.0f, innerY, 1.0f, innerH);
        }

        g.Restore(st);
    }

    // Subtle refresh flash highlight
    if (params.refreshFlash) {
        SolidBrush flashBrush(Color(48, 255, 255, 255));
        g.FillPath(&flashBrush, &bodyPath);
    }

    // Outer battery frame border
    Color borderCol;
    if (hovered) {
        borderCol = lightTaskbar ? Color(230, 18, 20, 26) : Color(235, 255, 255, 255);
    } else if (isLow) {
        borderCol = lightTaskbar ? Color(240, 214, 38, 42) : Color(238, 255, 86, 90);
    } else {
        borderCol = lightTaskbar ? Color(185, 36, 40, 48) : Color(120, 255, 255, 255);
    }

    float borderW = std::max(1.0f, Sc(isLow ? 1.25f : 1.1f, dpi));
    Pen borderPen(borderCol, borderW);
    g.DrawPath(&borderPen, &bodyPath);

    // Positive battery terminal nub on right edge
    float nubH = std::max(Sc(8.0f, dpi), h * 0.42f);
    float nubY = y + (h - nubH) * 0.5f;
    GraphicsPath nubPath;
    AddRightRoundedRect(nubPath, x + bodyW + Sc(0.5f, dpi), nubY, nubW - Sc(0.5f, dpi), nubH, Sc(1.3f, dpi));
    Color nubCol = borderCol;
    if (dev.connected && pct >= 98) {
        Color topCol, botCol;
        GetFillColors(dev, params.lowBatteryThreshold, topCol, botCol);
        nubCol = topCol;
    }
    SolidBrush nubBrush(nubCol);
    g.FillPath(&nubBrush, &nubPath);

    // Crisp vector device icon on the left side of the capsule
    float iconSize = std::min(Sc(13.0f, dpi), h * 0.56f);
    float iconX = x + Sc(5.0f, dpi);
    float iconY = y + (h - iconSize) * 0.5f;
    Color fgCol = dev.connected ? Color(255, 255, 255, 255) : Color(185, 210, 215, 224);
    Renderer::DrawDeviceIcon(g, dev.kind, dev.charging, iconX, iconY, iconSize, fgCol);

    // Crisp percentage text on the right side of the capsule
    std::wstring label = (dev.percent >= 0) ? (std::to_wstring(pct) + L"%") : L"--";

    float fontSizePx = std::min(Sc(11.5f, dpi), h * 0.50f);
    FontFamily family(L"Segoe UI");
    Font font(&family, fontSizePx, FontStyleBold, UnitPixel);

    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);

    float textX = iconX + iconSize + Sc(1.5f, dpi);
    float textW = std::max(Sc(18.0f, dpi), (x + bodyW - Sc(3.0f, dpi)) - textX);
    RectF textRect(textX, y + Sc(0.5f, dpi), textW, h);
    RectF shadowRect(textX, y + Sc(1.4f, dpi), textW, h);

    SolidBrush textShadow(Color(165, 0, 0, 0));
    SolidBrush textFg(fgCol);
    g.DrawString(label.c_str(), -1, &font, shadowRect, &fmt, &textShadow);
    g.DrawString(label.c_str(), -1, &font, textRect, &fmt, &textFg);
}

// Very compact 28x26px tile: crisp vector device icon on top + 10%-step (0..100%) mini battery bar underneath
void DrawUltraCompactBatteryTile(
    Gdiplus::Graphics& g,
    const BatteryDevice& dev,
    const RECT& tileRect,
    const RenderParams& params,
    bool hovered)
{
    using namespace Gdiplus;

    UINT dpi = params.dpi;
    bool lightTaskbar = params.lightTaskbar;
    bool isLow = IsLowBattery(dev, params.lowBatteryThreshold);

    float x = static_cast<float>(tileRect.left);
    float y = static_cast<float>(tileRect.top);
    float w = static_cast<float>(tileRect.right - tileRect.left);
    float h = static_cast<float>(tileRect.bottom - tileRect.top);
    float radius = Sc(4.0f, dpi);

    if (hovered) {
        GraphicsPath glow;
        AddRoundedRect(glow, x - Sc(1.2f, dpi), y - Sc(1.2f, dpi),
            w + Sc(2.4f, dpi), h + Sc(2.4f, dpi), radius + Sc(1.2f, dpi));
        SolidBrush glowBrush(lightTaskbar ? Color(36, 0, 0, 0) : Color(38, 255, 255, 255));
        g.FillPath(&glowBrush, &glow);
    }

    // Tile backdrop (vivid dark-crimson alert tint when battery <= lowBatteryThreshold)
    GraphicsPath cardPath;
    AddRoundedRect(cardPath, x, y, w, h, radius);

    Color bgTop, bgBot;
    if (isLow) {
        bgTop = lightTaskbar ? Color(246, 86, 20, 24) : Color(242, 78, 16, 20);
        bgBot = lightTaskbar ? Color(246, 58, 12, 16) : Color(242, 52, 10, 14);
    } else {
        bgTop = lightTaskbar ? Color(238, 38, 42, 50) : Color(225, 24, 27, 34);
        bgBot = lightTaskbar ? Color(238, 26, 30, 37) : Color(225, 16, 18, 23);
    }
    LinearGradientBrush bgBrush(RectF(x, y, w, h), bgTop, bgBot, LinearGradientModeVertical);
    g.FillPath(&bgBrush, &cardPath);

    if (params.refreshFlash) {
        SolidBrush flashBrush(Color(48, 255, 255, 255));
        g.FillPath(&flashBrush, &cardPath);
    }

    // Outer frame border
    Color borderCol;
    if (hovered) {
        borderCol = lightTaskbar ? Color(230, 18, 20, 26) : Color(235, 255, 255, 255);
    } else if (isLow) {
        borderCol = lightTaskbar ? Color(242, 224, 38, 42) : Color(245, 255, 82, 86);
    } else {
        borderCol = lightTaskbar ? Color(180, 36, 40, 48) : Color(110, 255, 255, 255);
    }
    Pen borderPen(borderCol, std::max(1.0f, Sc(isLow ? 1.25f : 1.0f, dpi)));
    g.DrawPath(&borderPen, &cardPath);

    // Top: crisp vector device icon (tinted bright coral-red on low battery for instant glanceability)
    float iconSize = std::min(Sc(13.0f, dpi), h * 0.50f);
    float iconX = x + (w - iconSize) * 0.5f;
    float iconY = y + Sc(2.0f, dpi);
    Color iconCol;
    if (!dev.connected || dev.percent < 0) {
        iconCol = Color(185, 188, 196, 208);
    } else if (isLow) {
        iconCol = Color(255, 255, 128, 130);
    } else {
        iconCol = Color(255, 250, 252, 255);
    }
    Renderer::DrawDeviceIcon(g, dev.kind, dev.charging, iconX, iconY, iconSize, iconCol);

    // Bottom: ultra-crisp 10%-step (0..100%) mini battery bar + micro terminal nub
    float nubW = Sc(1.6f, dpi);
    float barBodyW = std::max(Sc(16.0f, dpi), w - Sc(4.4f, dpi) - nubW);
    float barH = Sc(6.0f, dpi);
    float barX = x + (w - (barBodyW + nubW)) * 0.5f;
    float barY = y + h - barH - Sc(2.3f, dpi);

    // Mini battery outer track
    GraphicsPath barShell;
    AddRoundedRect(barShell, barX, barY, barBodyW, barH, Sc(1.5f, dpi));
    SolidBrush trackBg(isLow ? Color(220, 36, 8, 10) : Color(210, 10, 12, 16));
    g.FillPath(&trackBg, &barShell);

    // Quantize 0..100% into exact 10% steps (0, 10, 20, ..., 100)
    int pct = std::clamp(dev.percent, 0, 100);
    int steps10 = (dev.percent <= 0) ? 0 : std::clamp((pct + 9) / 10, 1, 10);

    float barInset = std::max(0.8f, Sc(1.0f, dpi));
    float innerX = barX + barInset;
    float innerY = barY + barInset;
    float innerW = std::max(1.0f, barBodyW - barInset * 2.0f);
    float innerH = std::max(1.5f, barH - barInset * 2.0f);

    if (dev.percent >= 0 && steps10 > 0) {
        float fillW = std::max(Sc(2.2f, dpi), innerW * (static_cast<float>(steps10) / 10.0f));
        fillW = std::min(innerW, fillW);
        Color topCol, botCol;
        GetFillColors(dev, params.lowBatteryThreshold, topCol, botCol);

        GraphicsState st = g.Save();
        GraphicsPath innerClip;
        AddRoundedRect(innerClip, innerX, innerY, innerW, innerH, Sc(0.8f, dpi));
        g.SetClip(&innerClip);

        LinearGradientBrush fillBrush(RectF(innerX, innerY, fillW, innerH), topCol, botCol, LinearGradientModeVertical);
        g.FillRectangle(&fillBrush, innerX, innerY, fillW, innerH);
        g.Restore(st);
    }

    // Mini battery shell border
    Color barBorderCol = isLow ? Color(235, 255, 104, 108) : Color(145, 215, 222, 234);
    Pen barPen(barBorderCol, std::max(0.85f, Sc(0.9f, dpi)));
    g.DrawPath(&barPen, &barShell);

    // Micro positive battery nub on right of mini bar
    float nubH = std::max(Sc(2.6f, dpi), barH * 0.54f);
    float nubY = barY + (barH - nubH) * 0.5f;
    Color nubCol = barBorderCol;
    if (dev.connected && steps10 >= 10) {
        Color topCol, botCol;
        GetFillColors(dev, params.lowBatteryThreshold, topCol, botCol);
        nubCol = topCol;
    }
    SolidBrush nubBrush(nubCol);
    g.FillRectangle(&nubBrush, barX + barBodyW + Sc(0.4f, dpi), nubY, nubW - Sc(0.3f, dpi), nubH);
}

// Live Network Activity (Download ↓ / Upload ↑) tile
void DrawNetworkTile(
    Gdiplus::Graphics& g,
    const RECT& tileRect,
    const RenderParams& params,
    bool hovered)
{
    using namespace Gdiplus;

    UINT dpi = params.dpi;
    bool lightTaskbar = params.lightTaskbar;
    bool ultraShort = (params.style == DisplayStyle::UltraCompact);

    float x = static_cast<float>(tileRect.left);
    float y = static_cast<float>(tileRect.top);
    float w = static_cast<float>(tileRect.right - tileRect.left);
    float h = static_cast<float>(tileRect.bottom - tileRect.top);
    float radius = Sc(3.8f, dpi);

    if (hovered) {
        GraphicsPath glow;
        AddRoundedRect(glow, x - Sc(1.3f, dpi), y - Sc(1.3f, dpi),
            w + Sc(2.6f, dpi), h + Sc(2.6f, dpi), radius + Sc(1.3f, dpi));
        SolidBrush glowBrush(lightTaskbar ? Color(36, 0, 0, 0) : Color(36, 255, 255, 255));
        g.FillPath(&glowBrush, &glow);
    }

    GraphicsPath bodyPath;
    AddRoundedRect(bodyPath, x, y, w, h, radius);
    Color bgTop = lightTaskbar ? Color(238, 38, 42, 50) : Color(225, 24, 27, 34);
    Color bgBot = lightTaskbar ? Color(238, 26, 30, 37) : Color(225, 16, 18, 23);
    LinearGradientBrush bgBrush(RectF(x, y, w, h), bgTop, bgBot, LinearGradientModeVertical);
    g.FillPath(&bgBrush, &bodyPath);

    if (params.refreshFlash) {
        SolidBrush flashBrush(Color(44, 255, 255, 255));
        g.FillPath(&flashBrush, &bodyPath);
    }

    Color borderCol = hovered
        ? (lightTaskbar ? Color(230, 18, 20, 26) : Color(235, 255, 255, 255))
        : (lightTaskbar ? Color(185, 36, 40, 48) : Color(115, 255, 255, 255));
    Pen borderPen(borderCol, std::max(1.0f, Sc(1.05f, dpi)));
    g.DrawPath(&borderPen, &bodyPath);

    float halfH = h * 0.5f;
    float arrowX = x + Sc(5.0f, dpi);
    float arrowW = Sc(5.0f, dpi);
    float arrowH = Sc(5.2f, dpi);

    // Top row: crisp vector Download arrow (↓) in sky-cyan
    float downCy = y + halfH * 0.54f;
    Color downCol(255, 86, 204, 255);
    {
        float ax = arrowX;
        float topY = downCy - arrowH * 0.5f;
        float midY = downCy + arrowH * 0.08f;
        float botY = downCy + arrowH * 0.52f;
        Pen stemPen(downCol, std::max(1.2f, Sc(1.3f, dpi)));
        stemPen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        g.DrawLine(&stemPen, ax + arrowW * 0.5f, topY, ax + arrowW * 0.5f, midY);
        PointF head[3] = {
            { ax, midY - Sc(0.2f, dpi) },
            { ax + arrowW, midY - Sc(0.2f, dpi) },
            { ax + arrowW * 0.5f, botY }
        };
        SolidBrush headBrush(downCol);
        g.FillPolygon(&headBrush, head, 3);
    }

    // Bottom row: crisp vector Upload arrow (↑) in mint-emerald
    float upCy = y + halfH + halfH * 0.46f;
    Color upCol(255, 84, 224, 146);
    {
        float ax = arrowX;
        float topY = upCy - arrowH * 0.52f;
        float midY = upCy - arrowH * 0.08f;
        float botY = upCy + arrowH * 0.50f;
        Pen stemPen(upCol, std::max(1.2f, Sc(1.3f, dpi)));
        stemPen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        g.DrawLine(&stemPen, ax + arrowW * 0.5f, midY, ax + arrowW * 0.5f, botY);
        PointF head[3] = {
            { ax, midY + Sc(0.2f, dpi) },
            { ax + arrowW, midY + Sc(0.2f, dpi) },
            { ax + arrowW * 0.5f, topY }
        };
        SolidBrush headBrush(upCol);
        g.FillPolygon(&headBrush, head, 3);
    }

    std::wstring downStr = NetworkMonitor::FormatSpeed(params.netStats.downBytesPerSec, params.netUnits, ultraShort);
    std::wstring upStr   = NetworkMonitor::FormatSpeed(params.netStats.upBytesPerSec, params.netUnits, ultraShort);

    float fontSize = std::min(Sc(ultraShort ? 9.6f : 9.8f, dpi), halfH * 0.86f);
    FontFamily fam(L"Segoe UI");
    Font font(&fam, fontSize, FontStyleBold, UnitPixel);

    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentFar);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);

    float textX = arrowX + arrowW + Sc(1.5f, dpi);
    float textW = std::max(Sc(24.0f, dpi), (x + w - Sc(4.0f, dpi)) - textX);
    RectF downRect(textX, y + Sc(0.5f, dpi), textW, halfH);
    RectF upRect(textX, y + halfH - Sc(0.5f, dpi), textW, halfH);

    SolidBrush downText(Color(255, 238, 248, 255));
    SolidBrush upText(Color(255, 224, 248, 232));
    g.DrawString(downStr.c_str(), -1, &font, downRect, &fmt, &downText);
    g.DrawString(upStr.c_str(), -1, &font, upRect, &fmt, &upText);
}

void DrawAllTiles(
    Gdiplus::Graphics& g,
    const std::vector<BatteryDevice>& devices,
    const WidgetLayout& layout,
    const RenderParams& params)
{
    for (size_t i = 0; i < layout.tiles.size(); ++i) {
        const auto& box = layout.tiles[i];
        bool isHovered = (params.hoveredTileIndex == static_cast<int>(i));

        if (box.isNetworkTile) {
            DrawNetworkTile(g, box.rect, params, isHovered);
            continue;
        }

        BatteryDevice placeholder;
        placeholder.kind = DeviceKind::Bluetooth;
        placeholder.percent = -1;
        placeholder.connected = false;

        const BatteryDevice& dev = (box.deviceIndex >= 0 && static_cast<size_t>(box.deviceIndex) < devices.size())
            ? devices[static_cast<size_t>(box.deviceIndex)]
            : placeholder;

        if (params.style == DisplayStyle::UltraCompact) {
            DrawUltraCompactBatteryTile(g, dev, box.rect, params, isHovered);
        } else {
            DrawBatteryCapsuleTile(g, dev, box.rect, params, isHovered);
        }
    }
}

} // namespace

namespace Renderer {

void InitGdiPlus() {
    if (g_gdiplusToken == 0) {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr);
    }
}

void ShutdownGdiPlus() {
    if (g_gdiplusToken != 0) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

void DrawDeviceIcon(
    Gdiplus::Graphics& g,
    DeviceKind kind,
    bool charging,
    float x,
    float y,
    float s,
    const Gdiplus::Color& color)
{
    using namespace Gdiplus;

    float stroke = std::max(1.15f, s * 0.105f);
    Pen pen(color, stroke);
    pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    pen.SetLineJoin(LineJoinRound);
    SolidBrush brush(color);

    // Subtle dark shadow behind icon for crisp readability over any fill edge
    Color shadowCol(135, 0, 0, 0);
    Pen shadowPen(shadowCol, stroke + 0.6f);
    shadowPen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    shadowPen.SetLineJoin(LineJoinRound);
    SolidBrush shadowBrush(shadowCol);

    auto drawIconPass = [&](Pen& p, Brush& b, Color col, float ox, float oy) {
        float ix = x + ox;
        float iy = y + oy;

        switch (kind) {
        case DeviceKind::Laptop: {
            // Laptop screen rectangle
            GraphicsPath scr;
            AddRoundedRect(scr, ix + s * 0.13f, iy + s * 0.13f, s * 0.74f, s * 0.55f, s * 0.08f);
            g.DrawPath(&p, &scr);
            // Laptop keyboard base
            Pen basePen(col, std::max(1.5f, stroke * 1.35f));
            basePen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            g.DrawLine(&basePen, ix + s * 0.04f, iy + s * 0.83f, ix + s * 0.96f, iy + s * 0.83f);
            break;
        }
        case DeviceKind::Mouse: {
            // Mouse vertical rounded shell
            GraphicsPath body;
            AddRoundedRect(body, ix + s * 0.22f, iy + s * 0.08f, s * 0.56f, s * 0.84f, s * 0.26f);
            g.DrawPath(&p, &body);
            // Center scroll wheel tick
            g.DrawLine(&p, ix + s * 0.50f, iy + s * 0.20f, ix + s * 0.50f, iy + s * 0.40f);
            // Subtle button divider
            Pen thinPen(col, std::max(0.85f, stroke * 0.72f));
            g.DrawLine(&thinPen, ix + s * 0.25f, iy + s * 0.43f, ix + s * 0.75f, iy + s * 0.43f);
            break;
        }
        case DeviceKind::Headphones: {
            // Curved headband arc
            g.DrawArc(&p, ix + s * 0.14f, iy + s * 0.12f, s * 0.72f, s * 0.68f, 180.0f, 180.0f);
            // Left earcup
            GraphicsPath leftCup;
            AddRoundedRect(leftCup, ix + s * 0.07f, iy + s * 0.48f, s * 0.23f, s * 0.40f, s * 0.10f);
            g.FillPath(&b, &leftCup);
            // Right earcup
            GraphicsPath rightCup;
            AddRoundedRect(rightCup, ix + s * 0.70f, iy + s * 0.48f, s * 0.23f, s * 0.40f, s * 0.10f);
            g.FillPath(&b, &rightCup);
            break;
        }
        case DeviceKind::Earbuds: {
            // Left earbud head + stem
            g.FillEllipse(&b, ix + s * 0.10f, iy + s * 0.18f, s * 0.32f, s * 0.30f);
            Pen stemPen(col, std::max(1.4f, stroke * 1.25f));
            stemPen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            g.DrawLine(&stemPen, ix + s * 0.32f, iy + s * 0.44f, ix + s * 0.24f, iy + s * 0.84f);
            // Right earbud head + stem
            g.FillEllipse(&b, ix + s * 0.58f, iy + s * 0.18f, s * 0.32f, s * 0.30f);
            g.DrawLine(&stemPen, ix + s * 0.68f, iy + s * 0.44f, ix + s * 0.76f, iy + s * 0.84f);
            break;
        }
        case DeviceKind::Keyboard: {
            GraphicsPath kb;
            AddRoundedRect(kb, ix + s * 0.05f, iy + s * 0.24f, s * 0.90f, s * 0.52f, s * 0.09f);
            g.DrawPath(&p, &kb);
            float kw = s * 0.10f;
            float kh = s * 0.09f;
            for (int k = 0; k < 4; ++k) {
                g.FillRectangle(&b, ix + s * (0.16f + k * 0.18f), iy + s * 0.36f, kw, kh);
            }
            // Spacebar
            g.FillRectangle(&b, ix + s * 0.28f, iy + s * 0.56f, s * 0.44f, kh);
            break;
        }
        case DeviceKind::Gamepad: {
            GraphicsPath pad;
            AddRoundedRect(pad, ix + s * 0.06f, iy + s * 0.24f, s * 0.88f, s * 0.50f, s * 0.22f);
            g.DrawPath(&p, &pad);
            // D-pad
            g.DrawLine(&p, ix + s * 0.28f, iy + s * 0.40f, ix + s * 0.28f, iy + s * 0.58f);
            g.DrawLine(&p, ix + s * 0.19f, iy + s * 0.49f, ix + s * 0.37f, iy + s * 0.49f);
            // Action dots
            float dot = std::max(1.5f, s * 0.13f);
            g.FillEllipse(&b, ix + s * 0.62f, iy + s * 0.39f, dot, dot);
            g.FillEllipse(&b, ix + s * 0.72f, iy + s * 0.49f, dot, dot);
            break;
        }
        case DeviceKind::Bluetooth:
        default: {
            PointF pts[6] = {
                { ix + s * 0.28f, iy + s * 0.32f },
                { ix + s * 0.72f, iy + s * 0.68f },
                { ix + s * 0.50f, iy + s * 0.88f },
                { ix + s * 0.50f, iy + s * 0.12f },
                { ix + s * 0.72f, iy + s * 0.32f },
                { ix + s * 0.28f, iy + s * 0.68f }
            };
            g.DrawLines(&p, pts, 6);
            break;
        }
        }
    };

    bool brightIcon = (static_cast<int>(color.GetR()) + static_cast<int>(color.GetG()) + static_cast<int>(color.GetB())) >= 384;
    if (brightIcon) {
        drawIconPass(shadowPen, shadowBrush, shadowCol, 0.0f, 0.75f);
    }
    drawIconPass(pen, brush, color, 0.0f, 0.0f);

    // Crisp bright charging lightning bolt inside laptop screen when charging
    if (charging && kind == DeviceKind::Laptop) {
        PointF bolt[6] = {
            { x + s * 0.57f, y + s * 0.18f },
            { x + s * 0.34f, y + s * 0.44f },
            { x + s * 0.49f, y + s * 0.44f },
            { x + s * 0.43f, y + s * 0.64f },
            { x + s * 0.67f, y + s * 0.38f },
            { x + s * 0.52f, y + s * 0.38f }
        };
        SolidBrush boltBg(Color(180, 0, 0, 0));
        PointF boltShadow[6];
        for (int i = 0; i < 6; ++i) {
            boltShadow[i] = PointF(bolt[i].X, bolt[i].Y + 0.6f);
        }
        g.FillPolygon(&boltBg, boltShadow, 6);
        SolidBrush boltFill(Color(255, 255, 236, 92));
        g.FillPolygon(&boltFill, bolt, 6);
    }
}

WidgetLayout ComputeLayout(
    const std::vector<BatteryDevice>& devices,
    UINT dpi,
    int taskbarThickness,
    bool horizontal,
    DisplayStyle style,
    bool showNetwork)
{
    WidgetLayout layout;
    int devCount = static_cast<int>(devices.size());
    bool emptyPlaceholder = (devCount == 0 && !showNetwork);
    int batteryTileCount = emptyPlaceholder ? 1 : devCount;

    float baseTileW = 72.0f;
    float baseTileH = 24.0f;
    float gap = 6.0f;
    float netW = 74.0f;

    float padX = 4.0f;
    float padY = 2.0f;

    if (style == DisplayStyle::Compact) {
        baseTileW = 64.0f;
        baseTileH = 22.0f;
        gap = 5.0f;
        netW = 68.0f;
    } else if (style == DisplayStyle::UltraCompact) {
        baseTileW = 25.0f;
        baseTileH = 26.0f;
        gap = 3.0f;
        padX = 3.0f;
        netW = 52.0f;
    }

    int tileW = ScI(baseTileW, dpi);
    int maxTileH = std::max(ScI(18.0f, dpi), taskbarThickness - ScI(4.0f, dpi));
    int tileH = std::min(ScI(baseTileH, dpi), maxTileH);
    int gapPx = ScI(gap, dpi);
    int padXPx = ScI(padX, dpi);
    int padYPx = ScI(padY, dpi);
    int netWPx = ScI(netW, dpi);

    if (emptyPlaceholder && style != DisplayStyle::UltraCompact) {
        tileW = ScI(58.0f, dpi);
    }

    if (horizontal) {
        layout.height = std::min(taskbarThickness, tileH + padYPx * 2);
        int topY = (layout.height - tileH) / 2;
        int curX = padXPx;

        if (showNetwork) {
            TileHitBox netBox;
            netBox.rect = { curX, topY, curX + netWPx, topY + tileH };
            netBox.deviceIndex = -1;
            netBox.isNetworkTile = true;
            layout.tiles.push_back(netBox);
            curX += netWPx;
            if (batteryTileCount > 0) {
                curX += gapPx;
            }
        }

        for (int i = 0; i < batteryTileCount; ++i) {
            TileHitBox box;
            box.rect = { curX, topY, curX + tileW, topY + tileH };
            box.deviceIndex = emptyPlaceholder ? -1 : i;
            box.isNetworkTile = false;
            layout.tiles.push_back(box);
            curX += tileW;
            if (i + 1 < batteryTileCount) {
                curX += gapPx;
            }
        }

        layout.width = curX + padXPx;
    } else {
        // Vertical taskbar (docked on left/right of screen)
        int maxW = std::max(ScI(54.0f, dpi), taskbarThickness - ScI(8.0f, dpi));
        int vTileW = std::min(tileW, maxW);
        int vNetW  = std::min(netWPx, maxW);
        int maxItemW = showNetwork ? std::max(vTileW, vNetW) : vTileW;
        layout.width = std::min(taskbarThickness, maxItemW + padXPx * 2);

        int curY = padYPx;
        if (showNetwork) {
            int leftX = (layout.width - vNetW) / 2;
            TileHitBox netBox;
            netBox.rect = { leftX, curY, leftX + vNetW, curY + tileH };
            netBox.deviceIndex = -1;
            netBox.isNetworkTile = true;
            layout.tiles.push_back(netBox);
            curY += tileH;
            if (batteryTileCount > 0) curY += gapPx;
        }

        int leftX = (layout.width - vTileW) / 2;
        for (int i = 0; i < batteryTileCount; ++i) {
            TileHitBox box;
            box.rect = { leftX, curY, leftX + vTileW, curY + tileH };
            box.deviceIndex = emptyPlaceholder ? -1 : i;
            box.isNetworkTile = false;
            layout.tiles.push_back(box);
            curY += tileH;
            if (i + 1 < batteryTileCount) curY += gapPx;
        }

        layout.height = curY + padYPx;
    }

    return layout;
}

bool RenderToLayeredWindow(
    HWND hwnd,
    const std::vector<BatteryDevice>& devices,
    const WidgetLayout& layout,
    const RenderParams& params,
    const POINT* ptDst)
{
    if (!hwnd || !IsWindow(hwnd) || layout.width <= 0 || layout.height <= 0) {
        return false;
    }

    HDC screenDC = GetDC(nullptr);
    if (!screenDC) return false;

    HDC memDC = CreateCompatibleDC(screenDC);
    if (!memDC) {
        ReleaseDC(nullptr, screenDC);
        return false;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = layout.width;
    bmi.bmiHeader.biHeight = -layout.height; // top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        return false;
    }

    HGDIOBJ oldBmp = SelectObject(memDC, dib);

    {
        Gdiplus::Bitmap bmp(layout.width, layout.height, layout.width * 4,
            PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

        // Imperceptible alpha=1 backdrop so clicks in small gaps between tiles still open our menu
        g.Clear(Gdiplus::Color(1, 0, 0, 0));

        DrawAllTiles(g, devices, layout, params);
    }

    POINT ptSrc = { 0, 0 };
    SIZE sz = { layout.width, layout.height };
    BLENDFUNCTION bf = {};
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;

    POINT dstCopy = {};
    POINT* dstPtr = nullptr;
    if (ptDst) {
        dstCopy = *ptDst;
        dstPtr = &dstCopy;
    }

    BOOL ok = UpdateLayeredWindow(hwnd, screenDC, dstPtr, &sz, memDC, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(memDC, oldBmp);
    DeleteObject(dib);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
    return ok != FALSE;
}

bool RenderToPaintDC(
    HDC targetDC,
    const std::vector<BatteryDevice>& devices,
    const WidgetLayout& layout,
    const RenderParams& params)
{
    if (!targetDC || layout.width <= 0 || layout.height <= 0) {
        return false;
    }

    HDC memDC = CreateCompatibleDC(targetDC);
    if (!memDC) return false;

    HBITMAP hBmp = CreateCompatibleBitmap(targetDC, layout.width, layout.height);
    if (!hBmp) {
        DeleteDC(memDC);
        return false;
    }
    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);

    {
        Gdiplus::Graphics g(memDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

        // Fill with exact color-key so transparent regions show the real Win10 taskbar seamlessly
        g.Clear(Gdiplus::Color(255,
            static_cast<BYTE>(kColorKeyRgb & 0xFFu),
            static_cast<BYTE>((kColorKeyRgb >> 8) & 0xFFu),
            static_cast<BYTE>((kColorKeyRgb >> 16) & 0xFFu)));

        DrawAllTiles(g, devices, layout, params);
    }

    BOOL ok = BitBlt(targetDC, 0, 0, layout.width, layout.height, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(memDC);
    return ok != FALSE;
}

} // namespace Renderer
