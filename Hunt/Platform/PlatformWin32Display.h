#pragma once

#include "Platform.h"
#include <windows.h>
#include <string>

// Native backend implementation details. No device names enter Platform.h.
namespace Platform::Win32Details {
struct NativeDisplay { DisplayBounds bounds; std::string device; };

inline std::optional<NativeDisplay> MapDisplayTarget(const std::vector<NativeDisplay>& displays,
                                                    DisplayTarget target)
{
    for (const auto& display : displays)
        if (EqualDisplayBounds(display.bounds, target.bounds)) return display;
    return std::nullopt;
}

// Remember only a device actually changed by this backend. The callback seam
// exercises mode/restore bookkeeping without changing a CI runner's desktop.
struct DisplayModeChange {
    std::optional<std::string> device;

    template<class Change>
    LONG Apply(const char* selectedDevice, DEVMODEA& mode, Change change)
    {
        const auto status = change(selectedDevice, &mode, CDS_FULLSCREEN);
        if (status == DISP_CHANGE_SUCCESSFUL && selectedDevice) device = selectedDevice;
        return status;
    }

    template<class Change>
    LONG Restore(Change change)
    {
        const auto status = change(device ? device->c_str() : nullptr, nullptr, 0);
        // Keep failed restorations available for a later shutdown retry.
        if (status == DISP_CHANGE_SUCCESSFUL) device.reset();
        return status;
    }
};
} // namespace Platform::Win32Details
