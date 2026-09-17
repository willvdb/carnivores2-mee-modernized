#pragma once

#include <windows.h>
#include "Platform.h"

// Temporary Win32 bridge for the engine WndProc and native WGL/GDI/audio users.
// Do not include this header in portable policy code.
namespace Platform::Win32 {
void Initialize(HINSTANCE instance, WNDPROC procedure);
HWND GameWindow();
// Use the callback's HWND, including during synchronous window creation.
bool IsWindowActive(HWND window);
inline KeyEvent DecodeKeyEvent(WPARAM key, LPARAM data, bool system, bool shift)
{
    unsigned int side = static_cast<unsigned int>(key);
    if (key == VK_SHIFT)
        side = MapVirtualKey((data >> 16) & 0xFF, MAPVK_VSC_TO_VK_EX);
    else if (key == VK_CONTROL)
        side = (data & (1u << 24)) ? VK_RCONTROL : VK_LCONTROL;
    else if (key == VK_MENU)
        side = (data & (1u << 24)) ? VK_RMENU : VK_LMENU;
    return {static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(side),
            (data & (1u << 30)) != 0, system, shift};
}
} // namespace Platform::Win32
