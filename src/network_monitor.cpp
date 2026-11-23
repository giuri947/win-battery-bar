#include "network_monitor.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

bool IsHardwareCandidate(const MIB_IF_ROW2& row) {
    if (row.OperStatus != IfOperStatusUp) return false;
    if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.Type == IF_TYPE_TUNNEL) return false;
    if (row.InterfaceAndOperStatusFlags.FilterInterface) return false;
    return row.InterfaceAndOperStatusFlags.HardwareInterface != FALSE;
}

bool IsFallbackCandidate(const MIB_IF_ROW2& row) {
    if (row.OperStatus != IfOperStatusUp) return false;
    if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.Type == IF_TYPE_TUNNEL) return false;
    if (row.InterfaceAndOperStatusFlags.FilterInterface) return false;
    return row.MediaType != NdisMediumUnspecified;
}

} // namespace

void NetworkMonitor::Reset() {
    m_hasBaseline = false;
    m_prevInBytes = 0;
    m_prevOutBytes = 0;
    m_prevTickMs = 0;
    m_sessionDown = 0;
    m_sessionUp = 0;
}

NetworkStats NetworkMonitor::Sample() {
    NetworkStats stats;
    uint64_t nowMs = GetTickCount64();
    stats.updatedTickMs = nowMs;

    PMIB_IF_TABLE2 table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || !table) {
        return stats;
    }

    uint64_t totalIn = 0;
    uint64_t totalOut = 0;
    std::vector<std::wstring> activeNames;

    auto accumulate = [&](bool fallback) {
        for (ULONG i = 0; i < table->NumEntries; ++i) {
            const MIB_IF_ROW2& row = table->Table[i];
            bool ok = fallback ? IsFallbackCandidate(row) : IsHardwareCandidate(row);
            if (!ok) continue;

            totalIn += row.InOctets;
            totalOut += row.OutOctets;

            if (row.Alias[0] != L'\0') {
                std::wstring alias(row.Alias);
                if (std::find(activeNames.begin(), activeNames.end(), alias) == activeNames.end()) {
                    activeNames.push_back(std::move(alias));
                }
            }
        }
    };

    accumulate(false);
    if (activeNames.empty() && totalIn == 0 && totalOut == 0) {
        accumulate(true);
    }

    FreeMibTable(table);

    stats.available = true;
    for (size_t i = 0; i < activeNames.size() && i < 2; ++i) {
        if (i > 0) stats.adapterSummary += L", ";
        stats.adapterSummary += activeNames[i];
    }
    if (activeNames.size() > 2) {
        stats.adapterSummary += L" +" + std::to_wstring(activeNames.size() - 2);
    }
    if (stats.adapterSummary.empty()) {
        stats.adapterSummary = L"No active adapter";
    }

    if (m_hasBaseline && nowMs > m_prevTickMs) {
        double dtSec = static_cast<double>(nowMs - m_prevTickMs) / 1000.0;
        if (dtSec >= 0.05) {
            uint64_t dIn  = (totalIn >= m_prevInBytes)   ? (totalIn - m_prevInBytes)   : 0;
            uint64_t dOut = (totalOut >= m_prevOutBytes) ? (totalOut - m_prevOutBytes) : 0;
            stats.downBytesPerSec = static_cast<double>(dIn) / dtSec;
            stats.upBytesPerSec   = static_cast<double>(dOut) / dtSec;
            m_sessionDown += dIn;
            m_sessionUp += dOut;
        }
    }

    m_hasBaseline = true;
    m_prevInBytes = totalIn;
    m_prevOutBytes = totalOut;
    m_prevTickMs = nowMs;

    stats.totalDownBytes = m_sessionDown;
    stats.totalUpBytes = m_sessionUp;
    return stats;
}

NetworkStats NetworkMonitor::BuildDemoStats() {
    NetworkStats s;
    s.available = true;
    s.downBytesPerSec = 12.6 * 1024.0 * 1024.0; // 12.6 MB/s
    s.upBytesPerSec   = 1.42 * 1024.0 * 1024.0; // 1.42 MB/s
    s.totalDownBytes  = static_cast<uint64_t>(418ULL * 1024ULL * 1024ULL);
    s.totalUpBytes    = static_cast<uint64_t>(64ULL * 1024ULL * 1024ULL);
    s.adapterSummary  = L"Wi-Fi";
    s.updatedTickMs   = GetTickCount64();
    return s;
}

std::wstring NetworkMonitor::FormatSpeed(double bytesPerSec, NetUnitMode units, bool ultraShort) {
    if (bytesPerSec < 0.0) bytesPerSec = 0.0;
    wchar_t buf[48] = {};

    if (units == NetUnitMode::BitsPerSec) {
        double bits = bytesPerSec * 8.0;
        if (bits < 1000.0) {
            swprintf_s(buf, ultraShort ? L"%.0fb" : L"%.0f bps", bits);
        } else if (bits < 1000.0 * 1000.0) {
            double k = bits / 1000.0;
            if (k < 10.0)      swprintf_s(buf, ultraShort ? L"%.1fK" : L"%.1f Kbps", k);
            else               swprintf_s(buf, ultraShort ? L"%.0fK" : L"%.0f Kbps", k);
        } else if (bits < 1000.0 * 1000.0 * 1000.0) {
            double m = bits / (1000.0 * 1000.0);
            if (m < 100.0)     swprintf_s(buf, ultraShort ? L"%.1fM" : L"%.1f Mbps", m);
            else               swprintf_s(buf, ultraShort ? L"%.0fM" : L"%.0f Mbps", m);
        } else {
            double g = bits / (1000.0 * 1000.0 * 1000.0);
            swprintf_s(buf, ultraShort ? L"%.2fG" : L"%.2f Gbps", g);
        }
        return buf;
    }

    // BytesPerSec
    if (bytesPerSec < 1024.0) {
        swprintf_s(buf, ultraShort ? L"%.0fB" : L"%.0f B/s", bytesPerSec);
    } else if (bytesPerSec < 1024.0 * 1024.0) {
        double k = bytesPerSec / 1024.0;
        if (k < 10.0)          swprintf_s(buf, ultraShort ? L"%.1fK" : L"%.1f KB/s", k);
        else                   swprintf_s(buf, ultraShort ? L"%.0fK" : L"%.0f KB/s", k);
    } else if (bytesPerSec < 1024.0 * 1024.0 * 1024.0) {
        double m = bytesPerSec / (1024.0 * 1024.0);
        if (m < 100.0)         swprintf_s(buf, ultraShort ? L"%.1fM" : L"%.1f MB/s", m);
        else                   swprintf_s(buf, ultraShort ? L"%.0fM" : L"%.0f MB/s", m);
    } else {
        double g = bytesPerSec / (1024.0 * 1024.0 * 1024.0);
        swprintf_s(buf, ultraShort ? L"%.2fG" : L"%.2f GB/s", g);
    }
    return buf;
}

std::wstring NetworkMonitor::FormatBytesTotal(uint64_t bytes) {
    wchar_t buf[48] = {};
    double b = static_cast<double>(bytes);
    if (b < 1024.0) {
        swprintf_s(buf, L"%.0f B", b);
    } else if (b < 1024.0 * 1024.0) {
        swprintf_s(buf, L"%.1f KB", b / 1024.0);
    } else if (b < 1024.0 * 1024.0 * 1024.0) {
        swprintf_s(buf, L"%.1f MB", b / (1024.0 * 1024.0));
    } else {
        swprintf_s(buf, L"%.2f GB", b / (1024.0 * 1024.0 * 1024.0));
    }
    return buf;
}
