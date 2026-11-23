#pragma once

#include <windows.h>

struct DockMetrics {
    bool embedded = false;
    bool isWin11 = false;
    bool horizontal = true;
    bool lightTheme = false;
    UINT dpi = 96;
    int taskbarThickness = 40; // Height if horizontal, width if vertical
    int posX = 0;              // Parent-client X (or screen X if fallback)
    int posY = 0;              // Parent-client Y (or screen Y if fallback)
    RECT screenRect = {};      // Computed screen coordinates of our widget
};

class TaskbarDock {
public:
    TaskbarDock() = default;
    ~TaskbarDock();

    bool Attach(HWND widgetHwnd);
    void DetachAndRestore(HWND widgetHwnd);

    // Re-check taskbar layout & position widget cleanly to the left of TrayNotifyWnd.
    // Returns true if taskbar is alive and position is valid; false if Explorer restarted (caller should re-Attach).
    bool UpdateLayout(HWND widgetHwnd, int widgetW, int widgetH, bool forceReposition, DockMetrics& outMetrics);

    bool IsAttached() const { return m_attached; }
    bool IsTaskbarAlive() const;
    UINT GetCurrentDpi() const;
    int GetTaskbarThickness() const;
    bool IsHorizontal() const;
    static bool QueryLightTaskbarTheme();

private:
    bool FindHandles();
    void RestoreTaskList();

    HWND m_hTaskbar = nullptr;
    HWND m_hReBar = nullptr;
    HWND m_hTaskList = nullptr;
    HWND m_hTrayNotify = nullptr;
    HWND m_hParent = nullptr;

    bool m_attached = false;
    bool m_isWin11 = false;
    bool m_horizontal = true;

    bool m_hasShrunk = false;
    int m_appliedShrink = 0;
    int m_expectedMinLen = 0;
    int m_lastWidgetW = 0;
    int m_lastWidgetH = 0;
};
