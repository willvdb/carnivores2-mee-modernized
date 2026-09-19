#include "Platform.h"
#include "LegacyKeyboardSDL.h"
#include "PlatformSDLInternal.h"
#include "WindowCoordinates.h"
#include "../Debug/Log.h"
#include <algorithm>
#include <memory>
#include <optional>

namespace Platform::SDLDetails {
MouseDelta ReadWaylandMouseLook(bool captured, bool focused, MouseStateQuery query)
{
    MouseDelta delta;
    query(&delta.x, &delta.y); // Drain even when inactive: no stale motion on regain.
    return captured && focused ? delta : MouseDelta{};
}

FullscreenResult EnterFullscreen(SDL_Window* window, const SDL_DisplayMode& mode, bool emulated,
                                 const FullscreenAPI& api)
{
    FullscreenResult result;
    result.requested = api.setMode(window, &mode) && api.setFullscreen(window, true);
    if (result.requested) result.synchronized = api.sync(window);
    result.fullscreen = (api.flags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    result.display = api.display(window);
    const bool pixelsValid = api.pixels(window, &result.pixels.width, &result.pixels.height);
    if (const auto* current = api.currentMode(result.display)) result.currentMode = CopyDisplayMode(*current);
    result.confirmed = result.requested && result.synchronized && result.fullscreen && pixelsValid &&
        result.display == mode.displayID && result.pixels.width == mode.w && result.pixels.height == mode.h;
    if (!emulated) {
        const auto expected = CopyDisplayMode(mode);
        result.confirmed = result.confirmed && result.currentMode &&
            result.currentMode->size.width == expected.size.width && result.currentMode->size.height == expected.size.height &&
            (!HasRefresh(expected.refresh) || EqualRefresh(result.currentMode->refresh, expected.refresh));
    }
    return result;
}

FullscreenResult EnterDesktopFullscreen(SDL_Window* window, SDL_DisplayID display,
                                        const FullscreenAPI& api, DesktopModeQuery query)
{
    const auto* desktop = query(display);
    return desktop && desktop->displayID == display ? EnterFullscreen(window, *desktop, true, api) : FullscreenResult{};
}

std::optional<SDL_Rect> RefreshWindowDisplayBounds(WindowDisplay& display,
    decltype(&SDL_GetDisplayBounds) bounds, decltype(&SDL_GetPrimaryDisplay) primary)
{
    if (!display.target) return std::nullopt;
    SDL_Rect current{};
    if (bounds(display.id, &current)) return current;
    display = {primary(), std::nullopt, std::nullopt};
    return std::nullopt;
}

WindowDisplay MapWindowDisplay(const std::vector<NativeDisplay>& displays, SDL_DisplayID primary,
                               std::optional<DisplayTarget> target, std::optional<DisplayMode> mode)
{
    if (!target) return {primary, std::nullopt, mode};
    const NativeDisplay* match = nullptr;
    for (const auto& display : displays) {
        if (EqualDisplayBounds(display.bounds, target->bounds)) {
            if (match) return {primary, std::nullopt, std::nullopt, true};
            match = &display;
        }
    }
    if (match && (!target->identity || (match->identity && EqualDisplayIdentity(*target->identity, *match->identity))))
        return {match->id, target, mode};
    return {primary, std::nullopt, std::nullopt};
}

bool FindAutomaticDisplayMode(SDL_DisplayID display, Size size, SDL_DisplayMode& result, ClosestModeQuery query)
{
    return query(display, size.width, size.height, 0, false, &result) &&
        result.displayID == display && result.w == size.width && result.h == size.height;
}

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
                                           const DisplayMode& requested, SDL_DisplayID display)
{
    if (!modes || !HasRefresh(requested.refresh)) return nullptr;
    for (int i = 0; i < count; ++i) {
        if (!modes[i] || modes[i]->displayID != display) continue;
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
bool wayland = false;
SDL_DisplayID occupiedDisplay = 0;
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
    Check(SDL_GetDisplayBounds(Platform::SDLCompatibility::PrimaryDisplay(), &bounds), "SDL_GetDisplayBounds");
    return bounds;
}
void CenterWindow(Platform::Size size, std::optional<SDL_Rect> targetBounds = std::nullopt)
{
    const auto bounds = targetBounds ? *targetBounds : DesktopBounds();
    int top=0, left=0, bottom=0, right=0;
    SDL_GetWindowBordersSize(gameWindow, &top, &left, &bottom, &right);
    Check(SDL_SetWindowSize(gameWindow, size.width, size.height), "SDL_SetWindowSize");
    if (wayland) return; // Normal Wayland toplevel placement belongs to the compositor.
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
    occupiedDisplay = 0;
    quitRequested = false;
    pendingKey.reset();
    keyboard.ClearDown();
    if (!SDL_Init(SDL_INIT_VIDEO)) return false;
    wayland = SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0;
    LOG_INFO("SDL video backend: %s", SDL_GetCurrentVideoDriver());
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
    SDLCompatibility::ShutdownMonitorDiscovery();
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
    const auto primary = SDLCompatibility::PrimaryDisplay();
    int count = 0;
    const std::unique_ptr<SDL_DisplayID, decltype(&SDL_free)> displays(SDL_GetDisplays(&count), SDL_free);
    if (displays) {
        for (int i = 0; i < count; ++i) {
            const auto id = displays.get()[i];
            if (id == primary) catalog.primaryDisplay = catalog.displays.size();
            catalog.displays.push_back(ReadDisplay(id));
        }
    }
    SDLCompatibility::DiscoverMonitorIdentities(catalog, displays.get(), count);
    return catalog;
}

DisplayInfo QueryDisplayInfo()
{
    // Share discovery with the catalog, but query only the authoritative primary
    // display as before (also preserving the old failure fallback).
    auto info = ProjectDisplayInfo(ReadDisplay(SDLCompatibility::PrimaryDisplay()), {800, 600});
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
#ifndef _WIN32
    switch (native.type) {
    case SDL_EVENT_DISPLAY_ADDED:
    case SDL_EVENT_DISPLAY_REMOVED:
    case SDL_EVENT_DISPLAY_MOVED:
    case SDL_EVENT_DISPLAY_ORIENTATION:
    case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED:
    case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
    case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
        event.type = EventType::DisplayChanged;
        event.occupiedDisplayRemoved = native.type == SDL_EVENT_DISPLAY_REMOVED &&
                                       native.display.displayID == occupiedDisplay;
        break;
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        if (native.window.windowID == windowID) {
            event.type = EventType::WindowChanged;
        }
        break;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
        if (native.window.windowID == windowID) event.type = EventType::WindowChanged;
        break;
    default: break;
    }
#endif
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
    if (wayland) {
        if (!SDL_SetWindowRelativeMouseMode(gameWindow, capture))
            LOG_WARN("Wayland relative mouse capture %s failed: %s; mouse-look unavailable (try SDL_VIDEODRIVER=x11)",
                     capture ? "enable" : "disable", SDL_GetError());
        SDL_GetRelativeMouseState(nullptr, nullptr);
    } else Check(SDL_SetWindowMouseGrab(gameWindow, capture), "SDL_SetWindowMouseGrab");
    Check(capture ? SDL_HideCursor() : SDL_ShowCursor(), "SDL cursor visibility");
}
void WarpPointerInClient(Point point)
{
    if (wayland) {
        // The engine calls this after reading a frame and on capture/re-entry.
        // Do not issue advisory Wayland warps or synthesize absolute motion.
        SDL_GetRelativeMouseState(nullptr, nullptr);
    } else if (gameWindow) {
        Size logical{}, pixels{};
        SDL_GetWindowSize(gameWindow, &logical.width, &logical.height);
        SDL_GetWindowSizeInPixels(gameWindow, &pixels.width, &pixels.height);
        if (logical.width <= 0 || logical.height <= 0 || pixels.width <= 0 || pixels.height <= 0) return;
        const auto position = Coordinates::Convert({static_cast<float>(point.x), static_cast<float>(point.y)}, pixels, logical);
        SDL_WarpMouseInWindow(gameWindow, position.x, position.y);
    }
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
    Size logical{}, pixels{};
    if (gameWindow) {
        SDL_GetWindowSize(gameWindow, &logical.width, &logical.height);
        SDL_GetWindowSizeInPixels(gameWindow, &pixels.width, &pixels.height);
        const auto point = Coordinates::Convert({x - wx, y - wy}, logical, pixels);
        return {Coordinates::Integer(point.x), Coordinates::Integer(point.y)};
    }
    return {static_cast<std::int32_t>(x) - wx, static_cast<std::int32_t>(y) - wy};
}
MouseDelta ReadMouseLookDelta(Point center)
{
    if (wayland) {
        const auto delta = SDLDetails::ReadWaylandMouseLook(gameWindow && SDL_GetWindowRelativeMouseMode(gameWindow),
            gameWindow && (SDL_GetWindowFlags(gameWindow) & SDL_WINDOW_INPUT_FOCUS));
        Size logical{}, pixels{};
        if (gameWindow) {
            SDL_GetWindowSize(gameWindow, &logical.width, &logical.height);
            SDL_GetWindowSizeInPixels(gameWindow, &pixels.width, &pixels.height);
        }
        return Coordinates::Convert(delta, logical, pixels);
    }
    if (gameWindow) {
        Size logical{}, pixels{};
        if (!SDL_GetWindowSize(gameWindow, &logical.width, &logical.height) ||
            !SDL_GetWindowSizeInPixels(gameWindow, &pixels.width, &pixels.height) ||
            logical.width <= 0 || logical.height <= 0 || pixels.width <= 0 || pixels.height <= 0) return {};
    }
    const auto point = PointerInClient();
    return {static_cast<float>(point.x) - center.x, static_cast<float>(point.y) - center.y};
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
    if (gameWindow) {
        Check(SDL_SetWindowFullscreen(gameWindow, false), "SDL_SetWindowFullscreen(false)");
#ifndef _WIN32
        Check(SDL_SyncWindow(gameWindow), "SDL desktop restoration sync");
#endif
    }
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
WindowState QueryWindowState()
{
    WindowState result;
    if (!gameWindow) return result;
    SDL_GetWindowSize(gameWindow, &result.logical.width, &result.logical.height);
    SDL_GetWindowSizeInPixels(gameWindow, &result.pixels.width, &result.pixels.height);
    result.minimized = (SDL_GetWindowFlags(gameWindow) & SDL_WINDOW_MINIMIZED) != 0;
    if (wayland) {
        // Global window positions do not exist in this protocol. SDL's occupied
        // output is the compositor's placement report, never a requested target.
        SDL_Rect bounds{};
        result.reachable = SDL_GetDisplayBounds(SDL_GetDisplayForWindow(gameWindow), &bounds);
    } else {
        int x=0, y=0, count=0;
        SDL_GetWindowPosition(gameWindow, &x, &y);
        const std::unique_ptr<SDL_DisplayID, decltype(&SDL_free)> ids(SDL_GetDisplays(&count), SDL_free);
        if (ids) for (int i=0; i<count; ++i) {
            SDL_Rect bounds{};
            if (SDL_GetDisplayBounds(ids.get()[i], &bounds) &&
                std::int64_t(x) < std::int64_t(bounds.x) + bounds.w &&
                std::int64_t(y) < std::int64_t(bounds.y) + bounds.h &&
                std::int64_t(x) + result.logical.width > bounds.x &&
                std::int64_t(y) + result.logical.height > bounds.y) result.reachable = true;
        }
    }
    occupiedDisplay = SDL_GetDisplayForWindow(gameWindow);
    return result;
}
void ConfigureGameWindow(WindowMode mode, Size size, Point videoCenter,
                         std::optional<DisplayMode> exclusiveMode, std::optional<DisplayTarget> target)
{
    if (!gameWindow) return;
    SDLDetails::WindowDisplay mapped{SDLCompatibility::PrimaryDisplay(), std::nullopt, exclusiveMode};
    if (target) {
        // Resolve BEFORE leaving fullscreen: the caller's snapshot can describe
        // the current exclusive rectangle. Native IDs stay within this call.
        std::vector<SDLDetails::NativeDisplay> nativeDisplays;
        int count = 0;
        const std::unique_ptr<SDL_DisplayID, decltype(&SDL_free)> ids(SDL_GetDisplays(&count), SDL_free);
        DisplayCatalog mappingCatalog;
        if (ids) for (int i = 0; i < count; ++i) {
            Display display;
            SDL_Rect bounds{};
            if (SDL_GetDisplayBounds(ids.get()[i], &bounds))
                display.bounds = {{bounds.x, bounds.y}, {bounds.w, bounds.h}};
            mappingCatalog.displays.push_back(std::move(display));
        }
#ifndef _WIN32
        if (target->identity) SDLCompatibility::DiscoverMonitorIdentities(mappingCatalog, ids.get(), count);
#else
        target->identity.reset(); // Native Windows compatibility remains rectangle-based.
#endif
        if (ids) for (int i = 0; i < count; ++i) {
            const auto& display = mappingCatalog.displays[i];
            if (display.bounds) nativeDisplays.push_back({ids.get()[i], *display.bounds, display.identity});
        }
        mapped = SDLDetails::MapWindowDisplay(nativeDisplays, SDLCompatibility::PrimaryDisplay(), target, exclusiveMode);
        if (!mapped.target)
            LOG_WARN("SDL display target (%d,%d %dx%d) %s; using primary display and automatic refresh",
                     target->bounds.origin.x, target->bounds.origin.y,
                     target->bounds.size.width, target->bounds.size.height,
                     mapped.ambiguousBounds ? "matches multiple displays" : "disappeared");
    }
    Check(SDL_SetWindowFullscreen(gameWindow, false), "SDL leave fullscreen");
    std::optional<SDL_Rect> targetBounds;
    // A lost or ambiguous target also needs to finish leaving the old display
    // before primary fallback. The normal no-override path is unchanged.
#ifdef _WIN32
    if (target) Check(SDL_SyncWindow(gameWindow), "SDL leave targeted fullscreen sync");
#else
    Check(SDL_SyncWindow(gameWindow), "SDL leave fullscreen sync");
#endif
    if (mapped.target) {
        targetBounds = SDLDetails::RefreshWindowDisplayBounds(mapped, SDL_GetDisplayBounds, SDLCompatibility::PrimaryDisplay);
        if (!targetBounds)
            LOG_WARN("SDL display target disappeared during restoration; using primary display and automatic refresh");
    }
    if (target && !mapped.target) targetBounds = DesktopBounds();
    Check(SDL_SetWindowBordered(gameWindow, mode == WindowMode::Windowed), "SDL_SetWindowBordered");
    Check(SDL_SetWindowResizable(gameWindow, mode == WindowMode::Windowed), "SDL_SetWindowResizable");
    if (mode == WindowMode::Exclusive) {
        // Do not choose a merely close resolution: the reference falls back to
        // a desktop-sized popup when the exact mode cannot be applied.
        SDL_DisplayMode closest{};
        const auto applyMode = [&](const SDL_DisplayMode& nativeMode) {
#ifdef _WIN32
            return SDL_SetWindowFullscreenMode(gameWindow, &nativeMode) && SDL_SetWindowFullscreen(gameWindow, true);
#else
            const auto actual = SDLDetails::EnterFullscreen(gameWindow, nativeMode, wayland);
            if (!actual.confirmed)
                LOG_WARN("SDL fullscreen unconfirmed: requested display=%u %dx%d; accepted=%d sync=%d actual fullscreen=%d display=%u pixels=%dx%d (%s)",
                         nativeMode.displayID, nativeMode.w, nativeMode.h, actual.requested, actual.synchronized,
                         actual.fullscreen, actual.display, actual.pixels.width, actual.pixels.height, SDL_GetError());
            return actual.confirmed;
#endif
        };
        const auto applyAutomatic = [&](SDL_DisplayID id) {
            return SDLDetails::FindAutomaticDisplayMode(id, size, closest) && applyMode(closest);
        };
        if (wayland)
            LOG_INFO("Wayland fullscreen uses compositor scaling; physical output mode and refresh remain compositor-controlled");
        if (targetBounds) {
            // SDL 3.2.28: position while windowed, then set the native mode (its
            // displayID is authoritative), then enter fullscreen. Synchronize
            // the move before application; never infer the target from position.
            CenterWindow(size, targetBounds);
            Check(SDL_SyncWindow(gameWindow), "SDL targeted window move sync");
        }
        bool explicitMode = false;
        bool applied = false;
        if (mapped.exclusiveMode && HasRefresh(mapped.exclusiveMode->refresh)) {
            const auto refresh = mapped.exclusiveMode->refresh;
            int count = 0;
            const std::unique_ptr<SDL_DisplayMode*, decltype(&SDL_free)> modes(
                SDL_GetFullscreenDisplayModes(mapped.id, &count), SDL_free);
            const auto* selected = mapped.exclusiveMode->size.width == size.width && mapped.exclusiveMode->size.height == size.height
                ? SDLDetails::FindNativeDisplayMode(modes.get(), count, *mapped.exclusiveMode, mapped.id) : nullptr;
            if (selected) {
                explicitMode = true;
                // SDL 3.2.28 copies the mode in SetWindowFullscreenMode. Keep
                // the single enumeration allocation alive through application;
                // no SDL mode pointer is retained by the engine.
                applied = applyMode(*selected);
                if (applied)
                    LOG_INFO("SDL %s %dx%d reported refresh %u/%u confirmed", wayland ? "emulated fullscreen" : "exclusive",
                             size.width, size.height, refresh.numerator, refresh.denominator);
                else
                    LOG_WARN("SDL exclusive refresh %u/%u application failed: %s",
                             refresh.numerator, refresh.denominator, SDL_GetError());
            } else {
                LOG_WARN("SDL exclusive %dx%d refresh %u/%u unavailable; using automatic refresh",
                         size.width, size.height, refresh.numerator, refresh.denominator);
            }
        }
        if (!explicitMode) {
            applied = applyAutomatic(mapped.id);
        }
        if (!applied && mapped.target) {
            if (!SDLDetails::RefreshWindowDisplayBounds(mapped, SDL_GetDisplayBounds, SDLCompatibility::PrimaryDisplay)) {
                LOG_WARN("SDL display target disappeared during exclusive application; using primary display and automatic refresh");
                Check(SDL_SetWindowFullscreen(gameWindow, false), "SDL leave disappeared target");
                Check(SDL_SyncWindow(gameWindow), "SDL disappeared target sync");
                targetBounds = DesktopBounds();
                CenterWindow(size, targetBounds);
                Check(SDL_SyncWindow(gameWindow), "SDL primary fallback move sync");
                // One automatic primary attempt, never reuse the missing
                // target's rate or recursively select another secondary.
                applied = applyAutomatic(mapped.id);
            }
        }
        if (!applied) {
#ifdef _WIN32
            LOG_WARN("SDL exclusive %dx%d unavailable; using desktop popup: %s", size.width, size.height, SDL_GetError());
            SDL_SetWindowFullscreen(gameWindow, false);
            const auto bounds = targetBounds ? *targetBounds : DesktopBounds();
            CenterWindow({bounds.w, bounds.h}, targetBounds);
#else
            LOG_WARN("SDL exclusive %dx%d unavailable or unconfirmed; restoring before fallback: %s", size.width, size.height, SDL_GetError());
            Check(SDL_SetWindowFullscreen(gameWindow, false), "SDL fallback leave fullscreen");
            Check(SDL_SyncWindow(gameWindow), "SDL fallback restoration sync");
            if (wayland) {
                // A popup cannot target a Wayland output. Request the target's
                // desktop-size fullscreen once; no physical mode/refresh change.
                applied = SDLDetails::EnterDesktopFullscreen(gameWindow, mapped.id).confirmed;
                if (applied) LOG_INFO("Wayland fallback: target desktop-size fullscreen confirmed");
                else {
                    LOG_WARN("Wayland fullscreen fallback unconfirmed; using compositor-placed window");
                    Check(SDL_SetWindowFullscreen(gameWindow, false), "SDL fallback windowed");
                    Check(SDL_SyncWindow(gameWindow), "SDL fallback windowed sync");
                    Check(SDL_SetWindowBordered(gameWindow, true), "SDL fallback window border");
                    CenterWindow(size);
                }
            } else {
                LOG_INFO("SDL fallback: desktop popup (placement subject to window manager)");
                const auto bounds = targetBounds ? *targetBounds : DesktopBounds();
                CenterWindow({bounds.w, bounds.h}, targetBounds);
            }
#endif
        }
    } else if (mode == WindowMode::Borderless) {
        const auto bounds = targetBounds ? *targetBounds : DesktopBounds();
        CenterWindow({size.width > 0 && size.width <= bounds.w ? size.width : bounds.w,
                      size.height > 0 && size.height <= bounds.h ? size.height : bounds.h}, targetBounds);
    } else CenterWindow(size, targetBounds);
    occupiedDisplay = SDL_GetDisplayForWindow(gameWindow);
    const bool synchronized = SDL_SyncWindow(gameWindow);
    Check(synchronized, "SDL_SyncWindow");
    Check(SDL_ShowWindow(gameWindow), "SDL_ShowWindow");
#ifndef _WIN32
    const auto actualDisplay = SDL_GetDisplayForWindow(gameWindow);
    const auto requestedDisplay = mapped.id;
    const auto pixels = ClientSize();
    LOG_INFO("SDL presentation: requested mode=%d display=%u; sync=%d actual fullscreen=%d display=%u pixels=%dx%d",
             static_cast<int>(mode), requestedDisplay, synchronized,
             (SDL_GetWindowFlags(gameWindow) & SDL_WINDOW_FULLSCREEN) != 0, actualDisplay, pixels.width, pixels.height);
    if (const auto* reported = SDL_GetCurrentDisplayMode(actualDisplay)) {
        const auto current = SDLDetails::CopyDisplayMode(*reported);
        LOG_INFO("SDL reported display mode: %dx%d refresh=%u/%u%s", current.size.width, current.size.height,
                 current.refresh.numerator, current.refresh.denominator,
                 wayland ? " (emulated mode; physical scanout is compositor-controlled)" : "");
    }
    if (actualDisplay != requestedDisplay)
        LOG_WARN("SDL requested display %u was not reached; actual display=%u%s", requestedDisplay, actualDisplay,
                 wayland ? " (normal Wayland window placement is compositor-controlled)" : " (window manager rejected or redirected placement)");
    else if (wayland && mode != WindowMode::Exclusive)
        LOG_INFO("Wayland window placement is compositor-controlled; session display selection cannot place normal/borderless windows");
#endif
#ifdef _WIN32
    if (mode != WindowMode::Windowed && targetBounds) WarpPointerInClient(videoCenter);
    else if (mode != WindowMode::Windowed)
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
