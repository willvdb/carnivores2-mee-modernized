#pragma once
#include "DisplayIdentityMapping.h"
#include <windows.h>

namespace Platform::Win32Details {
// EDD_GET_DEVICE_INTERFACE_NAME returns the OS-registered monitor interface,
// not DeviceName (the session GDI source) or DeviceString (a friendly label).
// Template seam tests enumeration/clone/error handling without a physical device.
template<class Enumerate>
std::optional<DisplayIdentity> ReadMonitorIdentity(const wchar_t* source, Enumerate enumerate)
{
    std::optional<DisplayIdentity> result;
    unsigned active = 0;
    for (DWORD index = 0;; ++index) {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        if (!enumerate(source, index, &device, EDD_GET_DEVICE_INTERFACE_NAME)) break;
        if (!(device.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;
        if (++active > 1) return std::nullopt; // Clones: no single target identity.
        std::size_t length = 0;
        while (length < 128 && device.DeviceID[length]) ++length;
        if (!length || length >= 127) continue; // Never accept a truncated path.
        CharLowerBuffW(device.DeviceID, static_cast<DWORD>(length));
        DisplayIdentity identity{1, "win-monitor-interface", {}};
        constexpr char hex[] = "0123456789abcdef";
        for (std::size_t i = 0; i < length; ++i) {
            const auto unit = static_cast<std::uint16_t>(device.DeviceID[i]);
            for (int shift = 12; shift >= 0; shift -= 4) identity.value += hex[(unit >> shift) & 15];
        }
        result = std::move(identity);
    }
    return result;
}
template<class ReadInfo, class Enumerate>
std::optional<DisplayIdentityDetails::NativeIdentity> ReadMonitorIdentityEntry(
    HMONITOR monitor, ReadInfo readInfo, Enumerate enumerate)
{
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!readInfo(monitor, reinterpret_cast<MONITORINFO*>(&info))) return std::nullopt;
    return DisplayIdentityDetails::NativeIdentity{
        {{info.rcMonitor.left, info.rcMonitor.top},
         {info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top}},
        ReadMonitorIdentity(info.szDevice, enumerate)};
}
inline BOOL CALLBACK CollectMonitorIdentity(HMONITOR monitor, HDC, LPRECT, LPARAM parameter)
{
    auto entry = ReadMonitorIdentityEntry(monitor, GetMonitorInfoW, EnumDisplayDevicesW);
    // Unknown bounds could conceal a duplicate rectangle. Abort the snapshot,
    // rather than declaring a remaining entry uniquely mapped from partial data.
    if (!entry) return FALSE;
    auto& entries = *reinterpret_cast<std::vector<DisplayIdentityDetails::NativeIdentity>*>(parameter);
    entries.push_back(std::move(*entry));
    return TRUE;
}
inline void DiscoverMonitorIdentities(DisplayCatalog& catalog)
{
    std::vector<DisplayIdentityDetails::NativeIdentity> entries;
    if (EnumDisplayMonitors(nullptr, nullptr, CollectMonitorIdentity, reinterpret_cast<LPARAM>(&entries)))
        DisplayIdentityDetails::Associate(catalog, entries);
}
} // namespace Platform::Win32Details
