#include "battery_reader.h"

#include <windows.h>
#include <setupapi.h>
#include <devpropdef.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <iomanip>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>

namespace {

// {104EA319-6EE2-4701-BD47-8DDBF425BBE5}, 2 — Windows Bluetooth battery percentage (BYTE)
constexpr DEVPROPKEY kPkeyBatteryLevel = {
    { 0x104EA319, 0x6EE2, 0x4701, { 0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5 } },
    2
};

// {83DA6326-97A6-4088-9453-A1923F573B29}, 15 — DEVPKEY_Device_IsConnected (DEVPROP_TYPE_BOOLEAN)
constexpr DEVPROPKEY kPkeyIsConnected = {
    { 0x83DA6326, 0x97A6, 0x4088, { 0x94, 0x53, 0xA1, 0x92, 0x3F, 0x57, 0x3B, 0x29 } },
    15
};

// {2BD67D8B-8BEB-48D5-87E0-6CDA3428040A}, 4 — DEVPKEY_Bluetooth_ClassOfDevice (UINT32)
constexpr DEVPROPKEY kPkeyClassOfDevice = {
    { 0x2BD67D8B, 0x8BEB, 0x48D5, { 0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A } },
    4
};

// {A45C254E-DF1C-4EFD-8020-67D146A850E0}, 14 — DEVPKEY_Device_FriendlyName (STRING)
constexpr DEVPROPKEY kPkeyFriendlyName = {
    { 0xA45C254E, 0xDF1C, 0x4EFD, { 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0 } },
    14
};

// {A45C254E-DF1C-4EFD-8020-67D146A850E0}, 2 — DEVPKEY_Device_DeviceDesc (STRING)
constexpr DEVPROPKEY kPkeyDeviceDesc = {
    { 0xA45C254E, 0xDF1C, 0x4EFD, { 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0 } },
    2
};

// {8C7ED206-3F8A-4827-B3AB-AE9E1FAEFC6C}, 2 — DEVPKEY_Device_ContainerId (GUID)
constexpr DEVPROPKEY kPkeyContainerId = {
    { 0x8C7ED206, 0x3F8A, 0x4827, { 0xB3, 0xAB, 0xAE, 0x9E, 0x1F, 0xAE, 0xFC, 0x6C } },
    2
};

// {4340A6C5-93FA-4706-972C-7B648008A5A7}, 2 — DEVPKEY_Device_DevNodeStatus (UINT32)
constexpr DEVPROPKEY kPkeyDevNodeStatus = {
    { 0x4340A6C5, 0x93FA, 0x4706, { 0x97, 0x2C, 0x7B, 0x64, 0x80, 0x08, 0xA5, 0xA7 } },
    2
};

constexpr uint32_t kDnDeviceDisconnected = 0x02000000; // DN_NEEDS_LOCKING / DN_DEVICE_DISCONNECTED

std::wstring ToLower(const std::wstring& s) {
    std::wstring out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return out;
}

bool ContainsAny(const std::wstring& lower, const std::vector<const wchar_t*>& needles) {
    for (const wchar_t* n : needles) {
        if (lower.find(n) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

std::wstring StripProfileSuffix(std::wstring name) {
    static const std::vector<std::wstring> suffixes = {
        L" Hands-Free AG Audio",
        L" Hands-Free AG",
        L" Hands-Free Audio",
        L" Hands-Free",
        L" Stereo",
        L" Avrcp Transport",
        L" Battery Service"
    };
    for (const auto& suf : suffixes) {
        if (name.size() > suf.size()) {
            std::wstring tail = name.substr(name.size() - suf.size());
            if (ToLower(tail) == ToLower(suf)) {
                name.erase(name.size() - suf.size());
                break;
            }
        }
    }
    while (!name.empty() && (name.back() == L' ' || name.back() == L'-')) {
        name.pop_back();
    }
    return name;
}

std::wstring GuidToString(const GUID& g) {
    wchar_t buf[64] = {};
    swprintf_s(buf, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        g.Data1, g.Data2, g.Data3,
        g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
        g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return buf;
}

std::wstring FormatMac(uint64_t addr) {
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%012llx", static_cast<unsigned long long>(addr));
    return buf;
}

bool ParseHex12(const std::wstring& s, uint64_t& out) {
    if (s.size() != 12) return false;
    uint64_t val = 0;
    for (wchar_t c : s) {
        val <<= 4;
        if (c >= L'0' && c <= L'9')      val |= static_cast<uint64_t>(c - L'0');
        else if (c >= L'a' && c <= L'f') val |= static_cast<uint64_t>(c - L'a' + 10);
        else if (c >= L'A' && c <= L'F') val |= static_cast<uint64_t>(c - L'A' + 10);
        else return false;
    }
    out = val;
    return val != 0;
}

// Extract MAC from device node: "BTHLE\DEV_F4EE25FBBA46\..." or "BTHENUM\DEV_F5CFD77BCB0B\..."
std::optional<uint64_t> ExtractDevNodeMac(const std::wstring& instanceId) {
    size_t slash1 = instanceId.find(L'\\');
    if (slash1 == std::wstring::npos) return std::nullopt;
    size_t slash2 = instanceId.find(L'\\', slash1 + 1);
    std::wstring seg = (slash2 == std::wstring::npos)
        ? instanceId.substr(slash1 + 1)
        : instanceId.substr(slash1 + 1, slash2 - slash1 - 1);
    std::wstring upperSeg = seg;
    std::transform(upperSeg.begin(), upperSeg.end(), upperSeg.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towupper(c));
    });
    if (upperSeg.rfind(L"DEV_", 0) != 0) return std::nullopt;
    std::wstring macStr = upperSeg.substr(4);
    if (macStr.size() > 12) {
        macStr = macStr.substr(0, 12);
    }
    uint64_t mac = 0;
    if (ParseHex12(macStr, mac)) return mac;
    return std::nullopt;
}

// Extract MAC from profile/service node:
// e.g. "BTHENUM\{0000111E-...}_VID&...\7&3057B904&0&F5CFD77BCB0B_C00000000"
// or   "BTHLEDEVICE\{0000180F-...}_DEV_VID&...\8&...&F4EE25FBBA46_..."
std::optional<uint64_t> ExtractAnyMacFromInstanceId(const std::wstring& instanceId) {
    if (auto devMac = ExtractDevNodeMac(instanceId)) {
        return devMac;
    }
    size_t lastSlash = instanceId.rfind(L'\\');
    if (lastSlash == std::wstring::npos) return std::nullopt;
    std::wstring tail = instanceId.substr(lastSlash + 1);

    // Split by '&' and check segments (strip trailing "_...")
    size_t pos = 0;
    while (pos < tail.size()) {
        size_t amp = tail.find(L'&', pos);
        std::wstring part = (amp == std::wstring::npos) ? tail.substr(pos) : tail.substr(pos, amp - pos);
        size_t under = part.find(L'_');
        if (under != std::wstring::npos) {
            // Could be "BLUETOOTHDEVICE_F5CFD77BCB0B" or "F5CFD77BCB0B_C00000000"
            std::wstring left = part.substr(0, under);
            std::wstring right = part.substr(under + 1);
            uint64_t mac = 0;
            if (ParseHex12(left, mac)) return mac;
            if (ParseHex12(right, mac)) return mac;
        } else {
            uint64_t mac = 0;
            if (ParseHex12(part, mac)) return mac;
        }
        if (amp == std::wstring::npos) break;
        pos = amp + 1;
    }
    return std::nullopt;
}

struct PnpNode {
    std::wstring instanceId;
    std::wstring friendlyName;
    std::wstring containerId;
    std::optional<int> batteryLevel;
    std::optional<bool> isConnected;
    std::optional<uint32_t> classOfDevice;
    std::optional<uint32_t> devNodeStatus;
};

std::optional<std::vector<uint8_t>> ReadDevPropBytes(
    HDEVINFO devInfo, SP_DEVINFO_DATA& devData, const DEVPROPKEY& key, DEVPROPTYPE* outType = nullptr)
{
    DEVPROPTYPE propType = 0;
    DWORD required = 0;
    SetupDiGetDevicePropertyW(devInfo, &devData, &key, &propType, nullptr, 0, &required, 0);
    if (required == 0 || required > 4096) {
        return std::nullopt;
    }
    std::vector<uint8_t> buf(required);
    if (!SetupDiGetDevicePropertyW(devInfo, &devData, &key, &propType, buf.data(), required, &required, 0)) {
        return std::nullopt;
    }
    if (outType) *outType = propType;
    buf.resize(required);
    return buf;
}

std::wstring ReadDevPropString(HDEVINFO devInfo, SP_DEVINFO_DATA& devData, const DEVPROPKEY& key) {
    DEVPROPTYPE type = 0;
    auto bytes = ReadDevPropBytes(devInfo, devData, key, &type);
    if (!bytes || type != DEVPROP_TYPE_STRING || bytes->size() < sizeof(wchar_t)) {
        return {};
    }
    const wchar_t* ptr = reinterpret_cast<const wchar_t*>(bytes->data());
    size_t chars = bytes->size() / sizeof(wchar_t);
    size_t len = 0;
    while (len < chars && ptr[len] != L'\0') ++len;
    return std::wstring(ptr, len);
}

std::vector<PnpNode> EnumeratePnpNodes(const wchar_t* enumerator) {
    std::vector<PnpNode> out;
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, enumerator, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        return out;
    }

    SP_DEVINFO_DATA data = {};
    data.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &data); ++i) {
        wchar_t idBuf[512] = {};
        if (!SetupDiGetDeviceInstanceIdW(set, &data, idBuf, static_cast<DWORD>(std::size(idBuf)), nullptr)) {
            continue;
        }
        PnpNode node;
        node.instanceId = idBuf;

        node.friendlyName = ReadDevPropString(set, data, kPkeyFriendlyName);
        if (node.friendlyName.empty()) {
            node.friendlyName = ReadDevPropString(set, data, kPkeyDeviceDesc);
        }

        DEVPROPTYPE type = 0;
        if (auto b = ReadDevPropBytes(set, data, kPkeyBatteryLevel, &type)) {
            if (type == DEVPROP_TYPE_BYTE && b->size() >= 1) {
                int v = (*b)[0];
                if (v >= 0 && v <= 100) node.batteryLevel = v;
            } else if ((type == DEVPROP_TYPE_UINT32 || type == DEVPROP_TYPE_INT32) && b->size() >= 4) {
                uint32_t v = 0;
                memcpy(&v, b->data(), 4);
                if (v <= 100) node.batteryLevel = static_cast<int>(v);
            }
        }

        if (auto b = ReadDevPropBytes(set, data, kPkeyIsConnected, &type)) {
            if (type == DEVPROP_TYPE_BOOLEAN && !b->empty()) {
                node.isConnected = ((*b)[0] != 0);
            }
        }

        if (auto b = ReadDevPropBytes(set, data, kPkeyClassOfDevice, &type)) {
            if (b->size() >= 4) {
                uint32_t cod = 0;
                memcpy(&cod, b->data(), 4);
                node.classOfDevice = cod;
            }
        }

        if (auto b = ReadDevPropBytes(set, data, kPkeyDevNodeStatus, &type)) {
            if (b->size() >= 4) {
                uint32_t status = 0;
                memcpy(&status, b->data(), 4);
                node.devNodeStatus = status;
            }
        }

        if (auto b = ReadDevPropBytes(set, data, kPkeyContainerId, &type)) {
            if (type == DEVPROP_TYPE_GUID && b->size() >= sizeof(GUID)) {
                GUID g = {};
                memcpy(&g, b->data(), sizeof(GUID));
                node.containerId = GuidToString(g);
            }
        }

        out.push_back(std::move(node));
    }

    SetupDiDestroyDeviceInfoList(set);
    return out;
}

DeviceKind ClassifyClassicCoD(uint32_t cod, const std::wstring& name) {
    uint32_t major = (cod >> 8) & 0x1F;
    uint32_t minor = (cod >> 2) & 0x3F;
    if (major == 0x04) { // Audio/Video
        DeviceKind byName = BatteryReader::ClassifyByName(name);
        if (byName == DeviceKind::Earbuds) return DeviceKind::Earbuds;
        return DeviceKind::Headphones;
    }
    if (major == 0x05) { // Peripheral
        if (minor & 0x20) return DeviceKind::Mouse;
        if (minor & 0x10) return DeviceKind::Keyboard;
        return DeviceKind::Gamepad;
    }
    return BatteryReader::ClassifyByName(name);
}

bool IsPhoneOrComputerCoD(uint32_t cod) {
    uint32_t major = (cod >> 8) & 0x1F;
    return (major == 0x01 || major == 0x02); // Computer or Phone
}

struct BleReadResult {
    bool skipDevice = false;
    bool connected = false;
    std::optional<int> percent;
    std::optional<DeviceKind> kind;
    std::wstring name;
    std::wstring note;
};

template <typename AsyncOp>
bool WaitAsyncCompleted(const AsyncOp& op, int timeoutSec = 4) {
    auto st = op.wait_for(std::chrono::seconds(timeoutSec));
    if (st == winrt::Windows::Foundation::AsyncStatus::Completed) {
        return true;
    }
    if (st == winrt::Windows::Foundation::AsyncStatus::Started) {
        try { op.Cancel(); } catch (...) {}
    }
    return false;
}

BleReadResult ReadBleGattBattery(uint64_t addr) {
    using namespace winrt::Windows::Devices::Bluetooth;
    using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
    using namespace winrt::Windows::Storage::Streams;

    BleReadResult res;
    try {
        auto devOp = BluetoothLEDevice::FromBluetoothAddressAsync(addr);
        if (!WaitAsyncCompleted(devOp, 4)) {
            res.note = L"FromBluetoothAddressAsync timed out";
            return res;
        }
        BluetoothLEDevice dev = devOp.GetResults();
        if (!dev) {
            res.note = L"FromBluetoothAddressAsync returned null";
            return res;
        }

        auto closeGuard = [&]() {
            try { dev.Close(); } catch (...) {}
        };

        res.name = dev.Name().c_str();
        res.connected = (dev.ConnectionStatus() == BluetoothConnectionStatus::Connected);
        if (!res.connected) {
            res.note = L"BLE not connected";
            closeGuard();
            return res;
        }

        uint16_t appearance = 0;
        try {
            if (auto app = dev.Appearance()) {
                appearance = app.RawValue();
            }
        } catch (...) {}

        uint16_t category = static_cast<uint16_t>(appearance >> 6);
        uint16_t subcat   = static_cast<uint16_t>(appearance & 0x3F);
        if (category == 0x01 || category == 0x02) {
            res.skipDevice = true;
            res.note = L"Phone or computer skipped";
            closeGuard();
            return res;
        }
        if (category == 0x0F) { // HID
            if (subcat == 1)      res.kind = DeviceKind::Keyboard;
            else if (subcat == 2) res.kind = DeviceKind::Mouse;
            else if (subcat == 3 || subcat == 4) res.kind = DeviceKind::Gamepad;
        } else if (category == 0x25) { // Wearable Audio
            if (subcat == 1)      res.kind = DeviceKind::Earbuds;
            else if (subcat == 2 || subcat == 3) res.kind = DeviceKind::Headphones;
        }

        GattDeviceServicesResult svcResult = nullptr;
        auto svcOp = dev.GetGattServicesForUuidAsync(
            GattServiceUuids::Battery(), BluetoothCacheMode::Cached);
        if (WaitAsyncCompleted(svcOp, 4)) {
            svcResult = svcOp.GetResults();
        }
        if (!svcResult || svcResult.Status() != GattCommunicationStatus::Success || svcResult.Services().Size() == 0) {
            // Try uncached service lookup once if cached missed it
            auto svcUncachedOp = dev.GetGattServicesForUuidAsync(
                GattServiceUuids::Battery(), BluetoothCacheMode::Uncached);
            if (WaitAsyncCompleted(svcUncachedOp, 4)) {
                svcResult = svcUncachedOp.GetResults();
            }
        }
        if (svcResult && svcResult.Status() == GattCommunicationStatus::Success && svcResult.Services().Size() > 0) {
            auto svc = svcResult.Services().GetAt(0);
            GattCharacteristicsResult charResult = nullptr;
            auto charOp = svc.GetCharacteristicsForUuidAsync(
                GattCharacteristicUuids::BatteryLevel(), BluetoothCacheMode::Cached);
            if (WaitAsyncCompleted(charOp, 4)) {
                charResult = charOp.GetResults();
            }
            if (!charResult || charResult.Status() != GattCommunicationStatus::Success || charResult.Characteristics().Size() == 0) {
                auto charUncachedOp = svc.GetCharacteristicsForUuidAsync(
                    GattCharacteristicUuids::BatteryLevel(), BluetoothCacheMode::Uncached);
                if (WaitAsyncCompleted(charUncachedOp, 4)) {
                    charResult = charUncachedOp.GetResults();
                }
            }
            if (charResult && charResult.Status() == GattCommunicationStatus::Success && charResult.Characteristics().Size() > 0) {
                auto ch = charResult.Characteristics().GetAt(0);
                // Prefer Uncached for fresh reading; fall back to Cached if sleepy mouse didn't wake
                GattReadResult valResult = nullptr;
                auto valOp = ch.ReadValueAsync(BluetoothCacheMode::Uncached);
                if (WaitAsyncCompleted(valOp, 4)) {
                    valResult = valOp.GetResults();
                }
                if (!valResult || valResult.Status() != GattCommunicationStatus::Success) {
                    auto valCachedOp = ch.ReadValueAsync(BluetoothCacheMode::Cached);
                    if (WaitAsyncCompleted(valCachedOp, 4)) {
                        valResult = valCachedOp.GetResults();
                    }
                }
                if (valResult && valResult.Status() == GattCommunicationStatus::Success &&
                    valResult.Value() && valResult.Value().Length() >= 1)
                {
                    auto reader = DataReader::FromBuffer(valResult.Value());
                    uint8_t lvl = reader.ReadByte();
                    if (lvl <= 100) {
                        res.percent = static_cast<int>(lvl);
                        res.note = L"GATT 0x180F/0x2A19";
                    }
                } else {
                    res.note = L"GATT read failed";
                }
            } else {
                res.note = L"No BatteryLevel characteristic";
            }
            for (uint32_t i = 0; i < svcResult.Services().Size(); ++i) {
                try { svcResult.Services().GetAt(i).Close(); } catch (...) {}
            }
        } else {
            res.note = L"No GATT Battery Service";
        }

        closeGuard();
    } catch (const winrt::hresult_error& e) {
        std::wstringstream ss;
        ss << L"WinRT error 0x" << std::hex << static_cast<uint32_t>(e.code().value);
        res.note = ss.str();
    } catch (...) {
        res.note = L"Unknown BLE error";
    }
    return res;
}

std::wstring FormatDuration(DWORD seconds) {
    if (seconds == static_cast<DWORD>(-1) || seconds == 0 || seconds > 86400 * 3) {
        return {};
    }
    DWORD mins = (seconds + 30) / 60;
    DWORD hrs = mins / 60;
    mins %= 60;
    std::wstringstream ss;
    if (hrs > 0) {
        ss << hrs << L"h " << mins << L"m remaining";
    } else {
        ss << mins << L"m remaining";
    }
    return ss.str();
}

} // namespace

namespace BatteryReader {

DeviceKind ClassifyByName(const std::wstring& name) {
    std::wstring low = ToLower(name);
    if (ContainsAny(low, {
        L"mouse", L"mx master", L"mx anywhere", L"mx vertical", L"mx ergo",
        L"pebble", L"m350", L"m590", L"m720", L"m650", L"lift",
        L"g305", L"g604", L"g903", L"pro x superlight",
        L"deathadder", L"basilisk", L"orochi", L"viper", L"pro click",
        L"aerox", L"rival", L"arc touch", L"sculpt", L"trackball", L"trackpad"
    })) {
        return DeviceKind::Mouse;
    }
    if (ContainsAny(low, {
        L"airpods", L"earbud", L"buds", L"wf-", L"freebuds", L"jabra elite",
        L"liberty", L"space a40", L"pixel buds", L"powerbeats", L"ear ("
    })) {
        if (low.find(L"airpods max") != std::wstring::npos) {
            return DeviceKind::Headphones;
        }
        return DeviceKind::Earbuds;
    }
    if (ContainsAny(low, {
        L"headphone", L"headset", L"wh-", L"quietcomfort", L"qc35", L"qc45", L"qc ultra",
        L"xm3", L"xm4", L"xm5", L"bose", L"sennheiser", L"momentum", L"soundcore",
        L"space q", L"space one", L"q30", L"q35", L"q45", L"beats", L"studio3",
        L"arctis", L"barracuda", L"opus", L"jabra", L"marshall", L"major",
        L"ath-", L"jbl", L"sony", L"anker", L"earphone", L"audio"
    })) {
        return DeviceKind::Headphones;
    }
    if (ContainsAny(low, {
        L"keyboard", L"keychron", L"mx keys", L"k380", L"k780", L"k860",
        L"nuphy", L"hhkb", L"anne pro", L"magic keyboard", L"g915"
    })) {
        return DeviceKind::Keyboard;
    }
    if (ContainsAny(low, {
        L"controller", L"gamepad", L"xbox", L"dualsense", L"dualshock", L"8bitdo"
    })) {
        return DeviceKind::Gamepad;
    }
    return DeviceKind::Bluetooth;
}

ScanResult ScanAll(bool includeLaptop, bool includeDisconnected) {
    ScanResult result;
    std::wstringstream diag;
    uint64_t nowMs = GetTickCount64();

    // 1. Laptop / System Battery
    SYSTEM_POWER_STATUS sps = {};
    if (GetSystemPowerStatus(&sps)) {
        bool hasBattery = ((sps.BatteryFlag & 128) == 0) && (sps.BatteryLifePercent <= 100);
        diag << L"[Laptop] hasBattery=" << (hasBattery ? L"yes" : L"no")
             << L" pct=" << static_cast<int>(sps.BatteryLifePercent)
             << L" ac=" << static_cast<int>(sps.ACLineStatus)
             << L" flag=0x" << std::hex << static_cast<int>(sps.BatteryFlag) << std::dec << L"\r\n";

        if (includeLaptop && hasBattery) {
            BatteryDevice laptop;
            laptop.id = L"laptop";
            laptop.name = L"Laptop Battery";
            laptop.kind = DeviceKind::Laptop;
            laptop.percent = static_cast<int>(sps.BatteryLifePercent);
            laptop.connected = true;
            laptop.pluggedIn = (sps.ACLineStatus == 1);
            laptop.charging = ((sps.BatteryFlag & 8) != 0);
            laptop.sourceLabel = laptop.pluggedIn ? L"AC Power" : L"On Battery";
            if (laptop.charging) {
                laptop.extraStatus = L"Plugged in, charging";
            } else if (laptop.pluggedIn && laptop.percent >= 99) {
                laptop.extraStatus = L"Fully charged";
            } else if (laptop.pluggedIn) {
                laptop.extraStatus = L"Plugged in";
            } else {
                laptop.extraStatus = FormatDuration(sps.BatteryLifeTime);
                if (laptop.extraStatus.empty()) {
                    laptop.extraStatus = L"Discharging";
                }
            }
            laptop.updatedTickMs = nowMs;
            result.devices.push_back(std::move(laptop));
        }
    }

    // Track deduplicated Bluetooth devices by MAC (or containerId fallback)
    struct BtCandidate {
        std::wstring key;
        uint64_t mac = 0;
        std::wstring containerId;
        std::wstring name;
        std::wstring sourceLabel;
        DeviceKind kind = DeviceKind::Bluetooth;
        bool kindFromHardware = false;
        int percent = -1;
        bool connected = false;
        bool skip = false;
    };

    std::map<std::wstring, BtCandidate> byKey;
    std::unordered_map<std::wstring, std::wstring> containerToKey;

    auto ensureByMac = [&](uint64_t mac, const std::wstring& prefix) -> BtCandidate& {
        std::wstring k = FormatMac(mac);
        auto it = byKey.find(k);
        if (it == byKey.end()) {
            BtCandidate c;
            c.key = prefix + L":" + k;
            c.mac = mac;
            it = byKey.emplace(k, std::move(c)).first;
        }
        return it->second;
    };

    // 2. Classic Bluetooth (BTHENUM) via SetupAPI
    auto bthenumNodes = EnumeratePnpNodes(L"BTHENUM");
    diag << L"[BTHENUM] nodes=" << bthenumNodes.size() << L"\r\n";

    // Pass 2a: Collect device root nodes (BTHENUM\DEV_<MAC>)
    for (const auto& n : bthenumNodes) {
        auto devMac = ExtractDevNodeMac(n.instanceId);
        if (!devMac) continue;
        auto& c = ensureByMac(*devMac, L"bt");
        if (!n.friendlyName.empty()) c.name = n.friendlyName;
        if (!n.containerId.empty()) {
            c.containerId = n.containerId;
            containerToKey[n.containerId] = FormatMac(*devMac);
        }
        if (n.isConnected.has_value()) {
            c.connected = *n.isConnected;
        } else if (n.devNodeStatus.has_value()) {
            c.connected = ((*n.devNodeStatus & kDnDeviceDisconnected) == 0);
        }
        if (n.classOfDevice.has_value()) {
            if (IsPhoneOrComputerCoD(*n.classOfDevice)) {
                c.skip = true;
            } else {
                c.kind = ClassifyClassicCoD(*n.classOfDevice, c.name);
                c.kindFromHardware = true;
            }
        }
        if (n.batteryLevel.has_value()) {
            c.percent = *n.batteryLevel;
            c.sourceLabel = L"Bluetooth";
        }
    }

    // Pass 2b: Collect profile nodes carrying battery level ({104EA319-...} 2)
    for (const auto& n : bthenumNodes) {
        if (!n.batteryLevel.has_value()) continue;

        BtCandidate* target = nullptr;
        if (auto mac = ExtractAnyMacFromInstanceId(n.instanceId)) {
            target = &ensureByMac(*mac, L"bt");
        } else if (!n.containerId.empty()) {
            auto cit = containerToKey.find(n.containerId);
            if (cit != containerToKey.end() && byKey.count(cit->second)) {
                target = &byKey[cit->second];
            }
        }
        if (!target) continue;

        target->percent = *n.batteryLevel;
        if (target->sourceLabel.empty()) target->sourceLabel = L"Bluetooth (HFP)";
        if (target->name.empty() && !n.friendlyName.empty()) {
            target->name = StripProfileSuffix(n.friendlyName);
        }
        if (!target->connected && n.isConnected.value_or(false)) {
            target->connected = true;
        }
        diag << L"  BTHENUM battery: " << target->name << L" (" << target->key << L") = "
             << target->percent << L"% connected=" << (target->connected ? L"yes" : L"no") << L"\r\n";
    }

    // 3. Bluetooth LE (BTHLE + BTHLEDEVICE) via SetupAPI
    auto bthleNodes = EnumeratePnpNodes(L"BTHLE");
    auto bthleDevNodes = EnumeratePnpNodes(L"BTHLEDEVICE");
    diag << L"[BTHLE] nodes=" << bthleNodes.size() << L" [BTHLEDEVICE] nodes=" << bthleDevNodes.size() << L"\r\n";

    std::set<uint64_t> bleMacsToProbe;

    for (const auto& n : bthleNodes) {
        auto devMac = ExtractDevNodeMac(n.instanceId);
        if (!devMac) continue;
        auto& c = ensureByMac(*devMac, L"ble");
        if (!n.friendlyName.empty()) c.name = n.friendlyName;
        if (!n.containerId.empty()) {
            c.containerId = n.containerId;
            containerToKey[n.containerId] = FormatMac(*devMac);
        }
        bool conn = n.isConnected.value_or(true);
        if (!n.isConnected.has_value() && n.devNodeStatus.has_value()) {
            conn = ((*n.devNodeStatus & kDnDeviceDisconnected) == 0);
        }
        if (conn) {
            c.connected = true;
            bleMacsToProbe.insert(*devMac);
        }
        if (n.batteryLevel.has_value()) {
            c.percent = *n.batteryLevel;
            c.sourceLabel = L"Bluetooth LE";
        }
    }

    for (const auto& n : bthleDevNodes) {
        if (!n.batteryLevel.has_value()) continue;
        BtCandidate* target = nullptr;
        if (!n.containerId.empty()) {
            auto cit = containerToKey.find(n.containerId);
            if (cit != containerToKey.end() && byKey.count(cit->second)) {
                target = &byKey[cit->second];
            }
        }
        if (!target) {
            if (auto mac = ExtractAnyMacFromInstanceId(n.instanceId)) {
                target = &ensureByMac(*mac, L"ble");
            }
        }
        if (target) {
            target->percent = *n.batteryLevel;
            target->sourceLabel = L"Bluetooth LE";
            if (target->name.empty() && !n.friendlyName.empty()) {
                target->name = StripProfileSuffix(n.friendlyName);
            }
        }
    }

    // 3b. HID fallback for BLE/BT HID devices exposing battery via ContainerId
    auto hidNodes = EnumeratePnpNodes(L"HID");
    for (const auto& n : hidNodes) {
        if (!n.batteryLevel.has_value() || n.containerId.empty()) continue;
        auto cit = containerToKey.find(n.containerId);
        if (cit != containerToKey.end() && byKey.count(cit->second)) {
            auto& target = byKey[cit->second];
            if (target.percent < 0) {
                target.percent = *n.batteryLevel;
                if (target.sourceLabel.empty()) target.sourceLabel = L"Bluetooth HID";
            }
        }
    }

    // 4. Live BLE GATT Battery Service (0x180F / 0x2A19) via C++/WinRT
    for (uint64_t mac : bleMacsToProbe) {
        auto& c = ensureByMac(mac, L"ble");
        if (c.skip) continue;
        BleReadResult ble = ReadBleGattBattery(mac);
        diag << L"  BLE GATT " << FormatMac(mac) << L" (" << (ble.name.empty() ? c.name : ble.name)
             << L"): conn=" << (ble.connected ? L"yes" : L"no")
             << L" pct=" << (ble.percent ? std::to_wstring(*ble.percent) : L"none")
             << L" [" << ble.note << L"]\r\n";

        if (ble.skipDevice) {
            c.skip = true;
            continue;
        }
        if (!ble.name.empty()) {
            c.name = ble.name;
        }
        if (ble.kind.has_value()) {
            c.kind = *ble.kind;
            c.kindFromHardware = true;
        }
        if (ble.connected) {
            c.connected = true;
        }
        if (ble.percent.has_value()) {
            c.percent = *ble.percent;
            c.sourceLabel = L"Bluetooth LE";
        }
    }

    // 5. Finalize Bluetooth devices
    for (auto& kv : byKey) {
        auto& c = kv.second;
        if (c.skip) continue;
        if (c.percent < 0 || c.percent > 100) continue;
        if (!c.connected && !includeDisconnected) continue;

        BatteryDevice dev;
        dev.id = c.key;
        dev.name = c.name.empty() ? L"Bluetooth Device" : StripProfileSuffix(c.name);
        dev.kind = c.kindFromHardware ? c.kind : ClassifyByName(dev.name);
        // Refine generic Bluetooth classification if hardware class was generic
        if (dev.kind == DeviceKind::Bluetooth) {
            dev.kind = ClassifyByName(dev.name);
        }
        dev.percent = std::clamp(c.percent, 0, 100);
        dev.connected = c.connected;
        dev.sourceLabel = c.sourceLabel.empty() ? L"Bluetooth" : c.sourceLabel;
        if (!c.connected) {
            dev.extraStatus = L"Disconnected (last known)";
        } else if (dev.percent <= 20) {
            dev.extraStatus = L"Low battery";
        } else {
            dev.extraStatus = L"Connected";
        }
        dev.updatedTickMs = nowMs;
        result.devices.push_back(std::move(dev));
    }

    // Deterministic clean order: Laptop -> Mouse -> Headphones -> Earbuds -> Keyboard -> Gamepad -> Other
    std::sort(result.devices.begin(), result.devices.end(), [](const BatteryDevice& a, const BatteryDevice& b) {
        int oa = DeviceSortOrder(a.kind);
        int ob = DeviceSortOrder(b.kind);
        if (oa != ob) return oa < ob;
        return ToLower(a.name) < ToLower(b.name);
    });

    result.diagnostics = diag.str();
    return result;
}

std::vector<BatteryDevice> BuildDemoDevices(int preset) {
    uint64_t nowMs = GetTickCount64();
    std::vector<BatteryDevice> list;

    if (preset == 0) {
        // Classic 3-device demo: Laptop (charging green), Mouse (good green), Headphones (low red)
        list.push_back({
            L"laptop", L"Laptop Battery", L"AC Power", L"Plugged in, charging",
            DeviceKind::Laptop, 84, true, true, true, nowMs
        });
        list.push_back({
            L"ble:a1b2c3d4e5f6", L"MX Master 3S", L"Bluetooth LE", L"Connected",
            DeviceKind::Mouse, 62, true, false, false, nowMs
        });
        list.push_back({
            L"bt:112233445566", L"WH-1000XM4", L"Bluetooth (HFP)", L"Low battery",
            DeviceKind::Headphones, 18, true, false, false, nowMs
        });
    } else if (preset == 1) {
        // 4-device demo showing Full green, Amber warning, Mid green, Low red
        list.push_back({
            L"laptop", L"Laptop Battery", L"On Battery", L"4h 15m remaining",
            DeviceKind::Laptop, 95, true, false, false, nowMs
        });
        list.push_back({
            L"ble:a1b2c3d4e5f6", L"Logitech Pebble M350", L"Bluetooth LE", L"Connected",
            DeviceKind::Mouse, 34, true, false, false, nowMs
        });
        list.push_back({
            L"bt:112233445566", L"Soundcore Space Q45", L"Bluetooth (HFP)", L"Connected",
            DeviceKind::Headphones, 76, true, false, false, nowMs
        });
        list.push_back({
            L"ble:998877665544", L"Keychron K3 Pro", L"Bluetooth LE", L"Low battery",
            DeviceKind::Keyboard, 14, true, false, false, nowMs
        });
    } else {
        // 2-device BT-only mode (when user hides Laptop battery to sit beside existing BatteryBar)
        list.push_back({
            L"ble:a1b2c3d4e5f6", L"MX Master 3S", L"Bluetooth LE", L"Connected",
            DeviceKind::Mouse, 78, true, false, false, nowMs
        });
        list.push_back({
            L"bt:112233445566", L"WH-1000XM4", L"Bluetooth (HFP)", L"Connected",
            DeviceKind::Headphones, 35, true, false, false, nowMs
        });
    }

    return list;
}

} // namespace BatteryReader
