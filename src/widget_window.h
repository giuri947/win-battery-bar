#pragma once

#include "battery_model.h"
#include "battery_reader.h"
#include "taskbar_dock.h"
#include "renderer.h"
#include "tooltip_window.h"

#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct LaunchOptions {
    bool demoMode = false;
    int demoPreset = 0;
    int demoHoverIndex = -1;
    bool forceCompact = false;
    bool forceLightTheme = false;
    std::wstring statusFilePath;
};

class WidgetWindow {
public:
    explicit WidgetWindow(const LaunchOptions& opts);
    ~WidgetWindow();

    bool Create(HINSTANCE hInst);
    void Destroy();
    HWND Handle() const { return m_hwnd; }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);

    void StartWorkerThread();
    void StopWorkerThread();
    void TriggerScanNow(bool showFlash = false);
    void WorkerLoop();

    void ApplyFilteredDevices();
    void UpdateDockAndRedraw(bool forceReposition);
    void RedrawOnly();
    void EnableColorKeyFallback();
    void UpdateTooltipForHover();
    int HitTestTile(int clientX, int clientY) const;
    void ShowContextMenu(POINT screenPt);
    void CopyDiagnosticsToClipboard();
    void WriteStatusFile();

    LaunchOptions m_opts;
    HINSTANCE m_hInst = nullptr;
    HWND m_hwnd = nullptr;
    UINT m_taskbarCreatedMsg = 0;

    AppSettings m_settings;
    TaskbarDock m_dock;
    TooltipWindow m_tooltip;
    DockMetrics m_metrics;
    WidgetLayout m_layout;

    std::vector<BatteryDevice> m_allDevices;
    std::vector<BatteryDevice> m_visibleDevices;
    std::wstring m_lastDiagnostics;

    int m_hoveredIndex = -1;
    bool m_trackingMouse = false;
    bool m_refreshFlash = false;
    bool m_useColorKeyFallback = false;
    bool m_lastRenderOk = false;
    DWORD m_lastRenderErr = 0;

    // Background polling worker
    std::thread m_worker;
    std::mutex m_workerMu;
    std::condition_variable m_workerCv;
    bool m_stopWorker = false;
    bool m_scanRequested = true;

    std::mutex m_resultMu;
    ScanResult m_pendingResult;
};
