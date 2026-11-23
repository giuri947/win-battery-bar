#include "widget_window.h"
#include "renderer.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <string>

namespace {

void EnablePerMonitorDpiAwareness() {
    using SetDpiCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto fn = reinterpret_cast<SetDpiCtxFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (fn) {
        if (fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
        fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
    } else {
        SetProcessDPIAware();
    }
}

LaunchOptions ParseArgs() {
    LaunchOptions opts;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return opts;

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--demo") {
            opts.demoMode = true;
        } else if (arg == L"--demo-set" && i + 1 < argc) {
            opts.demoMode = true;
            opts.demoPreset = _wtoi(argv[++i]);
        } else if (arg == L"--demo-hover" && i + 1 < argc) {
            opts.demoHoverIndex = _wtoi(argv[++i]);
        } else if (arg == L"--demo-compact") {
            opts.forceStyle = DisplayStyle::Compact;
        } else if (arg == L"--demo-ultra") {
            opts.forceStyle = DisplayStyle::UltraCompact;
        } else if (arg == L"--demo-net") {
            opts.forceShowNetwork = true;
        } else if (arg == L"--demo-settings") {
            opts.demoOpenSettings = true;
        } else if (arg == L"--demo-light") {
            opts.forceLightTheme = true;
        } else if (arg == L"--status-file" && i + 1 < argc) {
            opts.statusFilePath = argv[++i];
        }
    }

    LocalFree(argv);
    return opts;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    EnablePerMonitorDpiAwareness();

    LaunchOptions opts = ParseArgs();

    // Enforce single instance for normal launches (demo tests may pass distinct runs sequentially)
    HANDLE hMutex = nullptr;
    if (!opts.demoMode) {
        hMutex = CreateMutexW(nullptr, TRUE, L"Local\\WinBatteryBar_SingleInstance_v1");
        if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(hMutex);
            return 0;
        }
    }

    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Renderer::InitGdiPlus();

    {
        WidgetWindow widget(opts);
        if (widget.Create(hInst)) {
            MSG msg = {};
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

    Renderer::ShutdownGdiPlus();
    if (SUCCEEDED(hrCo)) {
        CoUninitialize();
    }
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
    return 0;
}
