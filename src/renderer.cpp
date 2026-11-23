#include "renderer.h"

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

void DrawDeviceIcon(Gdiplus::Graphics& g, DeviceKind kind, bool charging, float x, float y, float s, Gdiplus::Color color) {
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
            AddRoundedRect(scr, ix + s * 0.14f, iy + s * 0.16f, s * 0.72f, s * 0.50f, s * 0.08f);
            g.DrawPath(&p, &scr);
            // Laptop keyboard base
            Pen basePen(col, std::max(1.5f, stroke * 1.35f));
            basePen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            g.DrawLine(&basePen, ix + s * 0.04f, iy + s * 0.80f, ix + s * 0.96f, iy + s * 0.80f);
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

    drawIconPass(shadowPen, shadowBrush, shadowCol, 0.0f, 0.75f);
    drawIconPass(pen, brush, color, 0.0f, 0.0f);

    // Crisp bright charging lightning bolt inside laptop screen when charging
    if (charging && kind == DeviceKind::Laptop) {
        PointF bolt[6] = {
            { x + s * 0.55f, y + s * 0.21f },
            { x + s * 0.37f, y + s * 0.43f },
            { x + s * 0.50f, y + s * 0.43f },
            { x + s * 0.45f, y + s * 0.62f },
            { x + s * 0.65f, y + s * 0.39f },
            { x + s * 0.52f, y + s * 0.39f }
        };
        SolidBrush boltBg(Color(160, 0, 0, 0));
        PointF boltShadow[6];
        for (int i = 0; i < 6; ++i) {
            boltShadow[i] = PointF(bolt[i].X, bolt[i].Y + 0.6f);
        }
        g.FillPolygon(&boltBg, boltShadow, 6);
        SolidBrush boltFill(Color(255, 255, 232, 102));
        g.FillPolygon(&boltFill, bolt, 6);
    }
}

void GetFillColors(const BatteryDevice& dev, Gdiplus::Color& topCol, Gdiplus::Color& botCol) {
    using namespace Gdiplus;
    if (!dev.connected || dev.percent < 0) {
        topCol = Color(190, 96, 103, 115);
        botCol = Color(190, 70, 76, 86);
        return;
    }
    if (dev.percent <= 20) {
        // Alert red
        topCol = Color(248, 224, 58, 62);
        botCol = Color(248, 174, 30, 34);
    } else if (dev.percent <= 40) {
        // Warm amber/gold (high white-text legibility)
        topCol = Color(248, 222, 146, 14);
        botCol = Color(248, 176, 106, 4);
    } else if (dev.charging) {
        // Vibrant charging emerald
        topCol = Color(248, 34, 182, 118);
        botCol = Color(248, 20, 134, 84);
    } else {
        // Classic BatteryBar rich green
        topCol = Color(248, 36, 168, 92);
        botCol = Color(248, 22, 126, 66);
    }
}

void DrawBatteryTile(
    Gdiplus::Graphics& g,
    const BatteryDevice& dev,
    const RECT& tileRect,
    UINT dpi,
    bool lightTaskbar,
    bool hovered,
    bool refreshFlash)
{
    using namespace Gdiplus;

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

    // Battery body dark interior (ensures crisp BatteryBar look on both dark and light Win10 taskbars)
    GraphicsPath bodyPath;
    AddRoundedRect(bodyPath, x, y, bodyW, h, radius);

    Color bgTop = lightTaskbar ? Color(238, 38, 42, 50) : Color(225, 24, 27, 34);
    Color bgBot = lightTaskbar ? Color(238, 26, 30, 37) : Color(225, 16, 18, 23);
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
        GetFillColors(dev, topCol, botCol);

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
    if (refreshFlash) {
        SolidBrush flashBrush(Color(48, 255, 255, 255));
        g.FillPath(&flashBrush, &bodyPath);
    }

    // Outer battery frame border
    bool isLow = dev.connected && dev.percent >= 0 && dev.percent <= 20;
    Color borderCol;
    if (hovered) {
        borderCol = lightTaskbar ? Color(230, 18, 20, 26) : Color(235, 255, 255, 255);
    } else if (isLow) {
        borderCol = Color(210, 255, 115, 115);
    } else {
        borderCol = lightTaskbar ? Color(185, 36, 40, 48) : Color(120, 255, 255, 255);
    }

    float borderW = std::max(1.0f, Sc(1.1f, dpi));
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
        GetFillColors(dev, topCol, botCol);
        nubCol = topCol;
    }
    SolidBrush nubBrush(nubCol);
    g.FillPath(&nubBrush, &nubPath);

    // Crisp vector device icon on the left side of the capsule
    float iconSize = std::min(Sc(13.0f, dpi), h * 0.56f);
    float iconX = x + Sc(5.0f, dpi);
    float iconY = y + (h - iconSize) * 0.5f;
    Color fgCol = dev.connected ? Color(255, 255, 255, 255) : Color(185, 210, 215, 224);
    DrawDeviceIcon(g, dev.kind, dev.charging, iconX, iconY, iconSize, fgCol);

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

WidgetLayout ComputeLayout(
    const std::vector<BatteryDevice>& devices,
    UINT dpi,
    int taskbarThickness,
    bool horizontal,
    bool compactMode)
{
    WidgetLayout layout;
    int count = static_cast<int>(devices.size());
    bool emptyPlaceholder = (count == 0);
    if (emptyPlaceholder) count = 1;

    float baseTileW = compactMode ? 64.0f : 72.0f;
    float baseTileH = compactMode ? 22.0f : 24.0f;
    float gap = compactMode ? 5.0f : 6.0f;
    float padX = 4.0f;
    float padY = 2.0f;

    int tileW = ScI(baseTileW, dpi);
    int maxTileH = std::max(ScI(18.0f, dpi), taskbarThickness - ScI(6.0f, dpi));
    int tileH = std::min(ScI(baseTileH, dpi), maxTileH);
    int gapPx = ScI(gap, dpi);
    int padXPx = ScI(padX, dpi);
    int padYPx = ScI(padY, dpi);

    if (emptyPlaceholder) {
        tileW = ScI(58.0f, dpi);
    }

    if (horizontal) {
        layout.width = padXPx * 2 + count * tileW + (count - 1) * gapPx;
        layout.height = std::min(taskbarThickness, tileH + padYPx * 2);
        int topY = (layout.height - tileH) / 2;

        for (int i = 0; i < count; ++i) {
            int tx = padXPx + i * (tileW + gapPx);
            TileHitBox box;
            box.rect = { tx, topY, tx + tileW, topY + tileH };
            box.deviceIndex = emptyPlaceholder ? -1 : i;
            layout.tiles.push_back(box);
        }
    } else {
        // Vertical taskbar (docked on left/right of screen)
        int maxW = std::max(ScI(54.0f, dpi), taskbarThickness - ScI(8.0f, dpi));
        tileW = std::min(tileW, maxW);
        layout.width = std::min(taskbarThickness, tileW + padXPx * 2);
        layout.height = padYPx * 2 + count * tileH + (count - 1) * gapPx;
        int leftX = (layout.width - tileW) / 2;

        for (int i = 0; i < count; ++i) {
            int ty = padYPx + i * (tileH + gapPx);
            TileHitBox box;
            box.rect = { leftX, ty, leftX + tileW, ty + tileH };
            box.deviceIndex = emptyPlaceholder ? -1 : i;
            layout.tiles.push_back(box);
        }
    }

    return layout;
}

bool RenderToLayeredWindow(
    HWND hwnd,
    const std::vector<BatteryDevice>& devices,
    const WidgetLayout& layout,
    UINT dpi,
    bool lightTaskbar,
    int hoveredIndex,
    bool refreshFlash,
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

        // Imperceptible alpha=1 backdrop so clicks in small gaps between capsules still open our menu
        g.Clear(Gdiplus::Color(1, 0, 0, 0));

        if (devices.empty()) {
            BatteryDevice placeholder;
            placeholder.kind = DeviceKind::Bluetooth;
            placeholder.percent = -1;
            placeholder.connected = false;
            if (!layout.tiles.empty()) {
                DrawBatteryTile(g, placeholder, layout.tiles[0].rect, dpi, lightTaskbar, hoveredIndex == 0, refreshFlash);
            }
        } else {
            for (size_t i = 0; i < layout.tiles.size() && i < devices.size(); ++i) {
                bool isHovered = (hoveredIndex == static_cast<int>(i));
                DrawBatteryTile(g, devices[i], layout.tiles[i].rect, dpi, lightTaskbar, isHovered, refreshFlash);
            }
        }
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
    UINT dpi,
    bool lightTaskbar,
    int hoveredIndex,
    bool refreshFlash)
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

        if (devices.empty()) {
            BatteryDevice placeholder;
            placeholder.kind = DeviceKind::Bluetooth;
            placeholder.percent = -1;
            placeholder.connected = false;
            if (!layout.tiles.empty()) {
                DrawBatteryTile(g, placeholder, layout.tiles[0].rect, dpi, lightTaskbar, hoveredIndex == 0, refreshFlash);
            }
        } else {
            for (size_t i = 0; i < layout.tiles.size() && i < devices.size(); ++i) {
                bool isHovered = (hoveredIndex == static_cast<int>(i));
                DrawBatteryTile(g, devices[i], layout.tiles[i].rect, dpi, lightTaskbar, isHovered, refreshFlash);
            }
        }
    }

    BOOL ok = BitBlt(targetDC, 0, 0, layout.width, layout.height, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(memDC);
    return ok != FALSE;
}

} // namespace Renderer
