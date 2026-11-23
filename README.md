# WinBatteryBar

A lightweight, native **Windows 10 taskbar BatteryBar** for your **Laptop**, **Bluetooth Mouse**, and **Bluetooth Headphones** (plus earbuds, keyboards, and controllers) — **no system tray icon**, docked directly inside the Windows 10 taskbar right next to the system tray icons.

## Features
- **True Windows 10 Taskbar Embedding**: Docks inside `ReBarWindow32` immediately to the left of the system tray icons (or right next to classic BatteryBar) and neatly reserves space so running taskbar buttons never overlap.
- **No Tray Icon**: Left-click the bar to refresh immediately; right-click the bar for settings, per-device toggles, auto-start, or exit.
- **Universal Windows Bluetooth Battery Support**:
  - **Classic Bluetooth (HFP)** headsets/headphones via Windows SetupAPI (`{104EA319-6EE2-4701-BD47-8DDBF425BBE5} 2`).
  - **Bluetooth Low Energy (BLE)** mice/keyboards/earbuds via WinRT GATT Battery Service (`0x180F` / `0x2A19`) + SetupAPI fallback.
  - **Laptop Battery** via `GetSystemPowerStatus` (with charging indicator and remaining time in hover tooltip — can also be hidden if you already run BatteryBar for your laptop).
- **Clean Native Visuals**: BatteryBar-style color-coded capsules (green / amber / red), crisp custom vector device icons, Per-Monitor V2 DPI scaling, dark & light taskbar support, and floating hover tooltip cards.
- **Zero Dependencies**: Single standalone static-CRT `.exe` (~475 KB), ~3 MB RAM, 0% idle CPU, no installer or Admin rights needed.

## Usage
1. Download `WinBatteryBar.exe` from the **Releases** page.
2. Double-click `WinBatteryBar.exe` — it snaps straight onto your Windows 10 taskbar on the right next to the tray icons.
3. **Hover** any capsule for full device name, exact status, and time remaining.
4. **Left-click** to force an immediate Bluetooth battery scan.
5. **Right-click** the bar to toggle:
   - *Show Laptop Battery* (uncheck if you only want Mouse + Headphones next to your existing BatteryBar)
   - *Show Disconnected Devices*
   - *Compact Capsules*
   - *Devices* (show/hide individual Bluetooth devices)
   - *Start with Windows*
   - *Exit WinBatteryBar* (cleanly restores taskbar layout)

## Preview Demo Mode
Run `WinBatteryBar.exe --demo` to preview the widget with sample Laptop, Mouse, and Headphones battery levels.
