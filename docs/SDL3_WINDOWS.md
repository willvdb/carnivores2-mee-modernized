# Phase 3b: SDL3 Windows/OpenGL backend

Baseline: `b63fa50069d6820762ec84326617fe5be21b107d` (Phase 3a).
Implementation validated at `f88e030`. This is a Windows backend milestone,
not a Linux game executable. Native Windows gameplay approval remains pending;
the owner chose native Windows CI plus local Wine/Gamescope validation for this
branch. Do not treat those runs as certification of native Windows playability.

## Build and ownership

The existing presets still select `CARNIVORES_PLATFORM=WIN32`. Four additional
presets, `windows-{x86,x64}-sdl-gl-{debug,release}`, select SDL3 for the GL game.
Use the matching MSVC developer environment, then, for example:

```sh
cmake --preset windows-x64-sdl-gl-release
cmake --build --preset windows-x64-sdl-gl-release
ctest --preset windows-x64-sdl-gl-release
```

The output remains `bin/Carnivores1_GL.exe`. Menu and SOFT retain their native
entry/window/input paths and receive neither SDL definitions nor SDL linkage.
SDL backend selection rejects SOFT, menu-only and non-Windows game targets.
The Windows CI matrix retains all six reference jobs and adds four SDL jobs.

[cmake/SDL3.cmake](../cmake/SDL3.cmake) uses the SDL **3.2.28** release archive,
pinned by SHA-256, through CMake FetchContent. The default is static linkage,
matching the existing static MSVC runtime. `CARNIVORES_SYSTEM_SDL3=ON` instead
uses an installed SDL3 CMake package (minimum 3.2.28); shared-package DLLs are
copied beside the game and SDL tests. SDL source is not vendored. On newer CMake, CMP0135 uses extraction-time
timestamps so a changed archive rebuilds its dependents without an unset-policy
warning. Its Release
IPO is disabled because SDL's deliberately non-LTO `SDL_mslibc.c` otherwise
conflicts with the inherited LTO PCH (C4652). Game Release LTO is unchanged.

The application boundary is now:

```
engine policies and legacy VK bindings
  -> Platform.h (standard C++ types only)
  -> PlatformSDL.cpp / LegacyKeyboardSDL.cpp
  -> SDL3 -> Windows
```

`EntrySDL.cpp` uses SDL's main entry support and calls the shared `RunGame`.
SDL initializes video/events before logging, creates the authoritative GL
window, enumerates modes, manages window modes/focus/cursor/mouse confinement,
and provides counters, delays and the wrapping millisecond clock. Backend code
has no engine globals. The existing DPI manifest and SDL's Windows default both
request per-monitor-v2 awareness. Windows SDL client coordinates are pixels;
`ClientSize` queries pixel dimensions. No new UI scale or FOV policy is added.

`PlatformGLWin32.cpp` contains the original WGL implementation solely for the
reference build. Neither the SDL target nor renderer sources compile/call that
implementation. The renderer requests `CreateGLContext`, initializes GLAD with
`GLProcAddress`, and destroys the context after its GL resources. Both renderer
presentation and `ShowVideo` (including loading) use the same platform swap.
SDL requests RGB8/alpha8, 32-bit color, depth24/stencil8, double buffering and a
3.3 core context. If core creation fails, it deliberately tries a legacy 2.1
context request, corresponding to the old WGL fallback attempt; old hardware
still must satisfy the unchanged shader/renderer requirements. No swap interval
is imposed, matching the reference's driver-default policy. The renderer stores
only a context-present boolean, with no HWND, HDC, HGLRC or native accessor.

## Input and scheduling contracts

The engine still receives **256 bytes indexed by Windows VK numbers**. Down is
bit 7; Caps/Num/Scroll toggle state is bit 0. Generic and left/right Shift, Ctrl
and Alt, function/navigation/keypad keys, OEM keys and all five mouse-button
slots retain their saved meanings. No profile/menu/serialization code changes.
Unknown SDL keys do not alias valid bindings. Key releases clear their original
slot even if layout or Num Lock changes while the key is held.

SDL event scancodes/keycodes are confined to the private adapter. Logical
letters use the layout, and a narrow Windows native-scancode-to-VK projection
preserves OEM/dead-key/non-Latin and number-row identities. For example, the
French unshifted '&' key remains VK_1. This projection resolves identity only;
it does not poll native input. SDL suppresses Windows' synthetic AltGr left
Ctrl, so the adapter restores its polling slots and initial key-down before
right Alt, on separate event-only iterations. SDL's layout identifies whether
AltGr is present. Ordinary right Alt remains distinct. Native CI covers French
letter/OEM/number and AltGr behavior plus ordinary US right Alt.

`KeyEvent` carries generic VK, sided VK, repeat, system-key classification and
Shift state. The old MainWndProc gameplay body is now `HandleKeyEvent` in the
engine, shared by both backends. Cheats, bindings, pause/Escape, F10/F11/F12 and
Alt+Enter remain there. Existing selective repeat behavior is retained rather
than imposing a new repeat policy on all keys. SDL's optional Windows raw
keyboard path is disabled. Alt+F4 remains swallowed, like the old system-key
handler; normal window close exits normally.

Each loop consumes at most one portable event, and only the idle path runs a
frame. Inactive idle still sleeps 100 ms. The backend peeps one queued SDL event
and pumps native events only when that queue is empty. It does not use
`SDL_PollEvent`'s batch sentinel as evidence of an empty queue: events queued
behind that marker must be consumed before an idle frame. A regression test
covers this case. SDL can gather several native messages internally; this is
not a claim of one native Win32 dispatch per SDL pump. Keyboard down state
advances as individual events are consumed, avoiding a snapshot that already
reflects later queued events. Focus loss clears held keys; polling refreshes
SDL's lock modifiers and global mouse buttons.

The existing engine focus response keeps its order: update active state and
priority; on loss release capture, shut down graphics, set NeedRVM; on gain
restore audio, set NeedRVM, capture if playing and unpaused. The following
active idle iteration shows/raises the window and reactivates graphics. Initial
active priority is explicitly applied before the loop because SDL initial
focus arrives asynchronously. The platform never decides pause/game state.

Mouse input remains absolute client-position reading, engine sensitivity/delta
math, and explicit recentering. Capture is SDL window mouse confinement plus
cursor hiding, not button capture or relative mode. Both SDL automatic capture
and hidden-warp relative-mode emulation are disabled. The historical fullscreen
and borderless **global-coordinate** pre-dimension-update warp is retained.

Startup remains platform/DPI, logs, window, GL, engine, audio, loading/resources,
initial sync, then the active loop. Close keeps the SDL window alive through
audio/renderer/engine/GDI cleanup, then destroys the window and quits SDL before
closing logs. Evacuation still saves and uses the existing distinct DoHalt/
TerminateProcess path; it now also shuts down SDL after audio and graphics.
This does not generalize the legacy lifetime/error model.

All deferred `timeGetTime` gameplay calls now use `Platform::Milliseconds()`:
`ProcessSyncro`, `Wait`, message expiry and landing selection. SDL ticks are
explicitly narrowed modulo 2^32; existing units, integer arithmetic and wrap
quirks are retained. The backend epoch changes from Windows uptime to SDL
initialization time; absolute seeds/landing choices are not deterministic
between runs. Calendar/trophy dates remain unchanged.

## Remaining Windows boundaries

The SDL native window property is accessed only by
`PlatformSDLWin32.cpp::Platform::Win32::GameWindow`. The borrowed `hwndMain`
alias is intentional. All remaining active direct consumers are:

| Consumer | Reason it remains |
| --- | --- |
| `Core/GameState.h`, `Game/Hunt.cpp::CreateMainWindow` | Storage/assignment of the borrowed compatibility alias; SDL owns its lifetime |
| `Game/Hunt.cpp::RunGame`, `Audio/Audio_DLL.cpp::InitAudioSystem` | Existing HWND/HANDLE audio ABI; optional legacy DirectSound DLL consumes the HWND, OpenAL implementation is unchanged |
| `Loaders/Resources.cpp::CreateVideoDIB` | GetDC and compatible GDI DC/DIB for the unchanged CPU text/HUD/loading pipeline |
| `Game/EngineInit.cpp::ShutDownEngine` | Release the borrowed window DC before SDL destroys the window |
| `Game/EngineInit.cpp::InitEngine` | Existing heap-error MessageBox owner |
| `Game/Interface.cpp::DoHalt/DoHalt2` | Disable native window on the retained forced-exit path before SDL shutdown |
| `Game/CharacterSpawn.cpp` sighting message | Existing native MessageBox owner; other hwndMain occurrences in MapLoader/CharacterSpawn are commented examples |
| `Renderer/SoftHUD.cpp` | DirectDraw cooperative-level HWND, SOFT reference only |

The SDL Windows compatibility file also retains three mechanisms that SDL
cannot directly express with the old contract: process-wide high/idle priority;
layout-specific legacy VK identity via MapVirtualKeyEx/GetKeyboardLayout; and
legacy driver-order resolution ordinals. SDL supplies the actual modes, but its
sorted list is ordered against EnumDisplaySettings so saved OptRes indices and
the untouched Win32 menu continue agreeing. The portable display helper still
owns filtering, deduplication, desktop append, capacity and fallback. Native CI
compares the resulting resolution lists with the Windows reference.

Other deferred dependencies are GDI fonts/text/DIBs and native dialogs; file
handles/path/module-location/command-line APIs; HeapAlloc/VirtualAlloc; calendar
dates; OpenAL DLL loading and audio worker/locks; WinSock/network workers; BMP
output; MessageBeep; forced process termination; standalone menu; DirectDraw and
x86 SOFT. `Interface.cpp`'s old `MapVKKey`/`wait_mouse_release` helpers still
contain MapVirtualKey/GetAsyncKeyState but have no active call sites. The native
WndProc and WGL paths remain solely for the Win32 reference. No filesystem,
audio, networking, disk format, shader or gameplay algorithm was ported here.

## Deliberate SDL differences and remaining checks

- SDL focus is queued, rather than queried before every synchronous native
  WndProc invocation. The engine reaction order is preserved. SDL's additional
  fullscreen-minimize-on-focus-loss policy is disabled. Held keys across an
  external focus/layout change need native physical-key validation; SDL clears
  keyboard state on focus loss rather than preserving stale downs.
- SDL needs a nonzero initial window (1x1 before the existing loading sizing)
  and show/raise does not reproduce Win32's zero-size activation call.
- SDL confinement tracks the current window rectangle. It does not preserve
  the reference's accidentally stale loading-window clip or desktop-wide clip
  after reactivation. The engine's capture eligibility and warp math are intact.
- SDL hides the cursor image rather than decrementing the Win32 ShowCursor
  counter. GetCursorInfo flags alone cannot compare visibility; the probe also
  checks its cursor handle. Focus loss over the foreign probe window has a null
  cursor image in **both** implementations. Pause shows the cursor; the existing
  renderer reactivation while paused hides it again in both backends.
- On the 1024x768 virtual desktop, a requested 1024x768 decorated window has a
  1024x746 reference client after native clamping, while SDL retains a 1024x768
  client. SDL positions also remain stable across reactivation. Both query actual
  client size into the unchanged engine dimension/FOV calculations. At 800x600,
  both report the configured client dimensions. Physical high-DPI, monitor and
  edge-of-desktop window behavior still require native Windows review.
- SDL chooses among its advertised exact-resolution fullscreen modes, falling
  back to a desktop popup if unavailable. It does not reproduce the old explicit
  32-bit-then-16-bit ChangeDisplaySettings retry. Refresh/depth selection and
  unsupported exclusive-mode fallback remain physical-display validation items.
- SDL manages timer precision and releases it during SDL_Quit. The reference
  retains its process-life timeBeginPeriod request. Limiter arithmetic, sleep
  threshold, FPS option meanings and 32-bit gameplay tick units/wrap are unchanged.

## Validation evidence (2026-09-17)

[Native Windows CI for the implementation](https://github.com/willvdb/carnivores2-mee-modernized/actions/runs/35271019279)
passes all ten jobs. No tests were removed or disabled. The native warnings are
the baseline C4005 (APIENTRY), C4533 (SKIPWEAPON), and C4805 (Controls). The new
SDL PCH warning was fixed at its build configuration source, not suppressed.

| Configuration | Build | Native CI individual tests | Local Wine individual tests |
| --- | --- | --- | --- |
| x86 SDL GL Debug and Release | Pass | 251/251 each | 247/251 each |
| x64 SDL GL Debug and Release | Pass | 254/254 each | 250/254 each |
| x86 Win32 GL Debug and Release | Pass | 230/230 each | 227/230 each |
| x64 Win32 GL Debug and Release | Pass | 233/233 each | 230/233 each |
| x86 SOFT Release / menu Release | Pass | 230/230 each | 227/230 each |

The three Wine GDI font failures reproduce the established baseline. The one
additional Wine-only SDL test failure is the French layout fixture: this Wine
installation returns the **US** layout handle (04090409) even when asked to load
040c. A separate native-API-only probe reproduces US Q/A mapping without SDL.
The complete French letter/OEM/number/AltGr fixture passes on native Windows CI.
An early full-suite Wine process timed out after its assertions passed while
other MSVC/Wine work shared the prefix; isolated rerun and all final suites
complete. No timeout is hidden by the final counts.

GCC C++17 ASan+UBSan with `-Wall -Wextra -Werror` passes all **21** portable
platform/display/timing/VK adapter cases. Native SDL tests also cover one-event
scheduling, poll-boundary behavior, focus/key release, quit, timing/hints,
Windows layout mapping, AltGr, and legacy resolution order, without a visible
game window or proprietary content. The original Win32 reference tests remain.

[Asset-free runtime result and probe transcript](validation/SDL3_WINDOWS.json)
records **24** passing runs: exact-main x86/x64 GL Release baselines; final SDL
x86/x64 GL Release gameplay/save and close; SDL Debug close runs; and x64 mode/
resolution/frame-cap comparisons. Full local logs, saved comparison profiles,
compositor captures and scripts are outside git at
`/tmp/carnivores-sdl-validation/{evidence,tools}`. Proprietary content and profiles
were copied independently there; the original installation was untouched.

The runs load user-owned Genesis Redux 1.1 AREA1 with weapon 4, initialize
OpenAL, enter the hunt, move, aim, draw/fire the weapon, pause/unpause, open and
dismiss Escape, lose/regain focus while paused and playing, toggle Alt+Enter,
and evacuate/save or close normally. The final SDL sequences exercise W/A/S/D
and the left mouse button. Captures were visually inspected, including ammo,
pause and exit overlays. All 68 serialized binding bytes are unchanged in each
saved x86/x64 profile. Normal close logs audio shutdown, renderer/context
shutdown, SDL shutdown and normal exit 0. Debug close reports no engine memory
leaks. Evacuation's existing empty ABNORMAL_HALT log is retained, with both
save files written, audio/GL/SDL shutdown and exit 0.

A 360 Hz headless compositor with external Mesa `vblank_mode=0` separates caps;
the existing F11 harness samples 120 intervals per run:

| FPS option | Exact-main median ms | Final SDL median ms |
| --- | --- | --- |
| Unlimited | 1.532 | 1.618 |
| 60 | 16.671 | 16.666 |
| 120 | 8.331 | 8.333 |
| 240 | 4.182 | 4.165 |

These are bounded plausibility checks, not performance or audio-quality
benchmarks. Windowed/borderless/exclusive 800x600 and 1024x768 windowed/Alt+Enter
all pass. Native Windows gameplay, physical multi-monitor/high-DPI and fallback
GL/exclusive-mode hardware, held-modifier focus transitions, subjective audio
and long sessions remain unverified. **Native Windows playtesting and acceptance
of the documented window/focus/cursor differences should gate merge.**

## SDL API references

- [Window native properties](https://wiki.libsdl.org/SDL3/SDL_GetWindowProperties)
- [Keyboard events](https://wiki.libsdl.org/SDL3/SDL_KeyboardEvent)
- [Peep events](https://wiki.libsdl.org/SDL3/SDL_PeepEvents) and [pump events](https://wiki.libsdl.org/SDL3/SDL_PumpEvents)
- [Hidden-warp relative emulation](https://wiki.libsdl.org/SDL3/SDL_HINT_MOUSE_EMULATE_WARP_WITH_RELATIVE)

The pinned SDL source was also inspected for Windows DPI initialization, raw
keyboard behavior, AltGr filtering, mode ordering and context/swap mechanics.
