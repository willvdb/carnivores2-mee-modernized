#include "Platform.h"
#include "PlatformWin32.h"
#include <mmsystem.h>

namespace {
HWND gameWindow = nullptr;
}

namespace Platform::Win32 {
void SetGameWindow(HWND window) { gameWindow = window; }
HWND GameWindow() { return gameWindow; }
}

namespace Platform {

DisplayInfo QueryDisplayInfo()
{
    DisplayInfo info;
    info.desktop = {GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    DEVMODE current = {};
    current.dmSize = sizeof(current);
    if (EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &current)) {
        info.desktop = {static_cast<std::int32_t>(current.dmPelsWidth),
                        static_cast<std::int32_t>(current.dmPelsHeight)};
    }

    DEVMODE mode = {};
    mode.dmSize = sizeof(mode);
    for (int i = 0; EnumDisplaySettings(nullptr, i, &mode); ++i) {
        info.modes.push_back({{static_cast<std::int32_t>(mode.dmPelsWidth),
                               static_cast<std::int32_t>(mode.dmPelsHeight)},
                              mode.dmBitsPerPel});
    }
    return info;
}

Tick CounterFrequency()
{
    LARGE_INTEGER frequency = {};
    QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart;
}

Tick Counter()
{
    LARGE_INTEGER counter = {};
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

void BeginFrameTiming() { timeBeginPeriod(1); }
void SleepMilliseconds(std::uint32_t milliseconds) { Sleep(milliseconds); }

bool PollKeyboardState(KeyboardState& state)
{
    return GetKeyboardState(state) != FALSE;
}

void SetMouseCapture(bool capture)
{
    if (!gameWindow) return;
    if (capture) {
        RECT rect;
        GetClientRect(gameWindow, &rect);
        POINT p1 = {rect.left, rect.top};
        POINT p2 = {rect.right, rect.bottom};
        ClientToScreen(gameWindow, &p1);
        ClientToScreen(gameWindow, &p2);
        SetRect(&rect, p1.x, p1.y, p2.x, p2.y);
        ClipCursor(&rect);
        while (ShowCursor(false) >= 0);
    } else {
        ClipCursor(nullptr);
        while (ShowCursor(true) < 0);
    }
}

void WarpPointerInClient(Point position)
{
    if (!gameWindow) return;
    POINT point = {position.x, position.y};
    ClientToScreen(gameWindow, &point);
    SetCursorPos(point.x, point.y);
}

Point PointerInClient()
{
    POINT point;
    GetCursorPos(&point);
    ScreenToClient(gameWindow, &point);
    return {point.x, point.y};
}

} // namespace Platform
