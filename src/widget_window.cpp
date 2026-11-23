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

constexpr const wchar_t* kWidgetClass = L"WinBatteryBarTaskbarWidget";

constexpr UINT WM_APP_SCAN_DONE = WM_APP + 101;
constexpr UINT_PTR kTimerLayout     = 1;
constexpr UINT_PTR kTimerFlashEnd   = 2;
constexpr UINT_PTR kTimerDebounce   = 3;

constexpr UINT kCmdRefresh          = 1001;
constexpr UINT kCmdShowLaptop       = 1002;
constexpr UINT kCmdShowDisconnected = 1003;
constexpr UINT kCmdCompactMode      = 1004;
constexpr UINT kCmdAutoStart        = 1005;
constexpr UINT kCmdCopyDiagnostics  = 1006;
constexpr UINT kCmdExit             = 1007;
constexpr UINT kCmdToggleDeviceBase = 2000;

} // namespace

WidgetWindow::WidgetWindow(const LaunchOptions& opts)
    : m_opts(opts)
{
    m_settings = SettingsStore::Load();
    if (m_opts.forceCompact) {
        m_settings.compactMode = true;
    }
    if (m_opts.demoMode && m_opts.demoPreset == 2) {
        m_settings.showLaptop = false;
    }
}

WidgetWindow::~WidgetWindow() {
    Destroy();
}

bool WidgetWindow::Create(HINSTANCE hInst) {
    m_hInst = hInst;
    m_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWidgetClass;
    RegisterClassExW(&wc);

    m_tooltip.Create(hInst);

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kWidgetClass,
        L"WinBatteryBar",
        WS_POPUP,
        0, 0, 80, 28,
        nullptr, nullptr, hInst, this);

    if (!m_hwnd) {
        return false;
    }

    // Attach to Windows 10 taskbar (ReBarWindow32)
    m_dock.Attach(m_hwnd);

    if (m_opts.demoMode) {
        m_allDevices = BatteryReader::BuildDemoDevices(m_opts.demoPreset);
        m_lastDiagnostics = L"Demo mode active (preset " + std::to_wstring(m_opts.demoPreset) + L")";
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
        StartWorkerThread();
    }

    UpdateDockAndRedraw(true);

    // Optional scripted hover state for automated E2E visual testing
    if (m_opts.demoMode && m_opts.demoHoverIndex >= 0 &&
        m_opts.demoHoverIndex < static_cast<int>(m_visibleDevices.size()))
    {
        m_hoveredIndex = m_opts.demoHoverIndex;
        RedrawOnly();
        UpdateTooltipForHover();
    }

    WriteStatusFile();

    // Periodic taskbar layout guard (handles Explorer layout updates, DPI/theme changes, taskbar auto-hide)
    SetTimer(m_hwnd, kTimerLayout, 400, nullptr);
    return true;
}

void WidgetWindow::Destroy() {
    StopWorkerThread();
    m_tooltip.Destroy();
    if (m_hwnd && IsWindow(m_hwnd)) {
        KillTimer(m_hwnd, kTimerLayout);
        KillTimer(m_hwnd, kTimerFlashEnd);
        KillTimer(m_hwnd, kTimerDebounce);
        m_dock.DetachAndRestore(m_hwnd);
        DestroyWindow(m_hwnd);
    } else {
        m_dock.DetachAndRestore(nullptr);
    }
    m_hwnd = nullptr;
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
    if (showFlash && m_hwnd) {
        m_refreshFlash = true;
        RedrawOnly();
        SetTimer(m_hwnd, kTimerFlashEnd, 180, nullptr);
    }
    if (m_opts.demoMode) return;
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

    while (true) {
        {
            std::unique_lock<std::mutex> lk(m_workerMu);
            int interval = std::clamp(m_settings.pollIntervalSec, 5, 300);
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

        if (m_hwnd && IsWindow(m_hwnd)) {
            PostMessageW(m_hwnd, WM_APP_SCAN_DONE, 0, 0);
        }
    }

    try {
        winrt::uninit_apartment();
    } catch (...) {}
}

void WidgetWindow::ApplyFilteredDevices() {
    m_visibleDevices.clear();
    for (const auto& d : m_allDevices) {
        if (d.kind == DeviceKind::Laptop && !m_settings.showLaptop) {
            continue;
        }
        if (!d.connected && !m_settings.showDisconnected) {
            continue;
        }
        if (SettingsStore::IsDeviceHidden(m_settings, d.id)) {
            continue;
        }
        m_visibleDevices.push_back(d);
    }

    if (m_hoveredIndex >= static_cast<int>(m_visibleDevices.size())) {
        m_hoveredIndex = -1;
        m_tooltip.Hide();
    }
}

void WidgetWindow::UpdateDockAndRedraw(bool forceReposition) {
    if (!m_hwnd || !IsWindow(m_hwnd)) return;

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
        m_visibleDevices, dpi, thickness, horiz, m_settings.compactMode);

    bool sizeChanged = (newLayout.width != m_layout.width || newLayout.height != m_layout.height);
    m_layout = std::move(newLayout);

    DockMetrics newMetrics;
    if (!m_dock.UpdateLayout(m_hwnd, m_layout.width, m_layout.height, forceReposition || sizeChanged, newMetrics)) {
        return;
    }

    bool themeChanged = (newMetrics.lightTheme != m_metrics.lightTheme);
    bool dpiChanged = (newMetrics.dpi != m_metrics.dpi);
    m_metrics = newMetrics;
    if (m_opts.forceLightTheme) {
        m_metrics.lightTheme = true;
    }

    if (forceReposition || sizeChanged || themeChanged || dpiChanged || !m_lastRenderOk) {
        RedrawOnly();
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
    bool light = m_opts.forceLightTheme ? true : m_metrics.lightTheme;

    if (!m_useColorKeyFallback) {
        SetLastError(0);
        m_lastRenderOk = Renderer::RenderToLayeredWindow(
            m_hwnd,
            m_visibleDevices,
            m_layout,
            m_metrics.dpi,
            light,
            m_hoveredIndex,
            m_refreshFlash,
            nullptr);
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
    if (m_hoveredIndex < 0 || m_hoveredIndex >= static_cast<int>(m_layout.tiles.size())) {
        m_tooltip.Hide();
        return;
    }

    RECT tileLocal = m_layout.tiles[static_cast<size_t>(m_hoveredIndex)].rect;
    RECT anchorScreen = {
        m_metrics.screenRect.left + tileLocal.left,
        m_metrics.screenRect.top + tileLocal.top,
        m_metrics.screenRect.left + tileLocal.right,
        m_metrics.screenRect.top + tileLocal.bottom
    };

    bool light = m_opts.forceLightTheme ? true : m_metrics.lightTheme;
    if (m_visibleDevices.empty()) {
        m_tooltip.ShowEmpty(anchorScreen, m_metrics.dpi, light);
    } else if (m_hoveredIndex < static_cast<int>(m_visibleDevices.size())) {
        m_tooltip.ShowForDevice(m_visibleDevices[static_cast<size_t>(m_hoveredIndex)], anchorScreen, m_metrics.dpi, light);
    }
}

void WidgetWindow::ShowContextMenu(POINT screenPt) {
    m_tooltip.Hide();

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, kCmdRefresh, L"Refresh Now");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING | (m_settings.showLaptop ? MF_CHECKED : MF_UNCHECKED),
        kCmdShowLaptop, L"Show Laptop Battery");
    AppendMenuW(menu, MF_STRING | (m_settings.showDisconnected ? MF_CHECKED : MF_UNCHECKED),
        kCmdShowDisconnected, L"Show Disconnected Devices");
    AppendMenuW(menu, MF_STRING | (m_settings.compactMode ? MF_CHECKED : MF_UNCHECKED),
        kCmdCompactMode, L"Compact Capsules");

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
    AppendMenuW(menu, MF_STRING | (m_settings.autoStart ? MF_CHECKED : MF_UNCHECKED),
        kCmdAutoStart, L"Start with Windows");
    AppendMenuW(menu, MF_STRING, kCmdCopyDiagnostics, L"Copy Diagnostics");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"Exit WinBatteryBar");

    // Standard Win32 requirement so clicking outside dismisses the menu cleanly
    SetForegroundWindow(m_hwnd);
    UINT cmd = TrackPopupMenuEx(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        screenPt.x, screenPt.y,
        m_hwnd,
        nullptr);
    PostMessageW(m_hwnd, WM_NULL, 0, 0);

    DestroyMenu(menu);

    if (cmd == 0) return;

    if (cmd == kCmdRefresh) {
        TriggerScanNow(true);
    } else if (cmd == kCmdShowLaptop) {
        m_settings.showLaptop = !m_settings.showLaptop;
        SettingsStore::Save(m_settings);
        ApplyFilteredDevices();
        UpdateDockAndRedraw(true);
    } else if (cmd == kCmdShowDisconnected) {
        m_settings.showDisconnected = !m_settings.showDisconnected;
        SettingsStore::Save(m_settings);
        ApplyFilteredDevices();
        UpdateDockAndRedraw(true);
    } else if (cmd == kCmdCompactMode) {
        m_settings.compactMode = !m_settings.compactMode;
        SettingsStore::Save(m_settings);
        UpdateDockAndRedraw(true);
    } else if (cmd == kCmdAutoStart) {
        bool target = !m_settings.autoStart;
        if (SettingsStore::SetAutoStartEnabled(target)) {
            m_settings.autoStart = target;
            SettingsStore::Save(m_settings);
        }
    } else if (cmd == kCmdCopyDiagnostics) {
        CopyDiagnosticsToClipboard();
    } else if (cmd == kCmdExit) {
        PostQuitMessage(0);
    } else if (cmd >= kCmdToggleDeviceBase && cmd < kCmdToggleDeviceBase + 32) {
        size_t idx = static_cast<size_t>(cmd - kCmdToggleDeviceBase);
        if (idx < m_allDevices.size()) {
            SettingsStore::ToggleDeviceHidden(m_settings, m_allDevices[idx].id);
            SettingsStore::Save(m_settings);
            ApplyFilteredDevices();
            UpdateDockAndRedraw(true);
        }
    }
}

void WidgetWindow::CopyDiagnosticsToClipboard() {
    std::wstringstream ss;
    ss << L"WinBatteryBar Diagnostics\r\n"
       << L"Embedded: " << (m_metrics.embedded ? L"yes" : L"no")
       << L" | Win11: " << (m_metrics.isWin11 ? L"yes" : L"no")
       << L" | DPI: " << m_metrics.dpi
       << L" | LightTheme: " << (m_metrics.lightTheme ? L"yes" : L"no") << L"\r\n"
       << L"Visible devices: " << m_visibleDevices.size() << L" / " << m_allDevices.size() << L"\r\n\r\n"
       << m_lastDiagnostics;

    std::wstring text = ss.str();
    if (OpenClipboard(m_hwnd)) {
        EmptyClipboard();
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            void* ptr = GlobalLock(hMem);
            if (ptr) {
                memcpy(ptr, text.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            } else {
                GlobalFree(hMem);
            }
        }
        CloseClipboard();
    }
}

void WidgetWindow::WriteStatusFile() {
    if (m_opts.statusFilePath.empty()) return;

    std::ostringstream json;
    json << "{\n"
         << "  \"embedded\": " << (m_metrics.embedded ? "true" : "false") << ",\n"
         << "  \"isWin11\": " << (m_metrics.isWin11 ? "true" : "false") << ",\n"
         << "  \"horizontal\": " << (m_metrics.horizontal ? "true" : "false") << ",\n"
         << "  \"lightTheme\": " << (m_metrics.lightTheme ? "true" : "false") << ",\n"
         << "  \"dpi\": " << m_metrics.dpi << ",\n"
         << "  \"taskbarThickness\": " << m_metrics.taskbarThickness << ",\n"
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
         << "  \"tooltipVisible\": " << (m_tooltip.IsVisible() ? "true" : "false") << "\n"
         << "}\n";

    std::ofstream ofs(std::filesystem::path(m_opts.statusFilePath), std::ios::binary | std::ios::trunc);
    if (ofs) {
        ofs << json.str();
    }
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
        return self->WndProc(msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT WidgetWindow::WndProc(UINT msg, WPARAM wp, LPARAM lp) {
    if (m_taskbarCreatedMsg != 0 && msg == m_taskbarCreatedMsg) {
        // Explorer restarted: cleanly re-attach to the new taskbar
        m_dock.Attach(m_hwnd);
        UpdateDockAndRedraw(true);
        return 0;
    }

    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(m_hwnd, &ps);
        if (m_useColorKeyFallback && hdc) {
            bool light = m_opts.forceLightTheme ? true : m_metrics.lightTheme;
            Renderer::RenderToPaintDC(hdc, m_visibleDevices, m_layout, m_metrics.dpi, light, m_hoveredIndex, m_refreshFlash);
        }
        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wp == kTimerLayout) {
            UpdateDockAndRedraw(false);
            WriteStatusFile();
        } else if (wp == kTimerFlashEnd) {
            KillTimer(m_hwnd, kTimerFlashEnd);
            m_refreshFlash = false;
            RedrawOnly();
        } else if (wp == kTimerDebounce) {
            KillTimer(m_hwnd, kTimerDebounce);
            TriggerScanNow(false);
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
        if (m_hoveredIndex >= 0) {
            UpdateTooltipForHover();
        }
        WriteStatusFile();
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!m_trackingMouse) {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = m_hwnd;
            TrackMouseEvent(&tme);
            m_trackingMouse = true;
        }
        int x = static_cast<int>(static_cast<short>(LOWORD(lp)));
        int y = static_cast<int>(static_cast<short>(HIWORD(lp)));
        int hit = HitTestTile(x, y);
        if (hit != m_hoveredIndex) {
            m_hoveredIndex = hit;
            RedrawOnly();
            UpdateTooltipForHover();
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        m_trackingMouse = false;
        if (m_hoveredIndex != -1) {
            m_hoveredIndex = -1;
            RedrawOnly();
            m_tooltip.Hide();
        }
        return 0;

    case WM_LBUTTONUP:
        TriggerScanNow(true);
        return 0;

    case WM_RBUTTONUP: {
        POINT pt = {};
        GetCursorPos(&pt);
        ShowContextMenu(pt);
        return 0;
    }

    case WM_POWERBROADCAST:
        if (wp == PBT_APMPOWERSTATUSCHANGE || wp == PBT_APMRESUMEAUTOMATIC) {
            SetTimer(m_hwnd, kTimerDebounce, 500, nullptr);
        }
        return TRUE;

    case WM_DEVICECHANGE:
        if (wp == DBT_DEVNODES_CHANGED || wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE) {
            SetTimer(m_hwnd, kTimerDebounce, 1200, nullptr);
        }
        return TRUE;

    case WM_SETTINGCHANGE:
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        UpdateDockAndRedraw(true);
        return 0;

    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;

    case WM_DESTROY:
        m_dock.DetachAndRestore(m_hwnd);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
