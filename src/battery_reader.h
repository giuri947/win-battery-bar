#pragma once

#include "battery_model.h"
#include <string>
#include <vector>

struct ScanResult {
    std::vector<BatteryDevice> devices;
    std::wstring diagnostics;
};

namespace BatteryReader {
    // Call from a background MTA thread (init_apartment called internally once per thread if needed).
    ScanResult ScanAll(bool includeLaptop, bool includeDisconnected);

    // Deterministic demo sets for visual E2E testing on Windows runners (and user preview via --demo).
    // preset 0: Laptop 84% (charging) + MX Master 3 Mouse 62% + WH-1000XM4 Headphones 18% (low red)
    // preset 1: Laptop 96% + Pebble Mouse 34% (amber) + AirPods Pro 78% + Keychron K3 14% (low red)
    // preset 2: Mouse 88% + Headphones 51% (2-device BT-only mode, laptop hidden)
    std::vector<BatteryDevice> BuildDemoDevices(int preset = 0);

    // Infer icon type from device name when hardware class/appearance is generic.
    DeviceKind ClassifyByName(const std::wstring& name);
}
