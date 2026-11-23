#include "settings_window.h"
#include "renderer.h"
#include "settings.h"

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

constexpr const wchar_t* kSettingsClass = L"WinBatteryBarSettingsPanel";
constexpr int kMaxVisibleDevices = 5;

float Sc(float px, UINT dpi) {
    return px * (static_cast<float>(dpi) / 96.0f);
}

int ScI(float px, UINT dpi) {
    return static_cast<int>(std::lround(Sc(px, dpi)));
}

RECT MakeRect(float x, float y, float w, float h) {
    return {
        static_cast<LONG>(std::lround(x)),
        static_cast<LONG>(std::lround(y)),
        static_cast<LONG>(std::lround(x + w)),
        static_cast<LONG>(std::lround(y + h))
    };
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

void DrawToggleSwitch(
    Gdiplus::Graphics& g,
    float x, float y, float w, float h,
    bool on, bool disabled, bool hovered, bool lightTheme)
{
    using namespace Gdiplus;
    GraphicsPath track;
    AddRoundedRect(track, x, y, w, h, h * 0.5f);

    Color fillCol;
    if (disabled) {
        fillCol = lightTheme ? Color(180, 200, 206, 216) : Color(160, 56, 60, 70);
    } else if (on) {
        fillCol = hovered ? Color(255, 50, 198, 120) : Color(255, 36, 178, 102);
    } else {
        fillCol = lightTheme
            ? (hovered ? Color(255, 174, 182, 196) : Color(255, 192, 198, 210))
            : (hovered ? Color(255, 76, 82, 96) : Color(255, 60, 65, 78));
    }
    SolidBrush tb(fillCol);
    g.FillPath(&tb, &track);

    float pad = h * 0.15f;
    float knobD = h - pad * 2.0f;
    float knobX = on ? (x + w - pad - knobD) : (x + pad);
    float knobY = y + pad;
    SolidBrush kb(disabled ? Color(190, 220, 224, 232) : Color(255, 255, 255, 255));
    g.FillEllipse(&kb, knobX, knobY, knobD, knobD);
}

void DrawPillButton(
    Gdiplus::Graphics& g,
    const RECT& r,
    const std::wstring& label,
    const Gdiplus::Font& font,
    bool active,
    bool hovered,
    bool alertRed,
    bool lightTheme,
    UINT dpi)
{
    using namespace Gdiplus;
    float x = static_cast<float>(r.left);
    float y = static_cast<float>(r.top);
    float w = static_cast<float>(r.right - r.left);
    float h = static_cast<float>(r.bottom - r.top);

    GraphicsPath p;
    AddRoundedRect(p, x, y, w, h, Sc(4.5f, dpi));

    Color bg, border, fg;
    if (active) {
        if (alertRed) {
            bg = Color(255, 218, 46, 52);
            border = Color(255, 248, 92, 96);
            fg = Color(255, 255, 255, 255);
        } else {
            bg = lightTheme ? Color(255, 24, 132, 76) : Color(255, 32, 162, 92);
            border = lightTheme ? Color(255, 18, 114, 64) : Color(255, 62, 206, 126);
            fg = Color(255, 255, 255, 255);
        }
    } else {
        if (alertRed && hovered) {
            bg = lightTheme ? Color(255, 248, 226, 228) : Color(255, 86, 30, 34);
            border = Color(220, 224, 68, 72);
            fg = lightTheme ? Color(255, 180, 28, 34) : Color(255, 255, 160, 164);
        } else {
            bg = lightTheme
                ? (hovered ? Color(255, 226, 231, 240) : Color(255, 238, 241, 246))
                : (hovered ? Color(255, 56, 61, 74) : Color(255, 42, 46, 56));
            border = lightTheme ? Color(210, 198, 205, 218) : Color(180, 70, 76, 92);
            fg = lightTheme ? Color(255, 28, 32, 40) : Color(255, 228, 232, 242);
        }
    }

    SolidBrush bgBrush(bg);
    g.FillPath(&bgBrush, &p);
    Pen bPen(border, std::max(1.0f, Sc(1.0f, dpi)));
    g.DrawPath(&bPen, &p);

    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush fgBrush(fg);
    g.DrawString(label.c_str(), -1, &font, RectF(x, y + Sc(0.3f, dpi), w, h), &fmt, &fgBrush);
}

} // namespace

SettingsWindow::~SettingsWindow() {
    Destroy();
}

bool SettingsWindow::Create(HINSTANCE hInst) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kSettingsClass;
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kSettingsClass,
        L"WinBatteryBar Settings",
        WS_POPUP,
        0, 0, 10, 10,
        nullptr, nullptr, hInst, this);
    return m_hwnd != nullptr;
}

void SettingsWindow::Destroy() {
    if (m_hwnd && IsWindow(m_hwnd)) {
        DestroyWindow(m_hwnd);
    }
    m_hwnd = nullptr;
    m_visible = false;
}

void SettingsWindow::Show(
    const AppSettings& settings,
    const std::vector<BatteryDevice>& allDevices,
    const RECT& anchorScreenRect,
    UINT dpi,
    bool lightTheme,
    bool demoMode,
    bool keepOpenForDemo,
    SettingsPanelCallbacks callbacks)
{
    if (!m_hwnd || !IsWindow(m_hwnd)) return;
    m_settings = settings;
    m_devices = allDevices;
    m_anchorRect = anchorScreenRect;
    m_dpi = dpi;
    m_lightTheme = lightTheme;
    m_demoMode = demoMode;
    m_keepOpenForDemo = keepOpenForDemo;
    m_cb = std::move(callbacks);
    m_hoveredZone = -1;
    m_trackingMouse = false;

    Render(true);
    ShowWindow(m_hwnd, keepOpenForDemo ? SW_SHOWNOACTIVATE : SW_SHOW);
    if (!keepOpenForDemo) {
        SetForegroundWindow(m_hwnd);
    }
    m_visible = true;
}

void SettingsWindow::UpdateState(
    const AppSettings& settings,
    const std::vector<BatteryDevice>& allDevices,
    UINT dpi,
    bool lightTheme)
{
    m_settings = settings;
    m_devices = allDevices;
    m_dpi = dpi;
    m_lightTheme = lightTheme;
    if (m_visible && m_hwnd && IsWindow(m_hwnd)) {
        Render(false);
    }
}

void SettingsWindow::Hide() {
    if (m_hwnd && IsWindow(m_hwnd) && m_visible) {
        ShowWindow(m_hwnd, SW_HIDE);
    }
    m_visible = false;
    m_hoveredZone = -1;
    m_trackingMouse = false;
}

int SettingsWindow::HitTest(int x, int y) const {
    POINT pt = { x, y };
    for (size_t i = 0; i < m_zones.size(); ++i) {
        if (PtInRect(&m_zones[i].rect, pt)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void SettingsWindow::HandleClick(const HitZone& zone) {
    bool changed = false;

    switch (zone.action) {
    case HitAction::CloseBtn:
        Hide();
        return;

    case HitAction::StyleFull:
        if (m_settings.displayStyle != DisplayStyle::Full) {
            m_settings.displayStyle = DisplayStyle::Full;
            changed = true;
        }
        break;
    case HitAction::StyleCompact:
        if (m_settings.displayStyle != DisplayStyle::Compact) {
            m_settings.displayStyle = DisplayStyle::Compact;
            changed = true;
        }
        break;
    case HitAction::StyleUltraCompact:
        if (m_settings.displayStyle != DisplayStyle::UltraCompact) {
            m_settings.displayStyle = DisplayStyle::UltraCompact;
            changed = true;
        }
        break;

    case HitAction::ToggleLaptop:
        m_settings.showLaptop = !m_settings.showLaptop;
        changed = true;
        break;
    case HitAction::ToggleDisconnected:
        m_settings.showDisconnected = !m_settings.showDisconnected;
        changed = true;
        break;
    case HitAction::ToggleNetwork:
        m_settings.showNetwork = !m_settings.showNetwork;
        changed = true;
        break;
    case HitAction::NetUnitBytes:
        if (m_settings.netUnits != NetUnitMode::BytesPerSec) {
            m_settings.netUnits = NetUnitMode::BytesPerSec;
            changed = true;
        }
        break;
    case HitAction::NetUnitBits:
        if (m_settings.netUnits != NetUnitMode::BitsPerSec) {
            m_settings.netUnits = NetUnitMode::BitsPerSec;
            changed = true;
        }
        break;

    case HitAction::LowThresh15: m_settings.lowBatteryThreshold = 15; changed = true; break;
    case HitAction::LowThresh20: m_settings.lowBatteryThreshold = 20; changed = true; break;
    case HitAction::LowThresh25: m_settings.lowBatteryThreshold = 25; changed = true; break;
    case HitAction::LowThresh30: m_settings.lowBatteryThreshold = 30; changed = true; break;

    case HitAction::Poll15: m_settings.pollIntervalSec = 15; changed = true; break;
    case HitAction::Poll30: m_settings.pollIntervalSec = 30; changed = true; break;
    case HitAction::Poll60: m_settings.pollIntervalSec = 60; changed = true; break;

    case HitAction::ToggleDeviceVis:
        if (zone.param >= 0 && static_cast<size_t>(zone.param) < m_devices.size()) {
            SettingsStore::ToggleDeviceHidden(m_settings, m_devices[static_cast<size_t>(zone.param)].id);
            changed = true;
        }
        break;
    case HitAction::CycleDeviceIcon:
        if (zone.param >= 0 && static_cast<size_t>(zone.param) < m_devices.size()) {
            SettingsStore::CycleIconOverride(m_settings, m_devices[static_cast<size_t>(zone.param)].id);
            changed = true;
        }
        break;

    case HitAction::ToggleAutoStart:
        if (!m_demoMode && m_cb.onToggleAutoStart) {
            m_cb.onToggleAutoStart(!m_settings.autoStart);
        }
        return;

    case HitAction::BtnRefresh:
        if (m_cb.onRefreshNow) m_cb.onRefreshNow();
        return;
    case HitAction::BtnDiagnostics:
        if (m_cb.onCopyDiagnostics) m_cb.onCopyDiagnostics();
        return;
    case HitAction::BtnExit:
        Hide();
        if (m_cb.onExitApp) m_cb.onExitApp();
        return;

    case HitAction::None:
    default:
        return;
    }

    if (changed) {
        Render(false);
        if (m_cb.onSettingsChanged) {
            m_cb.onSettingsChanged(m_settings);
        }
    }
}

void SettingsWindow::Render(bool reposition) {
    if (!m_hwnd || !IsWindow(m_hwnd)) return;

    using namespace Gdiplus;

    UINT dpi = m_dpi;
    int shadowPad = ScI(8.0f, dpi);
    int cardW = ScI(332.0f, dpi);

    int shownDevRows = std::max(1, std::min(kMaxVisibleDevices, static_cast<int>(m_devices.size())));
    float cardHf = Sc(436.0f, dpi) + shownDevRows * Sc(27.0f, dpi);
    int cardH = static_cast<int>(std::lround(cardHf));
    int w = cardW + shadowPad * 2;
    int h = cardH + shadowPad * 2;
    if (h != m_lastRenderedH) {
        reposition = true;
        m_lastRenderedH = h;
    }

    HDC screenDC = GetDC(nullptr);
    if (!screenDC) return;
    HDC memDC = CreateCompatibleDC(screenDC);
    if (!memDC) {
        ReleaseDC(nullptr, screenDC);
        return;
    }

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

    m_zones.clear();

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
        float rad = Sc(8.0f, dpi);
        float pad = Sc(14.0f, dpi);
        float contentW = cw - pad * 2.0f;

        // Soft elevation shadow
        for (int s = 4; s >= 1; --s) {
            float spread = Sc(static_cast<float>(s) * 1.6f, dpi);
            GraphicsPath sp;
            AddRoundedRect(sp, cx - spread * 0.5f, cy + Sc(2.0f, dpi), cw + spread, ch + spread * 0.7f, rad + spread * 0.4f);
            SolidBrush sb(Color(static_cast<BYTE>(28 / s), 0, 0, 0));
            g.FillPath(&sb, &sp);
        }

        // Card surface
        GraphicsPath card;
        AddRoundedRect(card, cx, cy, cw, ch, rad);
        Color bgCol     = m_lightTheme ? Color(252, 249, 250, 252) : Color(250, 28, 31, 38);
        Color borderCol = m_lightTheme ? Color(220, 188, 196, 210) : Color(190, 74, 80, 96);
        Color sepCol    = m_lightTheme ? Color(255, 224, 228, 236) : Color(255, 48, 52, 64);
        Color textMain  = m_lightTheme ? Color(255, 22, 25, 32)    : Color(255, 244, 246, 250);
        Color textSub   = m_lightTheme ? Color(255, 92, 98, 112)   : Color(255, 162, 170, 186);
        Color rowHover  = m_lightTheme ? Color(255, 236, 240, 246) : Color(255, 42, 46, 58);

        SolidBrush bgBrush(bgCol);
        g.FillPath(&bgBrush, &card);
        Pen borderPen(borderCol, std::max(1.0f, Sc(1.0f, dpi)));
        g.DrawPath(&borderPen, &card);

        FontFamily fam(L"Segoe UI");
        Font titleFont(&fam, Sc(13.5f, dpi), FontStyleBold, UnitPixel);
        Font secFont(&fam, Sc(9.6f, dpi), FontStyleBold, UnitPixel);
        Font labelFont(&fam, Sc(11.4f, dpi), FontStyleRegular, UnitPixel);
        Font pillFont(&fam, Sc(10.6f, dpi), FontStyleBold, UnitPixel);
        Font smallFont(&fam, Sc(9.8f, dpi), FontStyleRegular, UnitPixel);

        SolidBrush bMain(textMain);
        SolidBrush bSub(textSub);
        SolidBrush bSep(sepCol);

        StringFormat leftFmt;
        leftFmt.SetAlignment(StringAlignmentNear);
        leftFmt.SetLineAlignment(StringAlignmentCenter);
        leftFmt.SetTrimming(StringTrimmingEllipsisCharacter);
        leftFmt.SetFormatFlags(StringFormatFlagsNoWrap);

        StringFormat rightFmt;
        rightFmt.SetAlignment(StringAlignmentFar);
        rightFmt.SetLineAlignment(StringAlignmentCenter);
        rightFmt.SetFormatFlags(StringFormatFlagsNoWrap);

        auto addZone = [&](const RECT& rc, HitAction act, int param = -1) -> bool {
            int idx = static_cast<int>(m_zones.size());
            m_zones.push_back({ rc, act, param });
            return (m_hoveredZone == idx);
        };

        float y = cy;

        // 1. Header
        RectF titleRc(cx + pad, y + Sc(6.0f, dpi), contentW - Sc(30.0f, dpi), Sc(26.0f, dpi));
        g.DrawString(L"WinBatteryBar Settings", -1, &titleFont, titleRc, &leftFmt, &bMain);

        float closeS = Sc(22.0f, dpi);
        RECT closeRc = MakeRect(cx + cw - pad - closeS, y + Sc(8.0f, dpi), closeS, closeS);
        bool closeHov = addZone(closeRc, HitAction::CloseBtn);
        if (closeHov) {
            GraphicsPath cp;
            AddRoundedRect(cp, static_cast<float>(closeRc.left), static_cast<float>(closeRc.top), closeS, closeS, Sc(4.0f, dpi));
            SolidBrush hb(rowHover);
            g.FillPath(&hb, &cp);
        }
        Pen xPen(closeHov ? textMain : textSub, std::max(1.3f, Sc(1.4f, dpi)));
        xPen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        float xm = Sc(6.5f, dpi);
        g.DrawLine(&xPen, closeRc.left + xm, closeRc.top + xm, closeRc.right - xm, closeRc.bottom - xm);
        g.DrawLine(&xPen, closeRc.right - xm, closeRc.top + xm, closeRc.left + xm, closeRc.bottom - xm);

        y += Sc(36.0f, dpi);
        g.FillRectangle(&bSep, cx + pad, y, contentW, std::max(1.0f, Sc(1.0f, dpi)));
        y += Sc(8.0f, dpi);

        // 2. Display Style (Full / Compact / Ultra-Compact)
        g.DrawString(L"DISPLAY STYLE", -1, &secFont, RectF(cx + pad, y, contentW, Sc(14.0f, dpi)), &leftFmt, &bSub);
        y += Sc(17.0f, dpi);

        float gap = Sc(6.0f, dpi);
        float pillH = Sc(25.0f, dpi);
        float styleW = (contentW - gap * 2.0f) / 3.0f;

        struct StyleBtn { const wchar_t* text; DisplayStyle st; HitAction act; };
        StyleBtn styleBtns[3] = {
            { L"Full", DisplayStyle::Full, HitAction::StyleFull },
            { L"Compact", DisplayStyle::Compact, HitAction::StyleCompact },
            { L"Ultra-Compact", DisplayStyle::UltraCompact, HitAction::StyleUltraCompact }
        };
        for (int i = 0; i < 3; ++i) {
            RECT r = MakeRect(cx + pad + i * (styleW + gap), y, styleW, pillH);
            bool hov = addZone(r, styleBtns[i].act);
            DrawPillButton(g, r, styleBtns[i].text, pillFont,
                m_settings.displayStyle == styleBtns[i].st, hov, false, m_lightTheme, dpi);
        }

        y += pillH + Sc(10.0f, dpi);
        g.FillRectangle(&bSep, cx + pad, y, contentW, std::max(1.0f, Sc(1.0f, dpi)));
        y += Sc(6.0f, dpi);

        // 3. Toggles (Laptop, Disconnected, Network Monitor + Units)
        float rowH = Sc(28.0f, dpi);
        float swW = Sc(34.0f, dpi);
        float swH = Sc(17.0f, dpi);

        auto drawToggleRow = [&](const wchar_t* text, bool val, bool disabled, HitAction act) {
            RECT r = MakeRect(cx + pad - Sc(4.0f, dpi), y, contentW + Sc(8.0f, dpi), rowH);
            bool hov = addZone(r, act);
            if (hov && !disabled) {
                GraphicsPath rp;
                AddRoundedRect(rp, static_cast<float>(r.left), static_cast<float>(r.top),
                    static_cast<float>(r.right - r.left), rowH, Sc(4.0f, dpi));
                SolidBrush hb(rowHover);
                g.FillPath(&hb, &rp);
            }
            SolidBrush lb(disabled ? textSub : textMain);
            g.DrawString(text, -1, &labelFont, RectF(cx + pad, y, contentW - swW - Sc(8.0f, dpi), rowH), &leftFmt, &lb);
            DrawToggleSwitch(g, cx + cw - pad - swW, y + (rowH - swH) * 0.5f, swW, swH, val, disabled, hov, m_lightTheme);
            y += rowH;
        };

        drawToggleRow(L"Show Laptop Battery", m_settings.showLaptop, false, HitAction::ToggleLaptop);
        drawToggleRow(L"Show Disconnected Devices", m_settings.showDisconnected, false, HitAction::ToggleDisconnected);
        drawToggleRow(L"Network Activity (\x2193 / \x2191 Speed)", m_settings.showNetwork, false, HitAction::ToggleNetwork);

        // Network Units sub-row
        y += Sc(2.0f, dpi);
        float unitRowH = Sc(22.0f, dpi);
        g.DrawString(L"Network Units", -1, &smallFont, RectF(cx + pad, y, Sc(105.0f, dpi), unitRowH), &leftFmt, &bSub);
        float unitW = Sc(90.0f, dpi);
        RECT uBytes = MakeRect(cx + cw - pad - unitW * 2.0f - gap, y, unitW, unitRowH);
        RECT uBits  = MakeRect(cx + cw - pad - unitW, y, unitW, unitRowH);
        bool uBytesHov = addZone(uBytes, HitAction::NetUnitBytes);
        bool uBitsHov  = addZone(uBits, HitAction::NetUnitBits);
        DrawPillButton(g, uBytes, L"KB/s \x00B7 MB/s", smallFont,
            m_settings.netUnits == NetUnitMode::BytesPerSec, uBytesHov, false, m_lightTheme, dpi);
        DrawPillButton(g, uBits, L"Kbps \x00B7 Mbps", smallFont,
            m_settings.netUnits == NetUnitMode::BitsPerSec, uBitsHov, false, m_lightTheme, dpi);

        y += unitRowH + Sc(8.0f, dpi);
        g.FillRectangle(&bSep, cx + pad, y, contentW, std::max(1.0f, Sc(1.0f, dpi)));
        y += Sc(7.0f, dpi);

        // 4. Low Battery Red Alert Threshold (no notifications)
        g.DrawString(L"LOW BATTERY RED ALERT", -1, &secFont, RectF(cx + pad, y, Sc(165.0f, dpi), Sc(14.0f, dpi)), &leftFmt, &bSub);
        g.DrawString(L"Red bar, no popups", -1, &smallFont, RectF(cx + pad + Sc(160.0f, dpi), y, contentW - Sc(160.0f, dpi), Sc(14.0f, dpi)), &rightFmt, &bSub);
        y += Sc(17.0f, dpi);

        float threshW = (contentW - gap * 3.0f) / 4.0f;
        float smPillH = Sc(22.0f, dpi);
        struct ThreshBtn { const wchar_t* text; int val; HitAction act; };
        ThreshBtn tBtns[4] = {
            { L"15%", 15, HitAction::LowThresh15 },
            { L"20%", 20, HitAction::LowThresh20 },
            { L"25%", 25, HitAction::LowThresh25 },
            { L"30%", 30, HitAction::LowThresh30 }
        };
        for (int i = 0; i < 4; ++i) {
            RECT r = MakeRect(cx + pad + i * (threshW + gap), y, threshW, smPillH);
            bool hov = addZone(r, tBtns[i].act);
            DrawPillButton(g, r, tBtns[i].text, pillFont,
                m_settings.lowBatteryThreshold == tBtns[i].val, hov, true, m_lightTheme, dpi);
        }

        y += smPillH + Sc(9.0f, dpi);
        g.FillRectangle(&bSep, cx + pad, y, contentW, std::max(1.0f, Sc(1.0f, dpi)));
        y += Sc(7.0f, dpi);

        // 5. Bluetooth Poll Interval
        g.DrawString(L"BLUETOOTH POLL INTERVAL", -1, &secFont, RectF(cx + pad, y, contentW, Sc(14.0f, dpi)), &leftFmt, &bSub);
        y += Sc(17.0f, dpi);

        float pollW = (contentW - gap * 2.0f) / 3.0f;
        struct PollBtn { const wchar_t* text; int val; HitAction act; };
        PollBtn pBtns[3] = {
            { L"15 sec", 15, HitAction::Poll15 },
            { L"30 sec", 30, HitAction::Poll30 },
            { L"60 sec", 60, HitAction::Poll60 }
        };
        for (int i = 0; i < 3; ++i) {
            RECT r = MakeRect(cx + pad + i * (pollW + gap), y, pollW, smPillH);
            bool hov = addZone(r, pBtns[i].act);
            DrawPillButton(g, r, pBtns[i].text, pillFont,
                m_settings.pollIntervalSec == pBtns[i].val, hov, false, m_lightTheme, dpi);
        }

        y += smPillH + Sc(9.0f, dpi);
        g.FillRectangle(&bSep, cx + pad, y, contentW, std::max(1.0f, Sc(1.0f, dpi)));
        y += Sc(7.0f, dpi);

        // 6. Detected Devices (visibility checkbox + clickable icon override + live %)
        g.DrawString(L"DEVICES", -1, &secFont, RectF(cx + pad, y, Sc(100.0f, dpi), Sc(14.0f, dpi)), &leftFmt, &bSub);
        g.DrawString(L"Click icon to change", -1, &smallFont, RectF(cx + pad + Sc(100.0f, dpi), y, contentW - Sc(100.0f, dpi), Sc(14.0f, dpi)), &rightFmt, &bSub);
        y += Sc(16.0f, dpi);

        float devRowH = Sc(27.0f, dpi);
        if (m_devices.empty()) {
            g.DrawString(L"No battery devices detected yet", -1, &smallFont, RectF(cx + pad, y, contentW, devRowH), &leftFmt, &bSub);
            y += devRowH;
        } else {
            int count = std::min(kMaxVisibleDevices, static_cast<int>(m_devices.size()));
            for (int i = 0; i < count; ++i) {
                const auto& d = m_devices[static_cast<size_t>(i)];
                bool shown = !SettingsStore::IsDeviceHidden(m_settings, d.id);
                auto ov = SettingsStore::GetIconOverride(m_settings, d.id);
                DeviceKind effKind = ov.value_or(d.kind);

                // Visibility checkbox zone
                float cbS = Sc(15.0f, dpi);
                float cbX = cx + pad;
                float cbY = y + (devRowH - cbS) * 0.5f;
                RECT visRc = MakeRect(cbX - Sc(2.0f, dpi), y + Sc(2.0f, dpi), cbS + Sc(4.0f, dpi), devRowH - Sc(4.0f, dpi));
                bool visHov = addZone(visRc, HitAction::ToggleDeviceVis, i);

                GraphicsPath cbPath;
                AddRoundedRect(cbPath, cbX, cbY, cbS, cbS, Sc(3.0f, dpi));
                if (shown) {
                    SolidBrush cbf(visHov ? Color(255, 48, 194, 114) : Color(255, 36, 174, 98));
                    g.FillPath(&cbf, &cbPath);
                    Pen chk(Color(255, 255, 255, 255), std::max(1.5f, Sc(1.6f, dpi)));
                    chk.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
                    chk.SetLineJoin(LineJoinRound);
                    PointF pts[3] = {
                        { cbX + cbS * 0.22f, cbY + cbS * 0.52f },
                        { cbX + cbS * 0.43f, cbY + cbS * 0.74f },
                        { cbX + cbS * 0.78f, cbY + cbS * 0.28f }
                    };
                    g.DrawLines(&chk, pts, 3);
                } else {
                    Pen cbp(visHov ? textMain : textSub, std::max(1.1f, Sc(1.2f, dpi)));
                    g.DrawPath(&cbp, &cbPath);
                }

                // Clickable icon override badge
                float iconBtnW = Sc(24.0f, dpi);
                float iconBtnH = Sc(21.0f, dpi);
                float iconBtnX = cbX + cbS + Sc(8.0f, dpi);
                float iconBtnY = y + (devRowH - iconBtnH) * 0.5f;
                RECT iconRc = MakeRect(iconBtnX, iconBtnY, iconBtnW, iconBtnH);
                bool iconHov = addZone(iconRc, HitAction::CycleDeviceIcon, i);

                GraphicsPath ibPath;
                AddRoundedRect(ibPath, iconBtnX, iconBtnY, iconBtnW, iconBtnH, Sc(4.0f, dpi));
                Color ibBg = m_lightTheme
                    ? (iconHov ? Color(255, 218, 224, 236) : Color(255, 234, 238, 246))
                    : (iconHov ? Color(255, 62, 68, 84) : Color(255, 44, 48, 60));
                SolidBrush ibBrush(ibBg);
                g.FillPath(&ibBrush, &ibPath);
                if (ov.has_value()) {
                    Pen ovPen(Color(200, 72, 186, 255), std::max(1.0f, Sc(1.0f, dpi)));
                    g.DrawPath(&ovPen, &ibPath);
                }
                float isz = Sc(13.0f, dpi);
                Color iCol = m_lightTheme ? Color(255, 34, 38, 48) : Color(255, 240, 244, 252);
                Renderer::DrawDeviceIcon(g, effKind, d.charging,
                    iconBtnX + (iconBtnW - isz) * 0.5f,
                    iconBtnY + (iconBtnH - isz) * 0.5f,
                    isz, iCol);

                // Device name
                float nameX = iconBtnX + iconBtnW + Sc(8.0f, dpi);
                float pctW = Sc(44.0f, dpi);
                float nameW = std::max(Sc(60.0f, dpi), (cx + cw - pad - pctW) - nameX);
                SolidBrush nb(shown ? textMain : textSub);
                std::wstring dispName = d.name;
                if (!d.connected) dispName += L" (Off)";
                g.DrawString(dispName.c_str(), -1, &labelFont, RectF(nameX, y, nameW, devRowH), &leftFmt, &nb);

                // Right-aligned live battery %
                std::wstring pctStr = (d.percent >= 0) ? (std::to_wstring(d.percent) + L"%") : L"--";
                Color pctCol = textSub;
                if (d.connected && d.percent >= 0) {
                    if (d.percent <= m_settings.lowBatteryThreshold) pctCol = Color(255, 242, 68, 72);
                    else if (d.percent <= std::max(40, m_settings.lowBatteryThreshold + 15)) pctCol = Color(255, 224, 152, 18);
                    else pctCol = m_lightTheme ? Color(255, 24, 148, 78) : Color(255, 56, 206, 122);
                }
                SolidBrush pb(pctCol);
                g.DrawString(pctStr.c_str(), -1, &pillFont, RectF(cx + cw - pad - pctW, y, pctW, devRowH), &rightFmt, &pb);

                y += devRowH;
            }
        }

        y += Sc(6.0f, dpi);
        g.FillRectangle(&bSep, cx + pad, y, contentW, std::max(1.0f, Sc(1.0f, dpi)));
        y += Sc(4.0f, dpi);

        // 7. Start with Windows + Action buttons
        drawToggleRow(L"Start with Windows", m_settings.autoStart, m_demoMode, HitAction::ToggleAutoStart);
        y += Sc(5.0f, dpi);

        float actH = Sc(26.0f, dpi);
        float actW = (contentW - gap * 2.0f) / 3.0f;
        RECT rRef  = MakeRect(cx + pad, y, actW, actH);
        RECT rDiag = MakeRect(cx + pad + actW + gap, y, actW, actH);
        RECT rExit = MakeRect(cx + pad + (actW + gap) * 2.0f, y, actW, actH);
        bool hRef  = addZone(rRef, HitAction::BtnRefresh);
        bool hDiag = addZone(rDiag, HitAction::BtnDiagnostics);
        bool hExit = addZone(rExit, HitAction::BtnExit);
        DrawPillButton(g, rRef,  L"Refresh Now", pillFont, false, hRef,  false, m_lightTheme, dpi);
        DrawPillButton(g, rDiag, L"Diagnostics", pillFont, false, hDiag, false, m_lightTheme, dpi);
        DrawPillButton(g, rExit, L"Exit App",    pillFont, false, hExit, true,  m_lightTheme, dpi);
    }

    POINT ptDst = {};
    POINT* dstPtr = nullptr;
    if (reposition) {
        HMONITOR mon = MonitorFromRect(&m_anchorRect, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(mon, &mi);

        int gapPx = ScI(10.0f, dpi);
        int px = m_anchorRect.right - w + shadowPad;
        int py = m_anchorRect.top - h + shadowPad - gapPx;
        if (py < mi.rcMonitor.top) {
            py = m_anchorRect.bottom - shadowPad + gapPx;
        }
        int minX = mi.rcMonitor.left + ScI(6.0f, dpi) - shadowPad;
        int maxX = mi.rcMonitor.right - w + shadowPad - ScI(6.0f, dpi);
        int minY = mi.rcMonitor.top + ScI(6.0f, dpi) - shadowPad;
        int maxY = mi.rcMonitor.bottom - h + shadowPad - ScI(6.0f, dpi);
        px = std::clamp(px, minX, std::max(minX, maxX));
        py = std::clamp(py, minY, std::max(minY, maxY));

        ptDst = { px, py };
        dstPtr = &ptDst;
    }

    POINT ptSrc = { 0, 0 };
    SIZE sz = { w, h };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(m_hwnd, screenDC, dstPtr, &sz, memDC, &ptSrc, 0, &bf, ULW_ALPHA);
    if (reposition) {
        SetWindowPos(m_hwnd, HWND_TOPMOST, ptDst.x, ptDst.y, w, h, SWP_NOACTIVATE);
    }

    SelectObject(memDC, oldBmp);
    DeleteObject(dib);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

LRESULT CALLBACK SettingsWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SettingsWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<SettingsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) {
        return self->WndProc(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEMOVE: {
        if (!m_trackingMouse) {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            m_trackingMouse = true;
        }
        int x = static_cast<int>(static_cast<short>(LOWORD(lp)));
        int y = static_cast<int>(static_cast<short>(HIWORD(lp)));
        int hit = HitTest(x, y);
        if (hit != m_hoveredZone) {
            m_hoveredZone = hit;
            Render(false);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        m_trackingMouse = false;
        if (m_hoveredZone != -1) {
            m_hoveredZone = -1;
            Render(false);
        }
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, m_hoveredZone >= 0 ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;

    case WM_LBUTTONUP: {
        int x = static_cast<int>(static_cast<short>(LOWORD(lp)));
        int y = static_cast<int>(static_cast<short>(HIWORD(lp)));
        int hit = HitTest(x, y);
        if (hit >= 0 && static_cast<size_t>(hit) < m_zones.size()) {
            HitZone zone = m_zones[static_cast<size_t>(hit)];
            HandleClick(zone);
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            Hide();
            return 0;
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && !m_keepOpenForDemo) {
            Hide();
        }
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (m_hwnd == hwnd) {
            m_hwnd = nullptr;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
