# WinBatteryBar

A lightweight, native **Windows 10 taskbar BatteryBar & Network Activity Monitor** for your **Laptop**, **Bluetooth Mouse**, and **Bluetooth Headphones** (plus earbuds, keyboards, and controllers) — **no system tray icon**, docked directly inside the Windows 10 taskbar right next to the system tray icons.

## Features
- **True Windows 10 Taskbar Embedding**: Docks inside `ReBarWindow32` immediately to the left of the system tray icons (or right next to classic BatteryBar) and neatly reserves space so running taskbar buttons never overlap.
- **3 Display Styles**:
  - **Full Capsules**: Classic BatteryBar capsules with vector device icon + bold percentage.
  - **Compact Capsules**: Slimmer capsules for tighter taskbars.
  - **Ultra-Compact (Icon + 10%-Step Bar)**: Very compact `25×26px` tiles pairing a crisp vector device icon with a 0%–100% mini battery bar in exact 10% steps.
- **Vivid Red Low-Battery Highlight (No Popups)**: Devices at or below your chosen threshold (`15% / 20% / 25% / 30%`) turn unmistakable crimson-red with a bright red border — zero annoying toast notifications.
- **Optional Live Network Activity Monitor (`↓` / `↑`)**: Displays live download and upload link speeds right beside your battery tiles (`KB/s · MB/s` or `Kbps · Mbps`), with a hover tooltip showing live rates, session totals, and active adapter names (0% overhead when off).
- **Interactive Settings Panel**: Double-click the bar (or right-click → **Settings...**) to switch display styles, toggle laptop battery / disconnected devices / network monitor, tune low-battery threshold & poll interval, show/hide individual devices, or click any device's icon to override it.
- **Universal Windows Bluetooth Battery Support**:
  - **Classic Bluetooth (HFP)** headsets/headphones via Windows SetupAPI (`{104EA319-6EE2-4701-BD47-8DDBF425BBE5} 2`).
  - **Bluetooth Low Energy (BLE)** mice/keyboards/earbuds via WinRT GATT Battery Service (`0x180F` / `0x2A19`) + SetupAPI fallback.
  - **Laptop Battery** via `GetSystemPowerStatus` (toggleable off if you already run classic BatteryBar).
- **Zero Dependencies**: Single standalone static-CRT `.exe` (~500 KB), ~3 MB RAM, 0% idle CPU, no installer or Admin rights needed.

## Usage
1. Download `WinBatteryBar.exe` from the **Releases** page.
2. Double-click `WinBatteryBar.exe` — it snaps straight onto your Windows 10 taskbar on the right next to the tray icons.
3. **Hover** any battery tile or the network tile for full details (exact %, status, time remaining, or live network session totals).
4. **Left-click** to force an immediate Bluetooth battery scan.
5. **Double-click** (or **Right-click → Settings...**) to open the **Settings Panel** and customize everything live.

## Preview Demo Flags
- `WinBatteryBar.exe --demo` — Full 3-device preview (Laptop, Mouse, Headphones)
- `WinBatteryBar.exe --demo --demo-ultra --demo-net` — Ultra-Compact + Live Network Monitor preview
- `WinBatteryBar.exe --demo --demo-set 3 --demo-ultra` — All 11 0%–100% 10%-step Ultra-Compact icons
- `WinBatteryBar.exe --demo --demo-ultra --demo-net --demo-settings` — Open Settings Panel preview

