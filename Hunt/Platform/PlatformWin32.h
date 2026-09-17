#pragma once

#include <windows.h>

// Temporary Win32 bridge for the engine WndProc and native WGL/GDI/audio users.
// Do not include this header in portable policy code.
namespace Platform::Win32 {
void SetGameWindow(HWND window);
HWND GameWindow();
} // namespace Platform::Win32
