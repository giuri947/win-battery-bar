#include "taskbar_dock.h"

#include <algorithm>
#include <cmath>

namespace {

using GetDpiForWindowFn = UINT(WINAPI*)(HWND);

UINT SafeGetDpiForWindow(HWND hwnd) {
    static GetDpiForWindowFn fn = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (fn && hwnd && IsWindow(hwnd)) {
        UINT dpi = fn(hwnd);
        if (dpi >= 72 && dpi <= 480) return dpi;
    }
    HDC hdc = GetDC(nullptr);
    UINT dpi = hdc ? static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX)) : 96;
    if (hdc) ReleaseDC(nullptr, hdc);
    return dpi > 0 ? dpi : 96;
}

int ScalePx(int px, UINT dpi) {
    return MulDiv(px, static_cast<int>(dpi), 96);
}

} // namespace

TaskbarDock::~TaskbarDock() {
    RestoreTaskList();
}

bool TaskbarDock::QueryLightTaskbarTheme() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return false;
    }
    DWORD val = 0;
    DWORD type = 0;
    DWORD size = sizeof(DWORD);
    bool light = false;
    if (RegQueryValueExW(key, L"SystemUsesLightTheme", nullptr, &type,
        reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS && type == REG_DWORD)
    {
        light = (val != 0);
    }
    RegCloseKey(key);
    return light;
}

bool TaskbarDock::FindHandles() {
    m_hTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!m_hTaskbar || !IsWindow(m_hTaskbar)) {
        return false;
    }

    m_hTrayNotify = FindWindowExW(m_hTaskbar, nullptr, L"TrayNotifyWnd", nullptr);
    m_hReBar = FindWindowExW(m_hTaskbar, nullptr, L"ReBarWindow32", nullptr);
    if (!m_hReBar) {
        m_hReBar = FindWindowExW(m_hTaskbar, nullptr, L"WorkerW", nullptr);
    }

    HWND win11Bridge = FindWindowExW(m_hTaskbar, nullptr,
        L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr);
    m_isWin11 = (win11Bridge != nullptr);

    if (m_hReBar) {
        m_hTaskList = FindWindowExW(m_hReBar, nullptr, L"MSTaskSwWClass", nullptr);
        if (!m_hTaskList) {
            m_hTaskList = FindWindowExW(m_hReBar, nullptr, L"MSTaskListWClass", nullptr);
        }
    } else {
        m_hTaskList = nullptr;
    }

    RECT rcTaskbar = {};
    if (GetWindowRect(m_hTaskbar, &rcTaskbar)) {
        int w = rcTaskbar.right - rcTaskbar.left;
        int h = rcTaskbar.bottom - rcTaskbar.top;
        m_horizontal = (w >= h);
    }

    // On Windows 10, parent directly into ReBarWindow32 so we reserve real space beside TrayNotifyWnd.
    // On Windows 11, parent into Shell_TrayWnd and sit cleanly to the left of TrayNotifyWnd.
    if (!m_isWin11 && m_hReBar && m_hTaskList) {
        m_hParent = m_hReBar;
    } else {
        m_hParent = m_hTaskbar;
    }
    return true;
}

bool TaskbarDock::Attach(HWND widgetHwnd) {
    if (!widgetHwnd || !IsWindow(widgetHwnd)) return false;

    RestoreTaskList();
    m_attached = false;
    m_hasShrunk = false;
    m_appliedShrink = 0;
    m_expectedMinLen = 0;
    m_lastBarW = 0;
    m_lastBarH = 0;
    m_lastWidgetW = 0;
    m_lastWidgetH = 0;

    if (!FindHandles() || !m_hParent) {
        return false;
    }

    // Switch to WS_CHILD before parenting into the taskbar container
    LONG_PTR style = GetWindowLongPtrW(widgetHwnd, GWL_STYLE);
    style = (style & ~static_cast<LONG_PTR>(WS_POPUP)) | WS_CHILD | WS_CLIPSIBLINGS;
    SetWindowLongPtrW(widgetHwnd, GWL_STYLE, style);

    SetLastError(0);
    HWND prev = SetParent(widgetHwnd, m_hParent);
    if (!prev && GetLastError() != 0) {
        // Fallback: stay as topmost popup
        style = (style & ~static_cast<LONG_PTR>(WS_CHILD)) | WS_POPUP;
        SetWindowLongPtrW(widgetHwnd, GWL_STYLE, style);
        SetWindowPos(widgetHwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        m_attached = false;
        return false;
    }

    SetWindowPos(widgetHwnd, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    m_attached = true;
    return true;
}

void TaskbarDock::RestoreTaskList() {
    if (!m_hasShrunk) return;
    if (m_hTaskList && IsWindow(m_hTaskList) && m_hReBar && IsWindow(m_hReBar)) {
        RECT rcMin = {};
        RECT rcBar = {};
        if (GetWindowRect(m_hTaskList, &rcMin) && GetWindowRect(m_hReBar, &rcBar)) {
            int curW = rcMin.right - rcMin.left;
            int curH = rcMin.bottom - rcMin.top;
            int relX = rcMin.left - rcBar.left;
            int relY = rcMin.top - rcBar.top;
            if (m_horizontal && std::abs(curW - m_expectedMinLen) <= 6) {
                MoveWindow(m_hTaskList, relX, relY, curW + m_appliedShrink, curH, TRUE);
            } else if (!m_horizontal && std::abs(curH - m_expectedMinLen) <= 6) {
                MoveWindow(m_hTaskList, relX, relY, curW, curH + m_appliedShrink, TRUE);
            }
        }
    }
    m_hasShrunk = false;
    m_appliedShrink = 0;
    m_expectedMinLen = 0;
    m_lastBarW = 0;
    m_lastBarH = 0;
}

void TaskbarDock::DetachAndRestore(HWND widgetHwnd) {
    RestoreTaskList();
    if (widgetHwnd && IsWindow(widgetHwnd) && m_attached) {
        SetParent(widgetHwnd, nullptr);
    }
    m_attached = false;
}

bool TaskbarDock::IsTaskbarAlive() const {
    if (!m_hTaskbar || !IsWindow(m_hTaskbar)) return false;
    if (m_attached && m_hParent && !IsWindow(m_hParent)) return false;
    if (m_attached && !m_isWin11 && m_hParent == m_hReBar && m_hTaskList && !IsWindow(m_hTaskList)) return false;
    return true;
}

UINT TaskbarDock::GetCurrentDpi() const {
    return SafeGetDpiForWindow(m_hTaskbar);
}

int TaskbarDock::GetTaskbarThickness() const {
    RECT rc = {};
    if (m_hTaskbar && IsWindow(m_hTaskbar) && GetWindowRect(m_hTaskbar, &rc)) {
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        return (w >= h) ? h : w;
    }
    return ScalePx(40, GetCurrentDpi());
}

bool TaskbarDock::IsHorizontal() const {
    RECT rc = {};
    if (m_hTaskbar && IsWindow(m_hTaskbar) && GetWindowRect(m_hTaskbar, &rc)) {
        return (rc.right - rc.left) >= (rc.bottom - rc.top);
    }
    return true;
}

bool TaskbarDock::UpdateLayout(HWND widgetHwnd, int widgetW, int widgetH, bool forceReposition, DockMetrics& outMetrics) {
    if (!IsTaskbarAlive()) {
        return false;
    }

    RECT rcTaskbar = {};
    if (!GetWindowRect(m_hTaskbar, &rcTaskbar)) {
        return false;
    }

    int tbW = rcTaskbar.right - rcTaskbar.left;
    int tbH = rcTaskbar.bottom - rcTaskbar.top;
    bool horiz = (tbW >= tbH);
    if (horiz != m_horizontal) {
        RestoreTaskList();
        m_horizontal = horiz;
        forceReposition = true;
    }

    UINT dpi = GetCurrentDpi();
    outMetrics.embedded = m_attached;
    outMetrics.isWin11 = m_isWin11;
    outMetrics.horizontal = m_horizontal;
    outMetrics.lightTheme = QueryLightTaskbarTheme();
    outMetrics.dpi = dpi;
    outMetrics.taskbarThickness = m_horizontal ? tbH : tbW;

    bool sizeChanged = (widgetW != m_lastWidgetW || widgetH != m_lastWidgetH);

    // Classic Windows 10 ReBarWindow32 embedding with MSTaskSwWClass shrink
    if (m_attached && !m_isWin11 && m_hParent == m_hReBar && m_hTaskList && IsWindow(m_hTaskList)) {
        RECT rcBar = {};
        RECT rcMin = {};
        if (!GetWindowRect(m_hReBar, &rcBar) || !GetWindowRect(m_hTaskList, &rcMin)) {
            return false;
        }

        int barW = rcBar.right - rcBar.left;
        int barH = rcBar.bottom - rcBar.top;
        int minW = rcMin.right - rcMin.left;
        int minH = rcMin.bottom - rcMin.top;
        int leftSpace = rcMin.left - rcBar.left;
        int topSpace  = rcMin.top - rcBar.top;

        RECT rcWidget = {};
        bool haveWidgetRect = (GetWindowRect(widgetHwnd, &rcWidget) != FALSE);

        if (m_horizontal) {
            bool explorerResetMin = (!m_hasShrunk || std::abs(minW - m_expectedMinLen) > 2);
            bool barResized = (m_lastBarW > 0 && std::abs(barW - m_lastBarW) > 1);
            int fullW = explorerResetMin ? minW : (minW + m_appliedShrink);
            if (!explorerResetMin && barResized) {
                fullW = std::clamp(fullW + (barW - m_lastBarW), 0, std::max(0, barW - leftSpace));
            }

            int rightMargin = ScalePx(4, dpi);
            int desiredShrink = widgetW + rightMargin;
            int minKeep = ScalePx(100, dpi);
            int shrink = std::min(desiredShrink, std::max(0, fullW - minKeep));
            int newMinW = fullW - shrink;

            int wx = leftSpace + newMinW + ScalePx(2, dpi);
            int maxWx = std::max(0, barW - widgetW - ScalePx(2, dpi));
            wx = std::min(wx, maxWx);
            int wy = std::max(0, (barH - widgetH) / 2);

            bool drifted = !haveWidgetRect ||
                std::abs((rcWidget.left - rcBar.left) - wx) > 1 ||
                std::abs((rcWidget.top - rcBar.top) - wy) > 1 ||
                (rcWidget.right - rcWidget.left) != widgetW ||
                (rcWidget.bottom - rcWidget.top) != widgetH;

            if (forceReposition || sizeChanged || explorerResetMin || barResized || drifted || minW != newMinW) {
                if (minW != newMinW) {
                    MoveWindow(m_hTaskList, leftSpace, topSpace, newMinW, minH, TRUE);
                }
                SetWindowPos(widgetHwnd, HWND_TOP, wx, wy, widgetW, widgetH,
                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
                m_hasShrunk = true;
                m_appliedShrink = shrink;
                m_expectedMinLen = newMinW;
                m_lastBarW = barW;
                m_lastBarH = barH;
                m_lastWidgetW = widgetW;
                m_lastWidgetH = widgetH;
            }

            outMetrics.posX = wx;
            outMetrics.posY = wy;
            outMetrics.screenRect = {
                rcBar.left + wx,
                rcBar.top + wy,
                rcBar.left + wx + widgetW,
                rcBar.top + wy + widgetH
            };
        } else {
            bool explorerResetMin = (!m_hasShrunk || std::abs(minH - m_expectedMinLen) > 2);
            bool barResized = (m_lastBarH > 0 && std::abs(barH - m_lastBarH) > 1);
            int fullH = explorerResetMin ? minH : (minH + m_appliedShrink);
            if (!explorerResetMin && barResized) {
                fullH = std::clamp(fullH + (barH - m_lastBarH), 0, std::max(0, barH - topSpace));
            }

            int bottomMargin = ScalePx(4, dpi);
            int desiredShrink = widgetH + bottomMargin;
            int minKeep = ScalePx(100, dpi);
            int shrink = std::min(desiredShrink, std::max(0, fullH - minKeep));
            int newMinH = fullH - shrink;

            int wx = std::max(0, (barW - widgetW) / 2);
            int wy = topSpace + newMinH + ScalePx(2, dpi);
            int maxWy = std::max(0, barH - widgetH - ScalePx(2, dpi));
            wy = std::min(wy, maxWy);

            bool drifted = !haveWidgetRect ||
                std::abs((rcWidget.left - rcBar.left) - wx) > 1 ||
                std::abs((rcWidget.top - rcBar.top) - wy) > 1 ||
                (rcWidget.right - rcWidget.left) != widgetW ||
                (rcWidget.bottom - rcWidget.top) != widgetH;

            if (forceReposition || sizeChanged || explorerResetMin || barResized || drifted || minH != newMinH) {
                if (minH != newMinH) {
                    MoveWindow(m_hTaskList, leftSpace, topSpace, minW, newMinH, TRUE);
                }
                SetWindowPos(widgetHwnd, HWND_TOP, wx, wy, widgetW, widgetH,
                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
                m_hasShrunk = true;
                m_appliedShrink = shrink;
                m_expectedMinLen = newMinH;
                m_lastBarW = barW;
                m_lastBarH = barH;
                m_lastWidgetW = widgetW;
                m_lastWidgetH = widgetH;
            }

            outMetrics.posX = wx;
            outMetrics.posY = wy;
            outMetrics.screenRect = {
                rcBar.left + wx,
                rcBar.top + wy,
                rcBar.left + wx + widgetW,
                rcBar.top + wy + widgetH
            };
        }
        return true;
    }

    // Fallback / Windows 11 placement: immediately before TrayNotifyWnd
    RECT rcTray = {};
    int wx = 0;
    int wy = 0;
    bool haveTray = (m_hTrayNotify && IsWindow(m_hTrayNotify) && GetWindowRect(m_hTrayNotify, &rcTray));
    if (m_horizontal) {
        if (haveTray) {
            int trayRelX = rcTray.left - rcTaskbar.left;
            wx = std::max(ScalePx(4, dpi), trayRelX - widgetW - ScalePx(6, dpi));
        } else {
            wx = std::max(ScalePx(4, dpi), tbW - widgetW - ScalePx(180, dpi));
        }
        wy = std::max(0, (tbH - widgetH) / 2);
    } else {
        wx = std::max(0, (tbW - widgetW) / 2);
        if (haveTray) {
            int trayRelY = rcTray.top - rcTaskbar.top;
            wy = std::max(ScalePx(4, dpi), trayRelY - widgetH - ScalePx(6, dpi));
        } else {
            wy = std::max(ScalePx(4, dpi), tbH - widgetH - ScalePx(180, dpi));
        }
    }

    RECT rcWidget = {};
    bool haveWidgetRect = (GetWindowRect(widgetHwnd, &rcWidget) != FALSE);

    if (m_attached) {
        bool drifted = !haveWidgetRect ||
            std::abs((rcWidget.left - rcTaskbar.left) - wx) > 1 ||
            std::abs((rcWidget.top - rcTaskbar.top) - wy) > 1;
        if (forceReposition || sizeChanged || drifted) {
            SetWindowPos(widgetHwnd, HWND_TOP, wx, wy, widgetW, widgetH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            m_lastWidgetW = widgetW;
            m_lastWidgetH = widgetH;
        }
        outMetrics.posX = wx;
        outMetrics.posY = wy;
        outMetrics.screenRect = {
            rcTaskbar.left + wx,
            rcTaskbar.top + wy,
            rcTaskbar.left + wx + widgetW,
            rcTaskbar.top + wy + widgetH
        };
    } else {
        int sx = rcTaskbar.left + wx;
        int sy = rcTaskbar.top + wy;
        bool drifted = !haveWidgetRect ||
            std::abs(rcWidget.left - sx) > 1 ||
            std::abs(rcWidget.top - sy) > 1;
        if (forceReposition || sizeChanged || drifted) {
            SetWindowPos(widgetHwnd, HWND_TOPMOST, sx, sy, widgetW, widgetH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            m_lastWidgetW = widgetW;
            m_lastWidgetH = widgetH;
        }
        outMetrics.posX = sx;
        outMetrics.posY = sy;
        outMetrics.screenRect = { sx, sy, sx + widgetW, sy + widgetH };
    }
    return true;
}
