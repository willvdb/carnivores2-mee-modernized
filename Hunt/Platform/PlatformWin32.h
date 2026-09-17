#pragma once

#include <windows.h>

// Temporary Win32 bridge for the engine WndProc and native WGL/GDI/audio users.
// Do not include this header in portable policy code.
namespace Platform::Win32 {
void Initialize(HINSTANCE instance, WNDPROC procedure);
HWND GameWindow();
// Use the callback's HWND, including during synchronous window creation.
bool IsWindowActive(HWND window);
} // namespace Platform::Win32
