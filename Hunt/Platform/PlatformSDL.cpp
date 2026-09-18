#include "Platform.h"
#include "LegacyKeyboardSDL.h"
#include "PlatformSDLInternal.h"
#include "../Debug/Log.h"
#include <algorithm>
#include <memory>
#include <optional>

namespace Platform::SDLDetails {
DisplayMode CopyDisplayMode(const SDL_DisplayMode& mode)
{
    // SDL 3.2 finalizes the rational fields even for float-only video drivers.
    // Copy those fields, not its rounded refresh_rate convenience value.
    const auto refresh = mode.refresh_rate_numerator > 0 && mode.refresh_rate_denominator > 0
        ? MakeRefreshRate(static_cast<std::uint32_t>(mode.refresh_rate_numerator),
                          static_cast<std::uint32_t>(mode.refresh_rate_denominator))
        : RefreshRate{};
    return {{mode.w, mode.h}, static_cast<std::uint32_t>(SDL_BITSPERPIXEL(mode.format)), refresh};
}

const SDL_DisplayMode* FindNativeDisplayMode(SDL_DisplayMode* const* modes, int count,
                                           const DisplayMode& requested)
{
    if (!modes || !HasRefresh(requested.refresh)) return nullptr;
    for (int i = 0; i < count; ++i) {
        if (!modes[i]) continue;
        const auto candidate = CopyDisplayMode(*modes[i]);
        if (candidate.size.width == requested.size.width && candidate.size.height == requested.size.height &&
            candidate.bitsPerPixel == requested.bitsPerPixel && EqualRefresh(candidate.refresh, requested.refresh))
            return modes[i];
    }
    return nullptr;
}
}

namespace {
SDL_Window* gameWindow = nullptr;
SDL_GLContext context = nullptr;
SDL_Cursor* arrowCursor = nullptr;
Platform::SDLInput::Keyboard keyboard;
bool quitRequested = false;
bool altGrLayout = false;
std::optional<Platform::Event> pendingKey;

Platform::Display ReadDisplay(SDL_DisplayID id)
{
    Platform::Display display;
    SDL_Rect bounds{};
    if (SDL_GetDisplayBounds(id, &bounds))
        display.bounds = {{bounds.x, bounds.y}, {bounds.w, bounds.h}};
    if (const auto* desktop = SDL_GetDesktopDisplayMode(id))
        display.desktopMode = Platform::SDLDetails::CopyDisplayMode(*desktop);
    if (const auto* current = SDL_GetCurrentDisplayMode(id))
        display.currentMode = Platform::SDLDetails::CopyDisplayMode(*current);
    int count = 0;
    // SDL owns desktop/current pointers. Fullscreen modes are one allocation,
    // including the pointer array: copy everything before releasing it once.
    const std::unique_ptr<SDL_DisplayMode*, decltype(&SDL_free)> modes(
        SDL_GetFullscreenDisplayModes(id, &count), SDL_free);
    if (modes)
        for (int i = 0; i < count; ++i)
            display.modes.push_back(Platform::SDLDetails::CopyDisplayMode(*modes.get()[i]));
    return display;
}

bool HasAltGrLayout()
{
    // Ask SDL's current layout, not the host's asynchronous keyboard state.
    for (int i = SDL_SCANCODE_A; i <= SDL_SCANCODE_SLASH; ++i) {
        const auto scan = static_cast<SDL_Scancode>(i);
        const auto base = SDL_GetKeyFromScancode(scan, SDL_KMOD_NONE, false);
        const auto alternate = SDL_GetKeyFromScancode(scan, SDL_KMOD_MODE, false);
        if (alternate && !(alternate & SDLK_SCANCODE_MASK) && alternate != base) return true;
    }
    return false;
}

void Check(bool ok, const char* operation)
{
    if (!ok) LOG_WARN("%s: %s", operation, SDL_GetError());
}
void RequestCoreContext()
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
}
SDL_Rect DesktopBounds()
{
    SDL_Rect bounds{0,0,800,600};
    Check(SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &bounds), "SDL_GetDisplayBounds");
    return bounds;
}
void CenterWindow(Platform::Size size)
{
    const auto bounds = DesktopBounds();
    int top=0, left=0, bottom=0, right=0;
    SDL_GetWindowBordersSize(gameWindow, &top, &left, &bottom, &right);
    Check(SDL_SetWindowSize(gameWindow, size.width, size.height), "SDL_SetWindowSize");
    Check(SDL_SetWindowPosition(gameWindow,
        bounds.x + (bounds.w - size.width - left - right) / 2 + left,
        bounds.y + (bounds.h - size.height - top - bottom) / 2 + top), "SDL_SetWindowPosition");
}
}

// Internal native bridge; not part of the portable or renderer interface.
#ifdef _WIN32
namespace Platform::Win32 {
SDL_Window* SDLGameWindow() { return gameWindow; }
}
#endif

namespace Platform {
void EnableDpiAwareness()
{
    // SDL video initialization defaults to per-monitor-v2 on Windows. The game
    // manifest agrees. SDL window coordinates are physical pixels on Windows;
    // no independent content-scale/FOV change belongs to this backend.
}
bool InitializeApplication()
{
    EnableDpiAwareness();
    SDL_SetHint(SDL_HINT_MOUSE_EMULATE_WARP_WITH_RELATIVE, "0");
    SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");
    SDL_SetHint(SDL_HINT_WINDOWS_RAW_KEYBOARD, "0"); // Match the message-based reference.
    SDL_SetHint(SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4, "0"); // Old SYSKEY handler swallows it.
    // Preserve exclusive focus loss without SDL's additional minimize policy.
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    quitRequested = false;
    pendingKey.reset();
    keyboard.ClearDown();
    if (!SDL_Init(SDL_INIT_VIDEO)) return false;
    altGrLayout = HasAltGrLayout();
    return true;
}
const char* LastError() { return SDL_GetError(); }
void ShowMessage(const char* title, const char* text) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, text, gameWindow); }
void ShutdownApplication()
{
    SetMouseCapture(false);
    if (arrowCursor) { SDL_DestroyCursor(arrowCursor); arrowCursor = nullptr; }
    if (gameWindow) { SDL_DestroyWindow(gameWindow); gameWindow = nullptr; }
    SDL_Quit();
    LOG_INFO("SDL application shutdown completed");
}

bool CreateGameWindow()
{
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 32);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    RequestCoreContext();
    // SDL requires nonzero initial dimensions. Loading/game sizing follows at
    // the existing call sites, before the first active frame.
    gameWindow = SDL_CreateWindow("Carnivores 2 Renderer", 1, 1,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS);
    if (!gameWindow) return false;
    Check(SDL_ShowWindow(gameWindow), "SDL_ShowWindow");
    return true;
}
bool HasGameWindow() { return gameWindow != nullptr; }
void FocusGameWindow() { if (gameWindow) Check(SDL_RaiseWindow(gameWindow), "SDL_RaiseWindow"); }
void ShowAndFocusGameWindow()
{
    if (!gameWindow) return;
    Check(SDL_ShowWindow(gameWindow), "SDL_ShowWindow");
    FocusGameWindow();
}
void SetProcessActive(bool active) { SDLCompatibility::SetProcessActive(active); }

DisplayCatalog QueryDisplayCatalog()
{
    DisplayCatalog catalog;
    const auto primary = SDL_GetPrimaryDisplay();
    int count = 0;
    const std::unique_ptr<SDL_DisplayID, decltype(&SDL_free)> displays(SDL_GetDisplays(&count), SDL_free);
    if (displays) {
        for (int i = 0; i < count; ++i) {
            const auto id = displays.get()[i];
            if (id == primary) catalog.primaryDisplay = catalog.displays.size();
            catalog.displays.push_back(ReadDisplay(id));
        }
    }
    return catalog;
}

DisplayInfo QueryDisplayInfo()
{
    // Share discovery with the catalog, but query only the authoritative primary
    // display as before (also preserving the old failure fallback).
    auto info = ProjectDisplayInfo(ReadDisplay(SDL_GetPrimaryDisplay()), {800, 600});
    // Only the legacy projection gets the Windows driver-order ordinal shim.
    SDLCompatibility::OrderDisplayModes(info);
    return info;
}
Tick CounterFrequency() { return static_cast<Tick>(SDL_GetPerformanceFrequency()); }
Tick Counter() { return static_cast<Tick>(SDL_GetPerformanceCounter()); }
void BeginFrameTiming() { SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "1"); }
void SleepMilliseconds(std::uint32_t milliseconds) { SDL_Delay(milliseconds); }
std::uint32_t Milliseconds() { return WrapMilliseconds(SDL_GetTicks()); }

bool PollKeyboardState(KeyboardState& state)
{
#ifdef _WIN32
    const auto mouse = SDL_GetGlobalMouseState(nullptr, nullptr);
#else
    const auto mouse = SDL_GetMouseState(nullptr, nullptr);
#endif
    keyboard.Copy(state, SDL_GetModState(), mouse, altGrLayout);
    return true;
}
PumpResult PumpOneEvent(int& quitCode, Event* output)
{
    if (output) *output = {};
    if (quitRequested) { quitCode = 0; return PumpResult::Quit; }
    if (pendingKey) {
        if (output) *output = *pendingKey;
        pendingKey.reset();
        return PumpResult::Dispatched;
    }
    SDL_Event native;
    // PollEvent can return false at a batch sentinel even with newer events
    // queued behind it. Idle must mean the real queue is empty, since only the
    // idle path renders a frame. Drain one event, pumping only when empty.
    int count = SDL_PeepEvents(&native, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST);
    if (count == 0) {
        SDL_PumpEvents();
        count = SDL_PeepEvents(&native, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST);
    }
    if (count < 0) {
        LOG_ERROR("SDL event queue failed: %s", SDL_GetError());
        quitCode = 1;
        return PumpResult::Quit;
    }
    if (count == 0) return PumpResult::Idle;
    const auto windowID = gameWindow ? SDL_GetWindowID(gameWindow) : 0;
    Event event;
    if (native.type == SDL_EVENT_KEYMAP_CHANGED) altGrLayout = HasAltGrLayout();
    if (native.type == SDL_EVENT_QUIT ||
        (native.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && native.window.windowID == windowID)) {
        quitCode = 0;
        return PumpResult::Quit; // Keep window/DC alive until engine cleanup.
    }
    if ((native.type == SDL_EVENT_WINDOW_FOCUS_GAINED || native.type == SDL_EVENT_WINDOW_FOCUS_LOST) &&
        native.window.windowID == windowID) {
        event.type = EventType::FocusChanged;
        event.focused = native.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        if (!event.focused) keyboard.ClearDown();
    }
    if ((native.type == SDL_EVENT_KEY_DOWN || native.type == SDL_EVENT_KEY_UP) && native.key.windowID == windowID) {
        const auto key = SDLCompatibility::LayoutKey(native.key,
            SDLInput::LegacyKey(native.key.scancode, native.key.key, native.key.mod));
        keyboard.Key(native.key, key);
        if (native.type == SDL_EVENT_KEY_DOWN && key) {
            event.type = EventType::KeyDown;
            event.key = SDLInput::TranslateKey(native.key, key, altGrLayout);
            if (altGrLayout && key == 0xa5 && !native.key.repeat && !(native.key.mod & SDL_KMOD_CTRL)) {
                // Preserve the synthetic LCtrl key-down used by legacy toggle
                // bindings, followed by RAlt on the next event-only iteration.
                pendingKey = event;
                event.key = {0x11, 0xa2, false, false, event.key.shift};
            }
        }
    }
    if (output) *output = event;
    return PumpResult::Dispatched;
}
void RequestQuit() { quitRequested = true; }

void SetMouseCapture(bool capture)
{
    if (!gameWindow) return;
    Check(SDL_SetWindowMouseGrab(gameWindow, capture), "SDL_SetWindowMouseGrab");
    Check(capture ? SDL_HideCursor() : SDL_ShowCursor(), "SDL cursor visibility");
}
void WarpPointerInClient(Point point)
{
    if (gameWindow) SDL_WarpMouseInWindow(gameWindow, static_cast<float>(point.x), static_cast<float>(point.y));
}
Point PointerInClient()
{
    float x=0, y=0;
    int wx=0, wy=0;
#ifdef _WIN32
    SDL_GetGlobalMouseState(&x, &y);
    if (gameWindow) SDL_GetWindowPosition(gameWindow, &wx, &wy);
#else
    SDL_GetMouseState(&x, &y); // Window-relative; Wayland has no global pointer query.
#endif
    return {static_cast<std::int32_t>(x) - wx, static_cast<std::int32_t>(y) - wy};
}
void LoadArrowCursor() { arrowCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT); }
void HideArrowCursor()
{
    if (arrowCursor) Check(SDL_SetCursor(arrowCursor), "SDL_SetCursor");
    Check(SDL_HideCursor(), "SDL_HideCursor");
}
void ShowCursorOnExit() { Check(SDL_ShowCursor(), "SDL_ShowCursor"); }
void RestoreDesktopMode()
{
    if (gameWindow) Check(SDL_SetWindowFullscreen(gameWindow, false), "SDL_SetWindowFullscreen(false)");
}
void ShowLoadingWindow(Size size)
{
    if (!gameWindow) return;
    CenterWindow(size);
    ShowAndFocusGameWindow();
}
Size ClientSize()
{
    int width=0, height=0;
    if (gameWindow) SDL_GetWindowSizeInPixels(gameWindow, &width, &height);
    return {width, height};
}
void ConfigureGameWindow(WindowMode mode, Size size, Point videoCenter,
                         std::optional<DisplayMode> exclusiveMode)
{
    if (!gameWindow) return;
    Check(SDL_SetWindowFullscreen(gameWindow, false), "SDL leave fullscreen");
    Check(SDL_SetWindowBordered(gameWindow, mode == WindowMode::Windowed), "SDL_SetWindowBordered");
    Check(SDL_SetWindowResizable(gameWindow, mode == WindowMode::Windowed), "SDL_SetWindowResizable");
    if (mode == WindowMode::Exclusive) {
        // Do not choose a merely close resolution: the reference falls back to
        // a desktop-sized popup when the exact mode cannot be applied.
        SDL_DisplayMode closest{};
        const auto display = SDL_GetPrimaryDisplay();
        bool explicitMode = false;
        bool applied = false;
        if (exclusiveMode && HasRefresh(exclusiveMode->refresh)) {
            const auto refresh = exclusiveMode->refresh;
            int count = 0;
            const std::unique_ptr<SDL_DisplayMode*, decltype(&SDL_free)> modes(
                SDL_GetFullscreenDisplayModes(display, &count), SDL_free);
            const auto* selected = exclusiveMode->size.width == size.width && exclusiveMode->size.height == size.height
                ? SDLDetails::FindNativeDisplayMode(modes.get(), count, *exclusiveMode) : nullptr;
            if (selected) {
                explicitMode = true;
                // SDL 3.2.28 copies the mode in SetWindowFullscreenMode. Keep
                // the single enumeration allocation alive through application;
                // no SDL mode pointer is retained by the engine.
                applied = SDL_SetWindowFullscreenMode(gameWindow, selected) &&
                    SDL_SetWindowFullscreen(gameWindow, true);
                if (applied)
                    LOG_INFO("SDL exclusive %dx%d refresh %u/%u applied", size.width, size.height,
                             refresh.numerator, refresh.denominator);
                else
                    LOG_WARN("SDL exclusive refresh %u/%u application failed: %s",
                             refresh.numerator, refresh.denominator, SDL_GetError());
            } else {
                LOG_WARN("SDL exclusive %dx%d refresh %u/%u unavailable; using automatic refresh",
                         size.width, size.height, refresh.numerator, refresh.denominator);
            }
        }
        if (!explicitMode) {
            applied = SDL_GetClosestFullscreenDisplayMode(display, size.width, size.height, 0, false, &closest) &&
                closest.w == size.width && closest.h == size.height &&
                SDL_SetWindowFullscreenMode(gameWindow, &closest) && SDL_SetWindowFullscreen(gameWindow, true);
        }
        if (!applied) {
            LOG_WARN("SDL exclusive %dx%d unavailable; using desktop popup: %s", size.width, size.height, SDL_GetError());
            SDL_SetWindowFullscreen(gameWindow, false);
            const auto bounds = DesktopBounds();
            CenterWindow({bounds.w, bounds.h});
        }
    } else if (mode == WindowMode::Borderless) {
        const auto bounds = DesktopBounds();
        CenterWindow({size.width > 0 && size.width <= bounds.w ? size.width : bounds.w,
                      size.height > 0 && size.height <= bounds.h ? size.height : bounds.h});
    } else CenterWindow(size);
    Check(SDL_SyncWindow(gameWindow), "SDL_SyncWindow");
    Check(SDL_ShowWindow(gameWindow), "SDL_ShowWindow");
#ifdef _WIN32
    if (mode != WindowMode::Windowed)
        Check(SDL_WarpMouseGlobal(static_cast<float>(videoCenter.x), static_cast<float>(videoCenter.y)), "SDL_WarpMouseGlobal");
#else
    if (mode != WindowMode::Windowed) WarpPointerInClient(videoCenter);
#endif
}

bool CreateGLContext()
{
    RequestCoreContext();
    context = SDL_GL_CreateContext(gameWindow);
    if (!context) {
        LOG_WARN("SDL OpenGL 3.3 core failed; trying legacy context: %s", SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        context = SDL_GL_CreateContext(gameWindow);
    } else LOG_INFO("OpenGL 3.3 Core Profile context created (SDL3)");
    if (!context) { LOG_ERROR("SDL_GL_CreateContext: %s", SDL_GetError()); return false; }
    if (!SDL_GL_MakeCurrent(gameWindow, context)) {
        LOG_ERROR("SDL_GL_MakeCurrent: %s", SDL_GetError());
        DestroyGLContext();
        return false;
    }
    // No swap-interval override: keep driver defaults like the WGL reference.
    return true;
}
void DestroyGLContext()
{
    if (!context) return;
    if (SDL_GL_GetCurrentContext() == context) SDL_GL_MakeCurrent(gameWindow, nullptr);
    Check(SDL_GL_DestroyContext(context), "SDL_GL_DestroyContext");
    context = nullptr;
    LOG_INFO("SDL OpenGL context destroyed");
}
void* GLProcAddress(const char* name) { return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name)); }
void SwapGLBuffers() { if (gameWindow && context) Check(SDL_GL_SwapWindow(gameWindow), "SDL_GL_SwapWindow"); }
} // namespace Platform
