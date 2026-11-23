#pragma once

#include "battery_model.h"
#include <cstdint>
#include <string>

class NetworkMonitor {
public:
    NetworkMonitor() = default;

    void Reset();
    NetworkStats Sample();

    static NetworkStats BuildDemoStats();
    static std::wstring FormatSpeed(double bytesPerSec, NetUnitMode units, bool ultraShort = false);
    static std::wstring FormatBytesTotal(uint64_t bytes);

private:
    bool m_hasBaseline = false;
    uint64_t m_prevInBytes = 0;
    uint64_t m_prevOutBytes = 0;
    uint64_t m_prevTickMs = 0;
    uint64_t m_sessionDown = 0;
    uint64_t m_sessionUp = 0;
};
