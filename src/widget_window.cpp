#include "widget_window.h"
#include "battery_reader.h"
#include "settings.h"

#include <dbt.h>
#include <winrt/base.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

constexpr const wchar_t* kHostClass   = L"WinBatteryBarHostWnd";
constexpr const wchar_t* kWidgetClass = L"WinBatteryBarTaskbarWidget";

constexpr UINT WM_APP_SCAN_DONE = WM_APP + 101;
constexpr UINT_PTR kTimerLayout     = 1;
constexpr UINT_PTR kTimerFlashEnd   = 2;
constexpr UINT_PTR kTimerDebounce   = 3;
constexpr UINT_PTR kTimerNetwork    = 4;

constexpr UINT kCmdOpenSettings     = 1000;
constexpr UINT kCmdRefresh          = 1001;
constexpr UINT kCmdShowLaptop       = 1002;
constexpr UINT kCmdShowDisconnected = 1003;
constexpr UINT kCmdShowNetwork      = 1004;
constexpr UINT kCmdStyleFull        = 1010;
constexpr UINT kCmdStyleCompact     = 1011;
constexpr UINT kCmdStyleUltra       = 1012;
constexpr UINT kCmdAutoStart        = 1015;
constexpr UINT kCmdCopyDiagnostics  = 1016;
constexpr UINT kCmdExit             = 1017;
constexpr UINT kCmdToggleDeviceBase = 2000;

} // namespace

WidgetWindow::WidgetWindow(const LaunchOptions& opts)
    : m_opts(opts)
{
    if (m_opts.demoMode) {
        // Keep demo launches isolated from persisted user Registry settings
        m_settings = AppSettings{};
        if (m_opts.demoPreset == 2) {
            m_settings.showLaptop = false;
        }
    } else {
        m_settings = SettingsStore::Load();
    }
    if (m_opts.forceStyle.has_value()) {
        m_settings.displayStyle = *m_opts.forceStyle;
    }
    if (m_opts.forceShowNetwork) {
        m_settings.showNetwork = true;
    }
    m_pollIntervalSec.store(std::clamp(m_settings.pollIntervalSec, 5, 300));
}

WidgetWindow::~WidgetWindow() {
    Destroy();
}

void WidgetWindow::SaveSettingsIfAllowed() {
    if (!m_opts.demoMode) {
        SettingsStore::Save(m_settings);
    }
}

void WidgetWindow::SyncNetworkTimer() {
    if (!m_hostHwnd || !IsWindow(m_hostHwnd)) return;
    if (m_settings.showNetwork) {
        if (m_opts.demoMode) {
            m_netStats = NetworkMonitor::BuildDemoStats();
            KillTimer(m_hostHwnd, kTimerNetwork);
        } else {
            m_netStats = m_netMonitor.Sample();
            SetTimer(m_hostHwnd, kTimerNetwork, 1000, nullptr);
        }
    } else {
        KillTimer(m_hostHwnd, kTimerNetwork);
        m_netMonitor.Reset();
        m_netStats = NetworkStats{};
    }
}

void WidgetWindow::ApplyNewSettings(const AppSettings& updated) {
    bool pollChanged = (updated.pollIntervalSec != m_settings.pollIntervalSec);
    bool netChanged  = (updated.showNetwork != m_settings.showNetwork);
    m_settings = updated;
    m_pollIntervalSec.store(std::clamp(m_settings.pollIntervalSec, 5, 300));
    SaveSettingsIfAllowed();

    if (netChanged) {
        SyncNetworkTimer();
    }
    if (pollChanged && !m_opts.demoMode) {
        m_workerCv.notify_one();
    }

    ApplyFilteredDevices();
    UpdateDockAndRedraw(true);
    if (m_settingsPanel.IsVisible()) {
        bool light = m_opts.forceLightTheme ? true : m_metrics.lightTheme;
        m_settingsPanel.UpdateState(m_settings, m_allDevices, m_metrics.dpi, light);
    }
    WriteStatusFile();
}

void WidgetWindow::OpenSettingsPanel(bool keepOpenForDemo) {
    m_tooltip.Hide();
    if (m_hoveredTile != -1) {
        m_hoveredTile = -1;
        m_trackingMouse = false;
        RedrawOnly();
    }

    if (m_settingsPanel.IsVisible() && !keepOpenForDemo) {
        m_settingsPanel.Hide();
        return;
    }

    bool light = m_opts.forceLightTheme ? true : m_metrics.lightTheme;
    SettingsPanelCallbacks cb;
    cb.onSettingsChanged = [this](const AppSettings& next) {
        ApplyNewSettings(next);
    };
    cb.onToggleAutoStart = [this](bool enable) {
        if (!m_opts.demoMode && SettingsStore::SetAutoStartEnabled(enable)) {
            m_settings.autoStart = enable;
            SaveSettingsIfAllowed();
            bool l = m_opts.forceLightTheme ? true : m_metrics.lightTheme;
            m_settingsPanel.UpdateState(m_settings, m_allDevices, m_metrics.dpi, l);
        }
    };
    cb.onRefreshNow = [this]() {
        TriggerScanNow(true);
    };
    cb.onCopyDiagnostics = [this]() {
        CopyDiagnosticsToClipboard();
    };
    cb.onExitApp = [this]() {
        PostQuitMessage(0);
    };

    m_settingsPanel.Show(
        m_settings,
        m_allDevices,
        m_metrics.screenRect,
        m_metrics.dpi,
        light,
        m_opts.demoMode,
        keepOpenForDemo,
        std::move(cb));
}

bool WidgetWindow::CreateWidgetHwnd() {
    if (m_hwnd && IsWindow(m_hwnd)) {
        return true;
    }
    m_useColorKeyFallback = false;
    m_lastRenderOk = false;
    m_lastRenderErr = 0;
    m_trackingMouse = false;
    m_hoveredTile = -1;

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kWidgetClass,
        L"WinBatteryBar",
        WS_POPUP,
        0, 0, 80, 28,
        nullptr, nullptr, m_hInst, this);
    if (!m_hwnd) {
        return false;
    }

    m_dock.Attach(m_hwnd);
    return true;
}

bool WidgetWindow::Create(HINSTANCE hInst) {
    m_hInst = hInst;
    m_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    // 1. Hidden top-level host window so we always receive system broadcasts
    //    (TaskbarCreated, WM_POWERBROADCAST, WM_DEVICECHANGE, WM_SETTINGCHANGE, WM_ENDSESSION)
    //    and stay alive if explorer.exe restarts and destroys child windows of ReBarWindow32.
    WNDCLASSEXW hostWc = {};
    hostWc.cbSize = sizeof(hostWc);
    hostWc.lpfnWndProc = StaticHostWndProc;
    hostWc.hInstance = hInst;
    hostWc.lpszClassName = kHostClass;
    RegisterClassExW(&hostWc);

    m_hostHwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kHostClass,
        L"WinBatteryBarHost",
        WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, hInst, this);
    if (!m_hostHwnd) {
        return false;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWidgetClass;
    RegisterClassExW(&wc);

    m_tooltip.Create(hInst);
    m_settingsPanel.Create(hInst);

    if (!CreateWidgetHwnd()) {
        return false;
    }

    if (m_opts.demoMode) {
        m_allDevices = BatteryReader::BuildDemoDevices(m_opts.demoPreset);
        m_lastDiagnostics = L"Demo mode active (preset " + std::to_wstring(m_opts.demoPreset) + L")";
        if (m_settings.showNetwork) {
            m_netStats = NetworkMonitor::BuildDemoStats();
        }
        ApplyFilteredDevices();
    } else {
        // Immediate fast laptop read for instant first frame before BT scan finishes
        SYSTEM_POWER_STATUS sps = {};
        if (m_settings.showLaptop && GetSystemPowerStatus(&sps) &&
            (sps.BatteryFlag & 128) == 0 && sps.BatteryLifePercent <= 100)
        {
            BatteryDevice laptop;
            laptop.id = L"laptop";
            laptop.name = L"Laptop Battery";
            laptop.kind = DeviceKind::Laptop;
            laptop.percent = static_cast<int>(sps.BatteryLifePercent);
            laptop.connected = true;
            laptop.pluggedIn = (sps.ACLineStatus == 1);
            laptop.charging = ((sps.BatteryFlag & 8) != 0);
            laptop.sourceLabel = laptop.pluggedIn ? L"AC Power" : L"On Battery";
            laptop.extraStatus = laptop.charging ? L"Plugged in, charging" : L"Scanning Bluetooth...";
            laptop.updatedTickMs = GetTickCount64();
            m_allDevices.push_back(std::move(laptop));
            ApplyFilteredDevices();
        }
        SyncNetworkTimer();
        StartWorkerThread();
    }

    UpdateDockAndRedraw(true);

    // Optional scripted hover or settings-panel preview for automated E2E visual testing
    if (m_opts.demoMode && m_opts.demoHoverIndex >= 0 &&
        m_opts.demoHoverIndex < static_cast<int>(m_layout.tiles.size()))
    {
        m_hoveredTile = m_opts.demoHoverIndex;
        RedrawOnly();
        UpdateTooltipForHover();
    }
    if (m_opts.demoMode && m_opts.demoOpenSettings) {
        OpenSettingsPanel(true);
    }

    WriteStatusFile();

    // Periodic taskbar layout guard on the persistent top-level host window
    SetTimer(m_hostHwnd, kTimerLayout, 400, nullptr);
    return true;
}

void WidgetWindow::Destroy() {
    StopWorkerThread();
    m_settingsPanel.Destroy();
    m_tooltip.Destroy();
    if (m_hostHwnd && IsWindow(m_hostHwnd)) {
        KillTimer(m_hostHwnd, kTimerLayout);
        KillTimer(m_hostHwnd, kTimerFlashEnd);
        KillTimer(m_hostHwnd, kTimerDebounce);
        KillTimer(m_hostHwnd, kTimerNetwork);
    }
    if (m_hwnd && IsWindow(m_hwnd)) {
        m_dock.DetachAndRestore(m_hwnd);
        DestroyWindow(m_hwnd);
    } else {
        m_dock.DetachAndRestore(nullptr);
    }
    m_hwnd = nullptr;

    if (m_hostHwnd && IsWindow(m_hostHwnd)) {
        DestroyWindow(m_hostHwnd);
    }
    m_hostHwnd = nullptr;
}

void WidgetWindow::StartWorkerThread() {
    m_stopWorker = false;
    m_scanRequested = true;
    m_worker = std::thread(&WidgetWindow::WorkerLoop, this);
}

void WidgetWindow::StopWorkerThread() {
    {
        std::lock_guard<std::mutex> lk(m_workerMu);
        m_stopWorker = true;
    }
    m_workerCv.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void WidgetWindow::TriggerScanNow(bool showFlash) {
    if (showFlash && m_hwnd && m_hostHwnd) {
        m_refreshFlash = true;
        RedrawOnly();
        SetTimer(m_hostHwnd, kTimerFlashEnd, 180, nullptr);
    }
    if (m_opts.demoMode) return;
    if (m_settings.showNetwork) {
        m_netStats = m_netMonitor.Sample();
        RedrawOnly();
    }
    {
        std::lock_guard<std::mutex> lk(m_workerMu);
        m_scanRequested = true;
    }
    m_workerCv.notify_one();
}

void WidgetWindow::WorkerLoop() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
        // Apartment may already be initialized on this thread
    }

    HWND hostTarget = m_hostHwnd;

    while (true) {
        {
            std::unique_lock<std::mutex> lk(m_workerMu);
            int interval = std::clamp(m_pollIntervalSec.load(), 5, 300);
            m_workerCv.wait_for(lk, std::chrono::seconds(interval), [&]() {
                return m_stopWorker || m_scanRequested;
            });
            if (m_stopWorker) break;
            m_scanRequested = false;
        }

        // Always scan with includeLaptop=true & includeDisconnected=true so UI toggles respond instantly
        ScanResult scan = BatteryReader::ScanAll(true, true);

        {
            std::lock_guard<std::mutex> lk(m_resultMu);
            m_pendingResult = std::move(scan);
        }

        if (hostTarget && IsWindow(hostTarget)) {
            PostMessageW(hostTarget, WM_APP_SCAN_DONE, 0, 0);
        }
    }

    try {
        winrt::uninit_apartment();
    } catch (...) {}
}

void WidgetWindow::ApplyFilteredDevices() {
    m_visibleDevices.clear();
    for (const auto& orig : m_allDevices) {
        if (orig.kind == DeviceKind::Laptop && !m_settings.showLaptop) {
            continue;
        }
        if (!orig.connected && !m_settings.showDisconnected) {
            continue;
        }
        if (SettingsStore::IsDeviceHidden(m_settings, orig.id)) {
            continue;
        }
        BatteryDevice d = orig;
        if (auto ov = SettingsStore::GetIconOverride(m_settings, d.id)) {
            d.kind = *ov;
        }
        m_visibleDevices.push_back(std::move(d));
    }

    if (m_hoveredTile >= static_cast<int>(m_layout.tiles.size())) {
        m_hoveredTile = -1;
        m_tooltip.Hide();
    }
}

void WidgetWindow::UpdateDockAndRedraw(bool forceReposition) {
    // If Explorer restarted and destroyed our child window, wait for new taskbar before recreating
    if (!m_hwnd || !IsWindow(m_hwnd)) {
        m_hwnd = nullptr;
        if (!FindWindowW(L"Shell_TrayWnd", nullptr)) {
            return;
        }
        if (!CreateWidgetHwnd()) {
            return;
        }
        forceReposition = true;
    }

    if (!m_dock.IsTaskbarAlive()) {
        if (!m_dock.Attach(m_hwnd)) {
            return;
        }
        forceReposition = true;
    }

    UINT dpi = m_dock.GetCurrentDpi();
    int thickness = m_dock.GetTaskbarThickness();
    bool horiz = m_dock.IsHorizontal();

    WidgetLayout newLayout = Renderer::ComputeLayout(
        m_visibleDevices, dpi, thickness, horiz, m_settings.displayStyle, m_settings.showNetwork);

    bool sizeChanged = (newLayout.width != m_layout.width || newLayout.height != m_layout.height);
    m_layout = std::move(newLayout);
    if (m_hoveredTile >= static_cast<int>(m_layout.tiles.size())) {
        m_hoveredTile = -1;
        m_tooltip.Hide();
    }

    DockMetrics newMetrics;
    if (!m_dock.UpdateLayout(m_hwnd, m_layout.width, m_layout.height, forceReposition || sizeChanged, newMetrics)) {
        return;
    }

    bool themeChanged = (newMetrics.lightTheme != m_metrics.lightTheme);
    bool dpiChanged = (newMetrics.dpi != m_metrics.dpi);
    bool rectChanged = (newMetrics.screenRect.left != m_metrics.screenRect.left ||
                        newMetrics.screenRect.top != m_metrics.screenRect.top ||
                        newMetrics.screenRect.right != m_metrics.screenRect.right ||
                        newMetrics.screenRect.bottom != m_metrics.screenRect.bottom);
    m_metrics = newMetrics;
    if (m_opts.forceLightTheme) {
        m_metrics.lightTheme = true;
    }

    if (forceReposition || sizeChanged || themeChanged || dpiChanged || !m_lastRenderOk) {
        RedrawOnly();
    }
    if (m_tooltip.IsVisible() && (rectChanged || sizeChanged || themeChanged || dpiChanged)) {
        UpdateTooltipForHover();
    }
    if (m_settingsPanel.IsVisible() && (themeChanged || dpiChanged)) {
        bool light = m_opts.forceLightTheme ? true : m_metrics.lightTheme;
        m_settingsPanel.UpdateState(m_settings, m_allDevices, m_metrics.dpi, light);
    }
}

void WidgetWindow::EnableColorKeyFallback() {
    if (!m_hwnd || !IsWindow(m_hwnd) || m_useColorKeyFallback) return;
    LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex & ~static_cast<LONG_PTR>(WS_EX_LAYERED));
    SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    SetLayeredWindowAttributes(m_hwnd, Renderer::kColorKeyRgb, 0, LWA_COLORKEY);
    m_useColorKeyFallback = true;
}

void WidgetWindow::RedrawOnly() {
    if (!m_hwnd || !IsWindow(m_hwnd)) return;

    RenderParams rp;
    rp.style = m_settings.displayStyle;
    rp.lowBatteryThreshold = m_settings.lowBatteryThreshold;
    rp.showNetwork = m_settings.showNetwork;
    rp.netUnits = m_settings.netUnits;
    rp.netStats = m_netStats;
    rp.dpi = m_metrics.dpi;
    rp.lightTaskbar = m_opts.forceLightTheme ? true : m_metrics.lightTheme;
    rp.hoveredTileIndex = m_hoveredTile;
    rp.refreshFlash = m_refreshFlash;

    if (!m_useColorKeyFallback) {
        SetLastError(0);
        m_lastRenderOk = Renderer::RenderToLayeredWindow(
            m_hwnd, m_visibleDevices, m_layout, rp, nullptr);
        m_lastRenderErr = m_lastRenderOk ? 0 : GetLastError();
        if (m_lastRenderOk) {
            return;
        }
        // Automatic fallback if UpdateLayeredWindow rejects cross-process WS_CHILD on this OS/driver
        EnableColorKeyFallback();
    }

    InvalidateRect(m_hwnd, nullptr, FALSE);
    UpdateWindow(m_hwnd);
    m_lastRenderOk = true;
}

int WidgetWindow::HitTestTile(int clientX, int clientY) const {
    POINT pt = { clientX, clientY };
    for (size_t i = 0; i < m_layout.tiles.size(); ++i) {
        if (PtInRect(&m_layout.tiles[i].rect, pt)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void WidgetWindow::UpdateTooltipForHover() {
    if (m_settingsPanel.IsVisible()) {
        m_tooltip.Hide();
        return;
    }
    if (m_hoveredTile < 0 || m_hoveredTile >= static_cast<int>(m_layout.tiles.size())) {
        m_tooltip.Hide();
        return;
    }

    const auto& box = m_layout.tiles[static_cast<size_t>(m_hoveredTile)];
    RECT tileLocal = box.rect;
    RECT anchorScreen = {
        m_metrics.screenRect.left + tileLocal.left,
        m_metrics.screenRect.top + tileLocal.top,
        m_metrics.screenRect.left + tileLocal.right,
        m_metrics.screenRect.top + tileLocal.bottom
    };

    bool light = m_opts.forceLightTheme ? true : m_metrics.lightTheme;
    if (box.isNetworkTile) {
        m_tooltip.ShowForNetwork(m_netStats, m_settings.netUnits, anchorScreen, m_metrics.dpi, light, m_metrics.horizontal);
    } else if (box.deviceIndex < 0 || static_cast<size_t>(box.deviceIndex) >= m_visibleDevices.size()) {
        m_tooltip.ShowEmpty(anchorScreen, m_metrics.dpi, light, m_metrics.horizontal);
    } else {
        m_tooltip.ShowForDevice(
            m_visibleDevices[static_cast<size_t>(box.deviceIndex)],
            anchorScreen, m_metrics.dpi, light, m_metrics.horizontal,
            m_settings.lowBatteryThreshold);
    }
}

void WidgetWindow::ShowContextMenu(POINT screenPt) {
    m_tooltip.Hide();
    m_settingsPanel.Hide();
    if (m_hoveredTile != -1) {
        m_hoveredTile = -1;
        m_trackingMouse = false;
        RedrawOnly();
    }

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, kCmdOpenSettings, L"Settings...");
    SetMenuDefaultItem(menu, kCmdOpenSettings, FALSE);
    AppendMenuW(menu, MF_STRING, kCmdRefresh, L"Refresh Now");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // Display Style submenu
    HMENU styleMenu = CreatePopupMenu();
    if (styleMenu) {
        AppendMenuW(styleMenu, MF_STRING | (m_settings.displayStyle == DisplayStyle::Full ? MF_CHECKED : MF_UNCHECKED),
            kCmdStyleFull, L"Full Capsules");
        AppendMenuW(styleMenu, MF_STRING | (m_settings.displayStyle == DisplayStyle::Compact ? MF_CHECKED : MF_UNCHECKED),
            kCmdStyleCompact, L"Compact Capsules");
        AppendMenuW(styleMenu, MF_STRING | (m_settings.displayStyle == DisplayStyle::UltraCompact ? MF_CHECKED : MF_UNCHECKED),
            kCmdStyleUltra, L"Ultra-Compact (Icon + Bar)");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(styleMenu), L"Display Style");
    }

    AppendMenuW(menu, MF_STRING | (m_settings.showLaptop ? MF_CHECKED : MF_UNCHECKED),
        kCmdShowLaptop, L"Show Laptop Battery");
    AppendMenuW(menu, MF_STRING | (m_settings.showNetwork ? MF_CHECKED : MF_UNCHECKED),
        kCmdShowNetwork, L"Show Network Activity (\x2193/\x2191)");
    AppendMenuW(menu, MF_STRING | (m_settings.showDisconnected ? MF_CHECKED : MF_UNCHECKED),
        kCmdShowDisconnected, L"Show Disconnected Devices");

    // Per-device visibility submenu
    HMENU devMenu = CreatePopupMenu();
    if (devMenu) {
        if (m_allDevices.empty()) {
            AppendMenuW(devMenu, MF_STRING | MF_GRAYED, 0, L"(No battery devices detected)");
        } else {
            for (size_t i = 0; i < m_allDevices.size() && i < 32; ++i) {
                const auto& d = m_allDevices[i];
                std::wstring pctStr = (d.percent >= 0) ? (std::to_wstring(d.percent) + L"%") : L"--";
                std::wstring item = d.name + L" (" + pctStr + L")";
                if (!d.connected) item += L" [Offline]";
                bool shown = !SettingsStore::IsDeviceHidden(m_settings, d.id);
                AppendMenuW(devMenu, MF_STRING | (shown ? MF_CHECKED : MF_UNCHECKED),
                    kCmdToggleDeviceBase + static_cast<UINT>(i), item.c_str());
            }
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(devMenu), L"Devices");
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    UINT autoStartFlags = MF_STRING | (m_settings.autoStart ? MF_CHECKED : MF_UNCHECKED);
    if (m_opts.demoMode) autoStartFlags |= MF_GRAYED;
    AppendMenuW(menu, autoStartFlags, kCmdAutoStart, L"Start with Windows");
    AppendMenuW(menu, MF_STRING, kCmdCopyDiagnostics, L"Copy Diagnostics");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"Exit WinBatteryBar");

    // Choose menu alignment based on taskbar screen position so it never opens off-screen
    UINT alignFlags = TPM_BOTTOMALIGN | TPM_LEFTALIGN;
    HMONITOR mon = MonitorFromPoint(screenPt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        int midY = (mi.rcMonitor.top + mi.rcMonitor.bottom) / 2;
        if (screenPt.y < midY) {
            alignFlags = TPM_TOPALIGN | TPM_LEFTALIGN;
        }
    }

    // Use top-level m_hostHwnd so SetForegroundWindow succeeds and outside clicks dismiss cleanly
    HWND owner = (m_hostHwnd && IsWindow(m_hostHwnd)) ? m_hostHwnd : m_hwnd;
    SetForegroundWindow(owner);
    UINT cmd = TrackPopupMenuEx(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | alignFlags,
        screenPt.x, screenPt.y,
        owner,
        nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);

    DestroyMenu(menu);

    if (cmd == 0) return;

    if (cmd == kCmdOpenSettings) {
        OpenSettingsPanel(false);
    } else if (cmd == kCmdRefresh) {
        TriggerScanNow(true);
    } else if (cmd == kCmdStyleFull) {
        AppSettings next = m_settings;
        next.displayStyle = DisplayStyle::Full;
        ApplyNewSettings(next);
    } else if (cmd == kCmdStyleCompact) {
        AppSettings next = m_settings;
        next.displayStyle = DisplayStyle::Compact;
        ApplyNewSettings(next);
    } else if (cmd == kCmdStyleUltra) {
        AppSettings next = m_settings;
        next.displayStyle = DisplayStyle::UltraCompact;
        ApplyNewSettings(next);
    } else if (cmd == kCmdShowLaptop) {
        AppSettings next = m_settings;
        next.showLaptop = !next.showLaptop;
        ApplyNewSettings(next);
    } else if (cmd == kCmdShowNetwork) {
        AppSettings next = m_settings;
        next.showNetwork = !next.showNetwork;
        ApplyNewSettings(next);
    } else if (cmd == kCmdShowDisconnected) {
        AppSettings next = m_settings;
        next.showDisconnected = !next.showDisconnected;
        ApplyNewSettings(next);
    } else if (cmd == kCmdAutoStart) {
        if (!m_opts.demoMode) {
            bool target = !m_settings.autoStart;
            if (SettingsStore::SetAutoStartEnabled(target)) {
                m_settings.autoStart = target;
                SaveSettingsIfAllowed();
            }
        }
    } else if (cmd == kCmdCopyDiagnostics) {
        CopyDiagnosticsToClipboard();
    } else if (cmd == kCmdExit) {
        PostQuitMessage(0);
    } else if (cmd >= kCmdToggleDeviceBase && cmd < kCmdToggleDeviceBase + 32) {
        size_t idx = static_cast<size_t>(cmd - kCmdToggleDeviceBase);
        if (idx < m_allDevices.size()) {
            AppSettings next = m_settings;
            SettingsStore::ToggleDeviceHidden(next, m_allDevices[idx].id);
            ApplyNewSettings(next);
        }
    }
}

void WidgetWindow::CopyDiagnosticsToClipboard() {
    std::wstringstream ss;
    ss << L"WinBatteryBar Diagnostics\r\n"
       << L"Embedded: " << (m_metrics.embedded ? L"yes" : L"no")
       << L" | Win11: " << (m_metrics.isWin11 ? L"yes" : L"no")
       << L" | DPI: " << m_metrics.dpi
       << L" | LightTheme: " << (m_metrics.lightTheme ? L"yes" : L"no")
       << L" | Style: " << DisplayStyleLabel(m_settings.displayStyle)
       << L" | Render: " << (m_useColorKeyFallback ? L"colorkey" : L"ulw") << L"\r\n"
       << L"LowThreshold: " << m_settings.lowBatteryThreshold << L"%"
       << L" | Network: " << (m_settings.showNetwork ? (L"on (" + m_netStats.adapterSummary + L")") : L"off") << L"\r\n"
       << L"Visible devices: " << m_visibleDevices.size() << L" / " << m_allDevices.size() << L"\r\n\r\n"
       << m_lastDiagnostics;

    std::wstring text = ss.str();
    HWND clipOwner = (m_hostHwnd && IsWindow(m_hostHwnd)) ? m_hostHwnd : m_hwnd;
    if (OpenClipboard(clipOwner)) {
        EmptyClipboard();
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            void* ptr = GlobalLock(hMem);
            if (ptr) {
                memcpy(ptr, text.c_str(), bytes);
                GlobalUnlock(hMem);
                if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
                    GlobalFree(hMem);
                }
            } else {
                GlobalFree(hMem);
            }
        }
        CloseClipboard();
    }
}

void WidgetWindow::WriteStatusFile() {
    if (m_opts.statusFilePath.empty()) return;

    const char* styleStr = "full";
    if (m_settings.displayStyle == DisplayStyle::Compact) styleStr = "compact";
    else if (m_settings.displayStyle == DisplayStyle::UltraCompact) styleStr = "ultra";

    std::ostringstream json;
    json << "{\n"
         << "  \"embedded\": " << (m_metrics.embedded ? "true" : "false") << ",\n"
         << "  \"isWin11\": " << (m_metrics.isWin11 ? "true" : "false") << ",\n"
         << "  \"horizontal\": " << (m_metrics.horizontal ? "true" : "false") << ",\n"
         << "  \"lightTheme\": " << (m_metrics.lightTheme ? "true" : "false") << ",\n"
         << "  \"dpi\": " << m_metrics.dpi << ",\n"
         << "  \"taskbarThickness\": " << m_metrics.taskbarThickness << ",\n"
         << "  \"displayStyle\": \"" << styleStr << "\",\n"
         << "  \"showNetwork\": " << (m_settings.showNetwork ? "true" : "false") << ",\n"
         << "  \"renderMode\": \"" << (m_useColorKeyFallback ? "colorkey" : "ulw") << "\",\n"
         << "  \"renderOk\": " << (m_lastRenderOk ? "true" : "false") << ",\n"
         << "  \"renderError\": " << m_lastRenderErr << ",\n"
         << "  \"width\": " << m_layout.width << ",\n"
         << "  \"height\": " << m_layout.height << ",\n"
         << "  \"screenRect\": [" << m_metrics.screenRect.left << ", "
         << m_metrics.screenRect.top << ", "
         << m_metrics.screenRect.right << ", "
         << m_metrics.screenRect.bottom << "],\n"
         << "  \"deviceCount\": " << m_visibleDevices.size() << ",\n"
         << "  \"tileCount\": " << m_layout.tiles.size() << ",\n"
         << "  \"tooltipVisible\": " << (m_tooltip.IsVisible() ? "true" : "false") << ",\n"
         << "  \"settingsVisible\": " << (m_settingsPanel.IsVisible() ? "true" : "false") << "\n"
         << "}\n";

    std::ofstream ofs(std::filesystem::path(m_opts.statusFilePath), std::ios::binary | std::ios::trunc);
    if (ofs) {
        ofs << json.str();
    }
}

LRESULT CALLBACK WidgetWindow::StaticHostWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WidgetWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<WidgetWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hostHwnd = hwnd;
    } else {
        self = reinterpret_cast<WidgetWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HostWndProc(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT WidgetWindow::HostWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (m_taskbarCreatedMsg != 0 && msg == m_taskbarCreatedMsg) {
        // Explorer restarted: recreate child if destroyed, otherwise re-attach cleanly to the new taskbar
        if (!m_hwnd || !IsWindow(m_hwnd)) {
            m_hwnd = nullptr;
            CreateWidgetHwnd();
        } else {
            m_dock.Attach(m_hwnd);
        }
        UpdateDockAndRedraw(true);
        return 0;
    }

    switch (msg) {
    case WM_TIMER:
        if (wp == kTimerLayout) {
            UpdateDockAndRedraw(false);
            WriteStatusFile();
        } else if (wp == kTimerFlashEnd) {
            KillTimer(hwnd, kTimerFlashEnd);
            m_refreshFlash = false;
            RedrawOnly();
        } else if (wp == kTimerDebounce) {
            KillTimer(hwnd, kTimerDebounce);
            TriggerScanNow(false);
        } else if (wp == kTimerNetwork) {
            if (m_settings.showNetwork && !m_opts.demoMode) {
                m_netStats = m_netMonitor.Sample();
                RedrawOnly();
                if (m_tooltip.IsVisible() && m_hoveredTile >= 0 &&
                    m_hoveredTile < static_cast<int>(m_layout.tiles.size()) &&
                    m_layout.tiles[static_cast<size_t>(m_hoveredTile)].isNetworkTile)
                {
                    UpdateTooltipForHover();
                }
            }
        }
        return 0;

    case WM_APP_SCAN_DONE: {
        ScanResult res;
        {
            std::lock_guard<std::mutex> lk(m_resultMu);
            res = std::move(m_pendingResult);
        }
        m_allDevices = std::move(res.devices);
        m_lastDiagnostics = std::move(res.diagnostics);
        ApplyFilteredDevices();
        UpdateDockAndRedraw(true);
        if (m_hoveredTile >= 0) {
            UpdateTooltipForHover();
        }
        if (m_settingsPanel.IsVisible()) {
            bool light = m_opts.forceLightTheme ? true : m_metrics.lightTheme;
            m_settingsPanel.UpdateState(m_settings, m_allDevices, m_metrics.dpi, light);
        }
        WriteStatusFile();
        return 0;
    }

    case WM_POWERBROADCAST:
        if (wp == PBT_APMPOWERSTATUSCHANGE || wp == PBT_APMRESUMEAUTOMATIC) {
            SetTimer(hwnd, kTimerDebounce, 500, nullptr);
        }
        return TRUE;

    case WM_DEVICECHANGE:
        if (wp == DBT_DEVNODES_CHANGED || wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE) {
            SetTimer(hwnd, kTimerDebounce, 1200, nullptr);
        }
        return TRUE;

    case WM_SETTINGCHANGE:
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        UpdateDockAndRedraw(true);
        return 0;

    case WM_ENDSESSION:
        if (wp) {
            m_dock.DetachAndRestore(m_hwnd);
        }
        return 0;

    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (m_hostHwnd == hwnd) {
            m_hostHwnd = nullptr;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK WidgetWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WidgetWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<WidgetWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<WidgetWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->WndProc(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT WidgetWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        if (m_useColorKeyFallback && hdc) {
            RenderParams rp;
            rp.style = m_settings.displayStyle;
            rp.lowBatteryThreshold = m_settings.lowBatteryThreshold;
            rp.showNetwork = m_settings.showNetwork;
            rp.netUnits = m_settings.netUnits;
            rp.netStats = m_netStats;
            rp.dpi = m_metrics.dpi;
            rp.lightTaskbar = m_opts.forceLightTheme ? true : m_metrics.lightTheme;
            rp.hoveredTileIndex = m_hoveredTile;
            rp.refreshFlash = m_refreshFlash;
            Renderer::RenderToPaintDC(hdc, m_visibleDevices, m_layout, rp);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

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
        int hit = HitTestTile(x, y);
        if (hit != m_hoveredTile) {
            m_hoveredTile = hit;
            RedrawOnly();
            UpdateTooltipForHover();
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        m_trackingMouse = false;
        if (m_hoveredTile != -1) {
            m_hoveredTile = -1;
            RedrawOnly();
            m_tooltip.Hide();
        }
        return 0;

    case WM_LBUTTONUP:
        if (m_ignoreNextLButtonUp) {
            m_ignoreNextLButtonUp = false;
            return 0;
        }
        if (m_settingsPanel.IsVisible()) {
            m_settingsPanel.Hide();
        } else {
            TriggerScanNow(true);
        }
        return 0;

    case WM_LBUTTONDBLCLK:
        m_ignoreNextLButtonUp = true;
        OpenSettingsPanel(false);
        return 0;

    case WM_RBUTTONUP: {
        POINT pt = {};
        GetCursorPos(&pt);
        ShowContextMenu(pt);
        return 0;
    }

    case WM_DPICHANGED:
        UpdateDockAndRedraw(true);
        return 0;

    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;

    case WM_DESTROY:
        m_tooltip.Hide();
        m_settingsPanel.Hide();
        // Restore tasklist without calling SetParent on a window currently being destroyed
        m_dock.DetachAndRestore(nullptr);
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
