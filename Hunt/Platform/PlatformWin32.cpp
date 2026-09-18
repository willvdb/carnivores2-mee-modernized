#include "Platform.h"
#include "PlatformWin32.h"
#include <mmsystem.h>
#include <utility>

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

namespace {
HWND gameWindow = nullptr;
HINSTANCE gameInstance = nullptr;
WNDPROC gameProcedure = nullptr;
HCURSOR arrowCursor = nullptr;

Platform::DisplayMode CopyDisplayMode(const DEVMODEA& mode)
{
    // DEVMODE exposes integer Hz; 0 and 1 both mean the hardware default.
    const auto refresh = mode.dmDisplayFrequency > 1
        ? Platform::MakeRefreshRate(mode.dmDisplayFrequency, 1) : Platform::RefreshRate{};
    return {{static_cast<std::int32_t>(mode.dmPelsWidth),
             static_cast<std::int32_t>(mode.dmPelsHeight)}, mode.dmBitsPerPel, refresh};
}

Platform::Display ReadDisplay(const char* device)
{
    Platform::Display display;
    DEVMODEA current{};
    current.dmSize = sizeof(current);
    if (EnumDisplaySettingsA(device, ENUM_CURRENT_SETTINGS, &current)) {
        display.currentMode = CopyDisplayMode(current);
        // The reference historically calls the current settings "desktop".
        // Unlike SDL, this stateless query does not retain a pre-exclusive mode.
        display.desktopMode = display.currentMode;
    }
    DEVMODEA mode{};
    mode.dmSize = sizeof(mode);
    for (int i = 0; EnumDisplaySettingsA(device, i, &mode); ++i)
        display.modes.push_back(CopyDisplayMode(mode));
    return display;
}

BOOL CALLBACK CollectDisplay(HMONITOR monitor, HDC, LPRECT, LPARAM data)
{
    MONITORINFOEXA info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoA(monitor, reinterpret_cast<MONITORINFO*>(&info))) return TRUE;
    auto& catalog = *reinterpret_cast<Platform::DisplayCatalog*>(data);
    const bool primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    // Keep the reference's default-device query for the primary display.
    auto display = ReadDisplay(primary ? nullptr : info.szDevice);
    display.bounds = {{info.rcMonitor.left, info.rcMonitor.top},
        {info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top}};
    if (primary) catalog.primaryDisplay = catalog.displays.size();
    catalog.displays.push_back(std::move(display));
    return TRUE;
}
}

namespace Platform::Win32 {
void Initialize(HINSTANCE instance, WNDPROC procedure)
{
    gameInstance = instance;
    gameProcedure = procedure;
}
HWND GameWindow() { return gameWindow; }
bool IsWindowActive(HWND window) { return GetActiveWindow() == window; }
}

namespace Platform {

bool InitializeApplication() { EnableDpiAwareness(); return true; }
void ShutdownApplication() {} // Preserve reference window destruction ordering.
const char* LastError() { return "Win32 platform operation failed"; }
void ShowMessage(const char* title, const char* text) { MessageBoxA(gameWindow, text, title, MB_OK | MB_SYSTEMMODAL | MB_ICONEXCLAMATION); }

void EnableDpiAwareness()
{
  using SetProcessDpiAwarenessContextProc = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
  auto setProcessDpiAwarenessContext =
    reinterpret_cast<SetProcessDpiAwarenessContextProc>(
      GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext"));

  if (setProcessDpiAwarenessContext) {
    setProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  }
}

bool CreateGameWindow()
{
  WNDCLASS wc;
  wc.style = CS_OWNDC;
  wc.lpfnWndProc = gameProcedure;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = gameInstance;
  wc.hIcon = (HICON)LoadIcon(gameInstance,"ACTION");
  wc.hCursor = nullptr;
  wc.hbrBackground = (HBRUSH)GetStockObject( BLACK_BRUSH );
  wc.lpszMenuName = nullptr;
  wc.lpszClassName = "HuntRenderWindow";
  if (!RegisterClass(&wc)) return false;

  gameWindow = CreateWindow(
               "HuntRenderWindow","Carnivores 2 Renderer",
               WS_VISIBLE |  WS_POPUP,
               0, 0, 0, 0, nullptr,  nullptr, gameInstance, nullptr );

  return true;
}

DisplayCatalog QueryDisplayCatalog()
{
    DisplayCatalog catalog;
    EnumDisplayMonitors(nullptr, nullptr, CollectDisplay, reinterpret_cast<LPARAM>(&catalog));
    return catalog;
}

DisplayInfo QueryDisplayInfo()
{
    // Preserve the default-device enumeration and system-metrics fallback even
    // if monitor enumeration is unavailable. No monitor-placement policy.
    return ProjectDisplayInfo(ReadDisplay(nullptr),
        {GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)});
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
std::uint32_t Milliseconds() { return timeGetTime(); }

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

bool HasGameWindow() { return gameWindow != nullptr; }

void ShowAndFocusGameWindow()
{
    SetWindowPos(gameWindow, HWND_TOP, 0, 0, 0, 0, SWP_SHOWWINDOW);
    SetFocus(gameWindow);
}

void SetProcessActive(bool active)
{
    SetPriorityClass(GetCurrentProcess(), active ? HIGH_PRIORITY_CLASS : IDLE_PRIORITY_CLASS);
}

void FocusGameWindow()
{
    if (!gameWindow) return;
    SetForegroundWindow(gameWindow);
    SetFocus(gameWindow);
}

PumpResult PumpOneEvent(int& quitCode, Event* event)
{
    if (event) *event = {};
    MSG message;
    if (!PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) return PumpResult::Idle;
    if (message.message == WM_QUIT) {
        quitCode = static_cast<int>(message.wParam);
        return PumpResult::Quit;
    }
    TranslateMessage(&message);
    DispatchMessage(&message);
    return PumpResult::Dispatched;
}

void RequestQuit() { PostQuitMessage(0); }
void RestoreDesktopMode() { ChangeDisplaySettings(nullptr, 0); }
void LoadArrowCursor() { arrowCursor = LoadCursor(nullptr, IDC_ARROW); }
void HideArrowCursor()
{
    SetCursor(arrowCursor);
    while (ShowCursor(false) >= 0);
}
void ShowCursorOnExit() { ShowCursor(true); }

void ShowLoadingWindow(Size size)
{
    SetWindowPos(gameWindow, HWND_TOP, (GetSystemMetrics(SM_CXSCREEN) - size.width) / 2,
                 (GetSystemMetrics(SM_CYSCREEN) - size.height) / 2,
                 size.width, size.height, SWP_SHOWWINDOW);
}

Size ClientSize()
{
    RECT rect;
    GetClientRect(gameWindow, &rect);
    return {rect.right - rect.left, rect.bottom - rect.top};
}

void ConfigureGameWindow(WindowMode mode, Size size, Point videoCenter)
{
  if (mode == WindowMode::Exclusive) {
    SetWindowLong(gameWindow, GWL_STYLE, WS_VISIBLE | WS_POPUP);

    // Honour the configured resolution by switching the display mode,
    // like the original game's exclusive fullscreen. Fall back to a
    // desktop-sized popup if the mode is not available (e.g. a
    // resolution the monitor cannot display). ChangeDisplaySettings is
    // per-process: the desktop is restored automatically on exit.
    bool modeSet = false;
    DEVMODE dm;
    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(dm);
    dm.dmPelsWidth  = size.width;
    dm.dmPelsHeight = size.height;
    dm.dmBitsPerPel = 32;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    if (ChangeDisplaySettings(&dm, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL)
      modeSet = true;
    else {
      dm.dmBitsPerPel = 16;
      if (ChangeDisplaySettings(&dm, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL)
        modeSet = true;
    }

    int dispW = GetSystemMetrics(SM_CXSCREEN);
    int dispH = GetSystemMetrics(SM_CYSCREEN);
    if (modeSet) {
      // The display is now size.width x size.height, so the window must cover exactly that.
      dispW = size.width;
      dispH = size.height;
    }

    // SWP_FRAMECHANGED: required after SetWindowLong changes the style, otherwise
    // the frame (title bar/borders) is not recalculated and the client area
    // ends up at the wrong size. This caused HUD elements (ammo counter etc.)
    // to be hidden behind the title bar in windowed mode, especially at
    // resolutions where the window extends off-screen (e.g. 2560x1440 windowed
    // on a 2560x1440 desktop).
    SetWindowPos(gameWindow, HWND_TOP, 0, 0, dispW, dispH, SWP_FRAMECHANGED | SWP_SHOWWINDOW);

    POINT center = { videoCenter.x, videoCenter.y };
    SetCursorPos(center.x, center.y);
  } else if (mode == WindowMode::Borderless) {
    // DWM-composed borderless window. Honour the configured resolution as
    // the window size (centered on the desktop); when the configured
    // resolution equals the desktop this covers the screen exactly, which
    // is the classic "borderless fullscreen" behaviour. A smaller pick
    // yields a true borderless window at that resolution instead of
    // silently rendering at the desktop size.
    int desktopW = GetSystemMetrics(SM_CXSCREEN);
    int desktopH = GetSystemMetrics(SM_CYSCREEN);
    int winW = (size.width > 0 && size.width <= desktopW) ? size.width : desktopW;
    int winH = (size.height > 0 && size.height <= desktopH) ? size.height : desktopH;

    SetWindowLong(gameWindow, GWL_STYLE, WS_VISIBLE | WS_OVERLAPPED);
    SetWindowPos(gameWindow, HWND_TOP,
                (desktopW - winW) / 2, (desktopH - winH) / 2,
                winW, winH,
                SWP_FRAMECHANGED | SWP_SHOWWINDOW);

    POINT center = { videoCenter.x, videoCenter.y };
    SetCursorPos(center.x, center.y);
  } else {
    DWORD style = WS_VISIBLE | WS_OVERLAPPEDWINDOW;
    SetWindowLong(gameWindow, GWL_STYLE, style);

    RECT r = { 0, 0, size.width, size.height };
    AdjustWindowRect(&r, style, false);

    int ww = r.right - r.left;
    int wh = r.bottom - r.top;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
    // SWP_FRAMECHANGED: see comment above. Without it the new
    // WS_OVERLAPPEDWINDOW frame is not applied and the client area is wrong.
    SetWindowPos(gameWindow, HWND_TOP, sx, sy, ww, wh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
  }

}

} // namespace Platform
