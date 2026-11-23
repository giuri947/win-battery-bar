param(
    [Parameter(Mandatory = $true)]
    [string]$ExePath,
    [Parameter(Mandatory = $true)]
    [string]$OutDir
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

Add-Type @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class WinCapture {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left, Top, Right, Bottom;
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowW(string lpClassName, string lpWindowName);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowExW(IntPtr parent, IntPtr childAfter, string cls, string title);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern bool PostMessageW(IntPtr hWnd, uint msg, IntPtr wp, IntPtr lp);

    [DllImport("user32.dll")]
    public static extern IntPtr SendMessageTimeoutW(
        IntPtr hWnd, uint msg, IntPtr wp, string lp, uint flags, uint timeout, out IntPtr result);

    [DllImport("user32.dll")]
    public static extern IntPtr GetDC(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);

    [DllImport("gdi32.dll")]
    public static extern IntPtr CreateCompatibleDC(IntPtr hdc);

    [DllImport("gdi32.dll")]
    public static extern bool DeleteDC(IntPtr hdc);

    [DllImport("gdi32.dll")]
    public static extern IntPtr CreateCompatibleBitmap(IntPtr hdc, int w, int h);

    [DllImport("gdi32.dll")]
    public static extern IntPtr SelectObject(IntPtr hdc, IntPtr hgdiobj);

    [DllImport("gdi32.dll")]
    public static extern bool DeleteObject(IntPtr hObject);

    [DllImport("gdi32.dll")]
    public static extern bool BitBlt(
        IntPtr hdcDest, int xDest, int yDest, int w, int h,
        IntPtr hdcSrc, int xSrc, int ySrc, uint rop);

    [DllImport("user32.dll")]
    public static extern bool SetProcessDPIAware();

    public const uint SRCCOPY = 0x00CC0020;
    public const uint CAPTUREBLT = 0x40000000;
    public const uint WM_CLOSE = 0x0010;
    public const uint WM_SETTINGCHANGE = 0x001A;
    public const uint SMTO_ABORTIFHUNG = 0x0002;

    public static Bitmap CaptureRegion(int x, int y, int w, int h) {
        IntPtr screenDC = GetDC(IntPtr.Zero);
        IntPtr memDC = CreateCompatibleDC(screenDC);
        IntPtr hBmp = CreateCompatibleBitmap(screenDC, w, h);
        IntPtr old = SelectObject(memDC, hBmp);
        BitBlt(memDC, 0, 0, w, h, screenDC, x, y, SRCCOPY | CAPTUREBLT);
        SelectObject(memDC, old);
        Bitmap bmp = Image.FromHbitmap(hBmp);
        DeleteObject(hBmp);
        DeleteDC(memDC);
        ReleaseDC(IntPtr.Zero, screenDC);
        return bmp;
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassNameW(IntPtr hWnd, System.Text.StringBuilder lpClassName, int nMaxCount);

    public static IntPtr FindByClass(string cls) {
        return FindWindowW(cls, null);
    }

    public static IntPtr FindChildByClass(IntPtr parent, IntPtr after, string cls) {
        return FindWindowExW(parent, after, string.IsNullOrEmpty(cls) ? null : cls, null);
    }

    public static void BroadcastThemeChange() {
        IntPtr res;
        SendMessageTimeoutW((IntPtr)0xFFFF, WM_SETTINGCHANGE, IntPtr.Zero, "ImmersiveColorSet", SMTO_ABORTIFHUNG, 2000, out res);
    }
}
"@ -ReferencedAssemblies System.Drawing

[WinCapture]::SetProcessDPIAware() | Out-Null

function Dump-ChildWindows([IntPtr]$parent, [string]$label) {
    if ($parent -eq [IntPtr]::Zero) { return }
    Write-Host "Children of ${label}:"
    $child = [IntPtr]::Zero
    while ($true) {
        $child = [WinCapture]::FindChildByClass($parent, $child, $null)
        if ($child -eq [IntPtr]::Zero) { break }
        $sb = New-Object System.Text.StringBuilder 256
        [WinCapture]::GetClassNameW($child, $sb, $sb.Capacity) | Out-Null
        $rc = New-Object WinCapture+RECT
        [WinCapture]::GetWindowRect($child, [ref]$rc) | Out-Null
        Write-Host ("  [{0}] hwnd=0x{1:X} rect=({2},{3})-({4},{5}) W={6} H={7}" -f $sb.ToString(), $child.ToInt64(), $rc.Left, $rc.Top, $rc.Right, $rc.Bottom, ($rc.Right - $rc.Left), ($rc.Bottom - $rc.Top))
    }
}

function Get-TaskbarGeometry {
    $tb = [WinCapture]::FindByClass("Shell_TrayWnd")
    if ($tb -eq [IntPtr]::Zero) { return $null }
    $rebar = [WinCapture]::FindChildByClass($tb, [IntPtr]::Zero, "ReBarWindow32")
    $taskList = [IntPtr]::Zero
    if ($rebar -ne [IntPtr]::Zero) {
        $taskList = [WinCapture]::FindChildByClass($rebar, [IntPtr]::Zero, "MSTaskSwWClass")
        if ($taskList -eq [IntPtr]::Zero) {
            $taskList = [WinCapture]::FindChildByClass($rebar, [IntPtr]::Zero, "MSTaskListWClass")
        }
    }
    $tray = [WinCapture]::FindChildByClass($tb, [IntPtr]::Zero, "TrayNotifyWnd")

    $rTb = New-Object WinCapture+RECT
    $rRebar = New-Object WinCapture+RECT
    $rMin = New-Object WinCapture+RECT
    $rTray = New-Object WinCapture+RECT

    [WinCapture]::GetWindowRect($tb, [ref]$rTb) | Out-Null
    if ($rebar -ne [IntPtr]::Zero) { [WinCapture]::GetWindowRect($rebar, [ref]$rRebar) | Out-Null }
    if ($taskList -ne [IntPtr]::Zero) { [WinCapture]::GetWindowRect($taskList, [ref]$rMin) | Out-Null }
    if ($tray -ne [IntPtr]::Zero) { [WinCapture]::GetWindowRect($tray, [ref]$rTray) | Out-Null }

    return [PSCustomObject]@{
        TaskbarHwnd  = $tb
        ReBarHwnd    = $rebar
        TaskListHwnd = $taskList
        TrayHwnd     = $tray
        Taskbar      = $rTb
        ReBar        = $rRebar
        TaskList     = $rMin
        Tray         = $rTray
        TaskListWidth = ($rMin.Right - $rMin.Left)
    }
}

function Save-Zoom2x([System.Drawing.Bitmap]$src, [string]$path) {
    $dst = New-Object System.Drawing.Bitmap ($src.Width * 2), ($src.Height * 2)
    $g = [System.Drawing.Graphics]::FromImage($dst)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $g.DrawImage($src, 0, 0, $dst.Width, $dst.Height)
    $g.Dispose()
    $dst.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $dst.Dispose()
}

function Capture-Scenario([string]$name, [int]$cropWidth = 680, [int]$cropHeight = 160) {
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $full = [WinCapture]::CaptureRegion($bounds.X, $bounds.Y, $bounds.Width, $bounds.Height)
    $full.Save((Join-Path $OutDir "${name}_full.png"), [System.Drawing.Imaging.ImageFormat]::Png)

    $cw = [Math]::Min($cropWidth, $bounds.Width)
    $ch = [Math]::Min($cropHeight, $bounds.Height)
    $cx = $bounds.X + $bounds.Width - $cw
    $cy = $bounds.Y + $bounds.Height - $ch
    $crop = [WinCapture]::CaptureRegion($cx, $cy, $cw, $ch)
    $cropPath = Join-Path $OutDir "${name}_taskbar.png"
    $crop.Save($cropPath, [System.Drawing.Imaging.ImageFormat]::Png)
    Save-Zoom2x $crop (Join-Path $OutDir "${name}_zoom2x.png")

    $crop.Dispose()
    $full.Dispose()
    Write-Host "Captured $name ($($bounds.Width)x$($bounds.Height), crop ${cw}x${ch})"
}

function Stop-WidgetCleanly($proc) {
    # Find our widget window (may be child of ReBarWindow32 or Shell_TrayWnd)
    $geo = Get-TaskbarGeometry
    $w = [IntPtr]::Zero
    if ($geo) {
        if ($geo.ReBarHwnd -ne [IntPtr]::Zero) {
            $w = [WinCapture]::FindChildByClass($geo.ReBarHwnd, [IntPtr]::Zero, "WinBatteryBarTaskbarWidget")
        }
        if ($w -eq [IntPtr]::Zero) {
            $w = [WinCapture]::FindChildByClass($geo.TaskbarHwnd, [IntPtr]::Zero, "WinBatteryBarTaskbarWidget")
        }
    }
    if ($w -eq [IntPtr]::Zero) {
        $w = [WinCapture]::FindByClass("WinBatteryBarTaskbarWidget")
    }
    if ($w -ne [IntPtr]::Zero) {
        [WinCapture]::PostMessageW($w, [WinCapture]::WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    } else {
        Write-Warning "Stop-WidgetCleanly: could not find WinBatteryBarTaskbarWidget HWND!"
    }
    if ($proc -and !$proc.HasExited) {
        $proc.WaitForExit(4000) | Out-Null
        if (!$proc.HasExited) {
            Write-Warning "Stop-WidgetCleanly: process did not exit on WM_CLOSE, killing!"
            $proc.Kill()
            $proc.WaitForExit(2000) | Out-Null
        }
    }
    Start-Sleep -Milliseconds 600
}

function Run-DemoScenario([string]$name, [string[]]$argsList) {
    $statusPath = Join-Path $OutDir "${name}_status.json"
    if (Test-Path $statusPath) { Remove-Item $statusPath -Force }
    $fullArgs = $argsList + @("--status-file", $statusPath)
    Write-Host "Launching $name : $ExePath $($fullArgs -join ' ')"
    $proc = Start-Process -FilePath $ExePath -ArgumentList $fullArgs -PassThru

    # Wait up to 6 seconds for status file to appear
    $waited = 0
    while (!(Test-Path $statusPath) -and $waited -lt 6000) {
        Start-Sleep -Milliseconds 200
        $waited += 200
    }
    Start-Sleep -Milliseconds 1200

    $geo = Get-TaskbarGeometry
    $status = $null
    if (Test-Path $statusPath) {
        $raw = Get-Content $statusPath -Raw
        Write-Host "Status ($name):`n$raw"
        $status = $raw | ConvertFrom-Json
    } else {
        Write-Warning "Status file missing for $name!"
    }

    $hostDuring = [WinCapture]::FindByClass("WinBatteryBarHostWnd")
    if ($hostDuring -eq [IntPtr]::Zero) {
        throw "Scenario $name failed assertion: WinBatteryBarHostWnd top-level host window not found!"
    }

    Capture-Scenario $name
    $widthDuring = if ($geo) { $geo.TaskListWidth } else { -1 }

    Stop-WidgetCleanly $proc
    $hostAfter = [WinCapture]::FindByClass("WinBatteryBarHostWnd")
    if ($hostAfter -ne [IntPtr]::Zero) {
        throw "Scenario $name failed assertion: WinBatteryBarHostWnd leaked after exit!"
    }
    return [PSCustomObject]@{
        Status = $status
        TaskListWidthDuring = $widthDuring
    }
}

# 0. Ensure Windows Explorer taskbar is running on this session
$geo = Get-TaskbarGeometry
if (-not $geo) {
    Write-Host "Shell_TrayWnd not found yet; starting explorer.exe..."
    Start-Process explorer.exe
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 500
        $geo = Get-TaskbarGeometry
        if ($geo) { break }
    }
}
if (-not $geo) {
    throw "Failed to locate Shell_TrayWnd taskbar on runner!"
}

Write-Host ("Baseline Taskbar: ({0},{1})-({2},{3}) ReBar: ({4},{5})-({6},{7}) MSTaskSwWClass W={8} Tray: ({9},{10})-({11},{12})" -f `
    $geo.Taskbar.Left, $geo.Taskbar.Top, $geo.Taskbar.Right, $geo.Taskbar.Bottom, `
    $geo.ReBar.Left, $geo.ReBar.Top, $geo.ReBar.Right, $geo.ReBar.Bottom, `
    $geo.TaskListWidth, `
    $geo.Tray.Left, $geo.Tray.Top, $geo.Tray.Right, $geo.Tray.Bottom)

$baselineTaskListWidth = $geo.TaskListWidth

# Open a Notepad window briefly so there is a real app icon on the taskbar to demonstrate zero overlap
$notepad = Start-Process notepad.exe -PassThru
Start-Sleep -Milliseconds 1000
Capture-Scenario "01_before_taskbar"

# Scenario 2: Classic 3-device BatteryBar on dark Win10 taskbar (Laptop charging 84% + Mouse 62% + Headphones 18% red)
$r2 = Run-DemoScenario "02_demo_3dev_dark" @("--demo", "--demo-set", "0")
if (-not $r2.Status -or -not $r2.Status.embedded -or -not $r2.Status.renderOk) {
    throw "Scenario 02 failed assertion: embedded=$($r2.Status.embedded) renderOk=$($r2.Status.renderOk) err=$($r2.Status.renderError)"
}
if ($r2.TaskListWidthDuring -ge $baselineTaskListWidth) {
    throw "MSTaskSwWClass did not shrink! baseline=$baselineTaskListWidth during=$($r2.TaskListWidthDuring)"
}

# Check taskbar restored cleanly right after exit!
$geoAfter2 = Get-TaskbarGeometry
Write-Host "After Scenario 02 exit: MSTaskSwWClass W=$($geoAfter2.TaskListWidth) (baseline=$baselineTaskListWidth)"
if ([Math]::Abs($geoAfter2.TaskListWidth - $baselineTaskListWidth) -gt 6) {
    throw "MSTaskSwWClass did not restore cleanly after exit! baseline=$baselineTaskListWidth after=$($geoAfter2.TaskListWidth)"
}

# Scenario 3: Hover tooltip visible right above the 18% Headphones capsule
$r3 = Run-DemoScenario "03_demo_hover_tooltip" @("--demo", "--demo-set", "0", "--demo-hover", "2")
if (-not $r3.Status -or -not $r3.Status.tooltipVisible) {
    throw "Scenario 03 failed assertion: tooltipVisible=$($r3.Status.tooltipVisible)"
}

# Scenario 4: 4-device state showing Green + Amber (34%) + Green + Red (14%)
$r4 = Run-DemoScenario "04_demo_4dev_states" @("--demo", "--demo-set", "1")
if (-not $r4.Status -or $r4.Status.deviceCount -ne 4) {
    throw "Scenario 04 failed assertion: deviceCount=$($r4.Status.deviceCount)"
}

# Scenario 5: 2-device BT-only compact mode (when laptop battery is hidden)
$r5 = Run-DemoScenario "05_demo_bt_compact" @("--demo", "--demo-set", "2", "--demo-compact")
if (-not $r5.Status -or $r5.Status.deviceCount -ne 2) {
    throw "Scenario 05 failed assertion: deviceCount=$($r5.Status.deviceCount)"
}

# Scenario 6: Light Windows 10 taskbar theme preview
$persKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize"
if (!(Test-Path $persKey)) { New-Item -Path $persKey -Force | Out-Null }
$origLight = $null
try {
    $origLight = (Get-ItemProperty -Path $persKey -Name SystemUsesLightTheme -ErrorAction SilentlyContinue).SystemUsesLightTheme
} catch {}
try {
    Set-ItemProperty -Path $persKey -Name SystemUsesLightTheme -Type DWord -Value 1
    [WinCapture]::BroadcastThemeChange()
    Start-Sleep -Milliseconds 1200

    $r6 = Run-DemoScenario "06_demo_light_theme" @("--demo", "--demo-set", "0", "--demo-light")
    if (-not $r6.Status -or -not $r6.Status.embedded -or -not $r6.Status.renderOk) {
        throw "Scenario 06 failed assertion: embedded=$($r6.Status.embedded) renderOk=$($r6.Status.renderOk)"
    }

    $r6b = Run-DemoScenario "06b_demo_light_tooltip" @("--demo", "--demo-set", "0", "--demo-light", "--demo-hover", "1")
    if (-not $r6b.Status -or -not $r6b.Status.tooltipVisible) {
        throw "Scenario 06b failed assertion: tooltipVisible=$($r6b.Status.tooltipVisible)"
    }
} finally {
    # Restore original taskbar theme
    if ($null -ne $origLight) {
        Set-ItemProperty -Path $persKey -Name SystemUsesLightTheme -Type DWord -Value $origLight
    } else {
        Set-ItemProperty -Path $persKey -Name SystemUsesLightTheme -Type DWord -Value 0
    }
    [WinCapture]::BroadcastThemeChange()
    Start-Sleep -Milliseconds 800
}

# Scenario 7: Real mode smoke test (actual SetupAPI + WinRT BLE + GetSystemPowerStatus scan)
$r7 = Run-DemoScenario "07_real_mode_smoke" @()
if (-not $r7.Status -or -not $r7.Status.renderOk) {
    throw "Scenario 07 (real mode) failed assertion: renderOk=$($r7.Status.renderOk)"
}

Capture-Scenario "08_after_exit_restore"

if ($notepad -and !$notepad.HasExited) {
    $notepad.CloseMainWindow() | Out-Null
    $notepad.WaitForExit(1500) | Out-Null
    if (!$notepad.HasExited) { $notepad.Kill() }
}

$finalGeo = Get-TaskbarGeometry
Write-Host "Final restored MSTaskSwWClass W=$($finalGeo.TaskListWidth) (baseline=$baselineTaskListWidth)"
if ([Math]::Abs($finalGeo.TaskListWidth - $baselineTaskListWidth) -gt 6) {
    throw "Final MSTaskSwWClass restore check failed!"
}

Write-Host "ALL E2E ASSERTIONS AND SCREENSHOTS PASSED!"
