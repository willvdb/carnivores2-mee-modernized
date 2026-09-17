#pragma once

#include <cstdint>
#include <vector>

// Single game window, main application thread. No engine globals or native
// handles belong to this interface. Backends and native compatibility are separate.
namespace Platform {

struct Size { std::int32_t width, height; };
struct Point { std::int32_t x, y; };
struct DisplayMode { Size size; std::uint32_t bitsPerPixel; };
struct DisplayInfo {
    Size desktop;
    std::vector<DisplayMode> modes; // Backend order, unfiltered, duplicates allowed.
};
DisplayInfo QueryDisplayInfo();

// Counter units are backend-defined; divide differences by CounterFrequency().
using Tick = std::int64_t;
Tick CounterFrequency();
Tick Counter();
void BeginFrameTiming(); // Request 1 ms delay precision for the existing limiter.
void SleepMilliseconds(std::uint32_t milliseconds);
// Legacy gameplay clock: milliseconds modulo 2^32, backend epoch unspecified.
constexpr std::uint32_t WrapMilliseconds(std::uint64_t ticks)
{
    return static_cast<std::uint32_t>(ticks);
}
std::uint32_t Milliseconds();

// Compatibility boundary: legacy virtual-key indices, bit 7 down / bit 0
// toggled, including sided modifiers and mouse buttons. Never SDL scancodes.
using KeyboardState = std::uint8_t[256];
bool PollKeyboardState(KeyboardState& state);

// Legacy VK values, not backend keycodes. key is the generic modifier code;
// sidedKey distinguishes left/right. System keys retain WM_SYSKEYDOWN policy.
struct KeyEvent {
    std::uint8_t key = 0;
    std::uint8_t sidedKey = 0;
    bool repeat = false;
    bool system = false;
    bool shift = false;
};
enum class EventType { None, KeyDown, FocusChanged };
struct Event {
    EventType type = EventType::None;
    KeyEvent key;
    bool focused = false;
};

// Capture is confinement + visibility, not relative input or OS button capture.
// Engine decides whether/when to recenter and supplies its video center.
void SetMouseCapture(bool capture);
void WarpPointerInClient(Point position);
Point PointerInClient();

void EnableDpiAwareness();
bool InitializeApplication();
void ShutdownApplication(); // After renderer, audio and native-window borrowers.
const char* LastError();
// The Win32 reference entry supplies its instance/WndProc separately. SDL owns
// its application entry/window. False reports backend window setup failure.
bool CreateGameWindow();
bool HasGameWindow();
void ShowAndFocusGameWindow();
void FocusGameWindow(); // Foreground/focus without changing size.
void SetProcessActive(bool active);

// Single context for the game window. Renderer owns the call ordering and GLAD.
bool CreateGLContext();
void DestroyGLContext();
void* GLProcAddress(const char* name);
void SwapGLBuffers();

enum class PumpResult { Idle, Dispatched, Quit };
// Consume at most one event; quitCode is written only for Quit.
PumpResult PumpOneEvent(int& quitCode, Event* event = nullptr);
void RequestQuit();

enum class WindowMode { Exclusive, Borderless, Windowed };
void ConfigureGameWindow(WindowMode mode, Size size, Point videoCenter);
Size ClientSize();
void ShowLoadingWindow(Size size);
void RestoreDesktopMode();
void LoadArrowCursor();
void HideArrowCursor();
void ShowCursorOnExit(); // Win32: one counter increment. SDL: show cursor.

} // namespace Platform
