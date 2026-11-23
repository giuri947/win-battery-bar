#include "tooltip_window.h"

#include <windows.h>
#include <objidl.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace Gdiplus {
    using std::min;
    using std::max;
}
#include <gdiplus.h>

namespace {

constexpr const wchar_t* kTooltipClass = L"WinBatteryBarTooltipWnd";

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

Gdiplus::Color AccentForLevel(int percent, bool connected, bool charging) {
    using namespace Gdiplus;
    if (!connected || percent < 0) return Color(255, 130, 138, 150);
    if (percent <= 20) return Color(255, 232, 72, 76);
    if (percent <= 40) return Color(255, 235, 162, 24);
    if (charging)      return Color(255, 42, 196, 130);
    return Color(255, 46, 188, 106);
}

std::wstring FormatUpdatedAgo(uint64_t updatedTickMs) {
    if (updatedTickMs == 0) return L"Just now";
    uint64_t now = GetTickCount64();
    uint64_t diffSec = (now >= updatedTickMs) ? ((now - updatedTickMs) / 1000) : 0;
    if (diffSec < 5)  return L"Updated just now";
    if (diffSec < 60) return L"Updated " + std::to_wstring(diffSec) + L"s ago";
    uint64_t mins = diffSec / 60;
    return L"Updated " + std::to_wstring(mins) + L"m ago";
}

} // namespace

TooltipWindow::~TooltipWindow() {
    Destroy();
}

bool TooltipWindow::Create(HINSTANCE hInst) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kTooltipClass;
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT,
        kTooltipClass,
        L"",
        WS_POPUP,
        0, 0, 10, 10,
        nullptr, nullptr, hInst, this);
    return m_hwnd != nullptr;
}

void TooltipWindow::Destroy() {
    if (m_hwnd && IsWindow(m_hwnd)) {
        DestroyWindow(m_hwnd);
    }
    m_hwnd = nullptr;
    m_visible = false;
}

void TooltipWindow::Hide() {
    if (m_hwnd && m_visible) {
        ShowWindow(m_hwnd, SW_HIDE);
        m_visible = false;
    }
}

void TooltipWindow::ShowForDevice(
    const BatteryDevice& dev,
    const RECT& anchorScreenRect,
    UINT dpi,
    bool lightTheme)
{
    std::wstring pctText = (dev.percent >= 0) ? (std::to_wstring(dev.percent) + L"%") : L"--";
    std::wstring subtitle = dev.sourceLabel;
    if (!dev.extraStatus.empty()) {
        if (!subtitle.empty()) subtitle += L" \x00B7 ";
        subtitle += dev.extraStatus;
    }
    std::wstring footer = FormatUpdatedAgo(dev.updatedTickMs) + L" \x00B7 Click to refresh";
    RenderAndPosition(dev.name, pctText, subtitle, footer, dev.percent, dev.connected, dev.charging,
        anchorScreenRect, dpi, lightTheme);
}

void TooltipWindow::ShowEmpty(
    const RECT& anchorScreenRect,
    UINT dpi,
    bool lightTheme)
{
    RenderAndPosition(
        L"WinBatteryBar",
        L"--",
        L"No Bluetooth battery devices connected",
        L"Right-click for options \x00B7 Click to scan",
        -1, false, false, anchorScreenRect, dpi, lightTheme);
}

void TooltipWindow::RenderAndPosition(
    const std::wstring& title,
    const std::wstring& pctText,
    const std::wstring& subtitle,
    const std::wstring& footer,
    int percent,
    bool connected,
    bool charging,
    const RECT& anchorScreenRect,
    UINT dpi,
    bool lightTheme)
{
    if (!m_hwnd) return;

    using namespace Gdiplus;

    int shadowPad = ScI(6.0f, dpi);
    int cardW = ScI(236.0f, dpi);
    int cardH = ScI(88.0f, dpi);
    int w = cardW + shadowPad * 2;
    int h = cardH + shadowPad * 2;

    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        return;
    }
    HGDIOBJ oldBmp = SelectObject(memDC, dib);

    {
        Bitmap bmp(w, h, w * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeHighQuality);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.Clear(Color(0, 0, 0, 0));

        float cx = static_cast<float>(shadowPad);
        float cy = static_cast<float>(shadowPad);
        float cw = static_cast<float>(cardW);
        float ch = static_cast<float>(cardH);
        float rad = Sc(6.0f, dpi);

        // Soft layered drop shadow
        for (int s = 3; s >= 1; --s) {
            float spread = Sc(static_cast<float>(s) * 1.4f, dpi);
            GraphicsPath sp;
            AddRoundedRect(sp, cx - spread * 0.5f, cy + Sc(1.5f, dpi), cw + spread, ch + spread * 0.7f, rad + spread * 0.4f);
            SolidBrush sb(Color(static_cast<BYTE>(28 / s), 0, 0, 0));
            g.FillPath(&sb, &sp);
        }

        // Card surface
        GraphicsPath card;
        AddRoundedRect(card, cx, cy, cw, ch, rad);
        Color bgCol = lightTheme ? Color(250, 250, 251, 253) : Color(248, 28, 31, 38);
        Color borderCol = lightTheme ? Color(210, 186, 194, 206) : Color(175, 76, 82, 96);
        SolidBrush bgBrush(bgCol);
        g.FillPath(&bgBrush, &card);

        Pen borderPen(borderCol, std::max(1.0f, Sc(1.0f, dpi)));
        g.DrawPath(&borderPen, &card);

        float pad = Sc(11.0f, dpi);
        FontFamily fam(L"Segoe UI");
        Font titleFont(&fam, Sc(12.5f, dpi), FontStyleBold, UnitPixel);
        Font pctFont(&fam, Sc(13.0f, dpi), FontStyleBold, UnitPixel);
        Font subFont(&fam, Sc(11.0f, dpi), FontStyleRegular, UnitPixel);
        Font footFont(&fam, Sc(10.5f, dpi), FontStyleRegular, UnitPixel);

        Color textMain = lightTheme ? Color(255, 22, 25, 32) : Color(255, 244, 246, 250);
        Color textSub  = lightTheme ? Color(255, 72, 78, 92) : Color(255, 196, 202, 214);
        Color textFoot = lightTheme ? Color(255, 102, 110, 124) : Color(255, 160, 168, 182);
        Color accent = AccentForLevel(percent, connected, charging);

        SolidBrush bMain(textMain);
        SolidBrush bSub(textSub);
        SolidBrush bFoot(textFoot);
        SolidBrush bAccent(accent);

        StringFormat leftFmt;
        leftFmt.SetAlignment(StringAlignmentNear);
        leftFmt.SetTrimming(StringTrimmingEllipsisCharacter);
        leftFmt.SetFormatFlags(StringFormatFlagsNoWrap);

        StringFormat rightFmt;
        rightFmt.SetAlignment(StringAlignmentFar);
        rightFmt.SetFormatFlags(StringFormatFlagsNoWrap);

        float pctBoxW = Sc(48.0f, dpi);
        RectF titleRect(cx + pad, cy + Sc(9.0f, dpi), cw - pad * 2.0f - pctBoxW, Sc(18.0f, dpi));
        RectF pctRect(cx + cw - pad - pctBoxW, cy + Sc(8.5f, dpi), pctBoxW, Sc(19.0f, dpi));
        g.DrawString(title.c_str(), -1, &titleFont, titleRect, &leftFmt, &bMain);
        g.DrawString(pctText.c_str(), -1, &pctFont, pctRect, &rightFmt, &bAccent);

        // Sleek progress bar track
        float barX = cx + pad;
        float barY = cy + Sc(32.0f, dpi);
        float barW = cw - pad * 2.0f;
        float barH = Sc(5.0f, dpi);
        GraphicsPath track;
        AddRoundedRect(track, barX, barY, barW, barH, barH * 0.5f);
        SolidBrush trackBrush(lightTheme ? Color(255, 218, 222, 232) : Color(255, 54, 58, 68));
        g.FillPath(&trackBrush, &track);

        if (percent > 0) {
            float fillW = std::max(barH, barW * (static_cast<float>(std::clamp(percent, 0, 100)) / 100.0f));
            GraphicsPath fill;
            AddRoundedRect(fill, barX, barY, fillW, barH, barH * 0.5f);
            g.FillPath(&bAccent, &fill);
        }

        // Subtitle & footer
        RectF subRect(cx + pad, cy + Sc(43.5f, dpi), cw - pad * 2.0f, Sc(16.0f, dpi));
        RectF footRect(cx + pad, cy + Sc(62.5f, dpi), cw - pad * 2.0f, Sc(16.0f, dpi));
        g.DrawString(subtitle.c_str(), -1, &subFont, subRect, &leftFmt, &bSub);
        g.DrawString(footer.c_str(), -1, &footFont, footRect, &leftFmt, &bFoot);
    }

    // Position cleanly above the taskbar top edge (or below if top taskbar), clamped to monitor bounds
    HMONITOR mon = MonitorFromRect(&anchorScreenRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);

    int anchorCenterX = (anchorScreenRect.left + anchorScreenRect.right) / 2;
    int px = anchorCenterX - w / 2;
    int gap = ScI(12.0f, dpi);
    int py = anchorScreenRect.top - h + shadowPad - gap;

    if (py < mi.rcMonitor.top) {
        // Taskbar is at top of screen: show cleanly below taskbar edge
        py = anchorScreenRect.bottom - shadowPad + gap;
    }

    int minX = mi.rcMonitor.left + ScI(4.0f, dpi) - shadowPad;
    int maxX = mi.rcMonitor.right - w + shadowPad - ScI(4.0f, dpi);
    px = std::clamp(px, minX, std::max(minX, maxX));

    POINT ptDst = { px, py };
    POINT ptSrc = { 0, 0 };
    SIZE sz = { w, h };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    UpdateLayeredWindow(m_hwnd, screenDC, &ptDst, &sz, memDC, &ptSrc, 0, &bf, ULW_ALPHA);
    SetWindowPos(m_hwnd, HWND_TOPMOST, px, py, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    m_visible = true;

    SelectObject(memDC, oldBmp);
    DeleteObject(dib);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

LRESULT CALLBACK TooltipWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(hwnd, msg, wp, lp);
}
