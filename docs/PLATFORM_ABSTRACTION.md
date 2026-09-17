# Win32 shell seam

Phase 3a reference record. The subsequent selectable SDL3 Windows GL backend,
remaining native boundaries and validation are documented in
[SDL3_WINDOWS.md](SDL3_WINDOWS.md).

Baseline: `5863c4e70aafdbd5bc9b1abb1a1144f91883899d`. This Phase 3 slice
extracts mechanisms, preserving Windows x86 GL/SOFT and x64 GL behavior. It
adds neither SDL nor a Linux engine. Serialization and the standalone menu are
outside the slice. Inventory below was made before production edits.

## Active dependencies and disposition

| Dependency | Current file / symbol | Category / this slice | Subsequent replacement point |
| --- | --- | --- | --- |
| Dynamic DPI-awareness lookup in user32 | Game/Hunt.cpp, EnablePerMonitorV2DpiAwareness | Application: extract unchanged | SDL window startup, with separately validated DPI policy |
| WNDCLASS, RegisterClass, CreateWindow, HINSTANCE | Game/Hunt.cpp, CreateMainWindow / WinMain | Window: extract creation; retain native entry/callback bridge | SDL window creation and application entry |
| PeekMessage, TranslateMessage, DispatchMessage, WM_QUIT, PostQuitMessage | Game/Hunt.cpp, WinMain / MainWndProc | Events: extract one-message pump and quit request | SDL event translation; retain one-message-versus-idle scheduling initially |
| GetActiveWindow, SetPriorityClass, GetCurrentProcess | Game/Hunt.cpp, MainWndProc | Focus: isolate engine response, extract native query/priority | SDL focus events driving the same engine response |
| SetWindowPos / SetFocus | Game/Hunt.cpp, ProcessGame | Window activation: extract exact zero-size/show sequence | SDL show/raise/focus before renderer activation |
| QPC/QPF, timeBeginPeriod(1), Sleep | Game/Hunt.cpp, LimitFPS / inactive loop | Time: extract mechanism; retain limiter arithmetic and scheduling | SDL performance counter, frequency, delay |
| GetKeyboardState | Game/Controls.cpp, ProcessControls | Input: extract exact 256-byte poll | SDL adapter populating legacy virtual-key slots |
| GetClientRect, ClientToScreen, ClipCursor, ShowCursor, SetCursorPos | Game/Controls.cpp, CaptureMouse / ResetMousePos | Mouse: extract primitives; retain engine eligibility checks | SDL confinement, visibility and pointer warp |
| GetCursorPos, ScreenToClient | Game/PlayerMovement.cpp, ProcessPlayerMovement | Mouse: extract client position; retain all delta/sensitivity math | SDL client position; relative motion is a later explicit behavior change |
| GetSystemMetrics, EnumDisplaySettings, DEVMODE | Game/EngineInit.cpp, EnumerateResolutions | Display: backend values plus portable engine list policy | SDL desktop/mode enumeration |
| Window styles, ChangeDisplaySettings, AdjustWindowRect, cursor setup | Game/Interface.cpp, SetVideoMode / SetFullScreen / StartLoading | Window mechanics: extract existing three modes and sizing; keep renderer/DIB/FOV policy here | SDL window/fullscreen mode operations; no new modes in this branch |
| Message constants, key repeat/extended-key bits, GetKeyState, DefWindowProc, paint suppression | Game/Hunt.cpp, MainWndProc; Core/KeyBindings.h | Deferred native event bridge: gameplay toggles/cheats/menu actions stay in engine | Small portable input events with legacy binding translation |
| HWND/HDC/HGLRC, pixel format, WGL, opengl32.dll, GLAD proc callback | Renderer/GLRenderer.cpp/.h, GLUtils.cpp | Deferred context ABI: only native-window access may change | SDL GL context/proc API, preserving 3.3 core request and fallback decisions |
| SwapBuffers / GetDC / ReleaseDC, foreground activation | Renderer/GLRenderer.cpp, GLUI.cpp | Deferred renderer presentation/loading UI | SDL GL swap and loading presentation, together with context migration |
| timeGetTime, SYSTEMTIME / GetLocalTime | Game/EngineInit.cpp, ProcessSyncro / SubmitDinoScore; Interface.cpp, Wait; Loaders/Resources.cpp, AddMessage / landing choice | Deferred legacy millisecond clock and calendar; preserve wrap/units and trophy dates | SDL ticks adapter for existing 32-bit game clock; separate calendar implementation |
| Win32 DLL loading, threads/locks | Audio/Audio_DLL.cpp, OpenAL_Loader.cpp | Deferred audio subsystem; avoid ownership/thread changes | Separate portable OpenAL work |
| Win32 file handles/I/O, paths and process termination | Loaders/*, Game/Trophy.cpp, EngineInit.cpp; Interface.cpp, DoHalt/DoHalt2 | Deferred filesystem/error policy; no format or lifetime rewrite | Separate filesystem and shutdown work |
| WinSock and Win32 workers | Network/NetworkManager.cpp, Game/Network.cpp | Deferred networking and wire compatibility | Separate network platform boundary |
| BMP headers/writes | Loaders/Resources.cpp, SaveScreenShot | Deferred screenshot output | Separate output adapter |
| GDI DIBs, fonts, text, blits, message boxes; DirectDraw | Game/Interface.cpp, EngineInit.cpp, Loaders/Resources.cpp, Renderer/UIText.cpp, GLHUD/GLUI, Soft* | Deferred drawing/error UI and legacy SOFT; these still consume native handles | Separate CPU text/UI boundary; SOFT stays Windows x86 |
| GDI application/launcher, audio, input and display enumeration | Menu/* | Unchanged independent executable | Later menu project, not the SDL engine branch |

## Characterized behavior to preserve

Focus is queried **before every MainWndProc message**, comparing GetActiveWindow
with the callback's HWND (including synchronous creation messages). Only a
transition changes blActive, then process priority (high/idle). Loss releases
capture, shuts down 3D, then sets NeedRVM. Gain restores audio, sets NeedRVM,
then captures if _GameState and not paused. The next active idle iteration
raises/shows the zero-sized window, focuses it, activates 3D and clears NeedRVM.
No replacement with WM_ACTIVATE semantics is implied.

Capture clips to the client rectangle in screen coordinates, decrements the
cursor visibility counter until hidden, then calls ResetMousePos. Release
unclips and increments until visible. Neither uses Win32 SetCapture. Reset
warps VideoCX/VideoCY from client to screen only when active, in game and not
paused. Escape's exit prompt still captures; Pause releases. Movement reads
client coordinates and resets each frame. The fullscreen/borderless sizing path
also performs its existing **screen-coordinate** warp before derived video
dimensions are recomputed; do not silently correct that distinction here.

The keyboard buffer is 256 bytes indexed by legacy Windows virtual-key numbers:
bit 7 is down, bit 0 preserves toggle state, and sided modifiers/mouse slots are
not remapped. Stored profile bindings retain their existing integer meanings.
The backend must return GetKeyboardState's bytes, not async key state or SDL
scancodes. Native key-event matching remains a separate transitional dependency.

The limiter returns immediately at Unlimited. First capped call queries QPF,
records QPC, requests timeBeginPeriod(1), then computes integer 1000000/FPS.
It sleeps 1 ms only while more than 2000 microseconds remain, otherwise polls,
and records a fresh counter after waiting. Frequency/start persist across cap
changes and disabled intervals. Baseline never calls timeEndPeriod, including
the normal return and forced DoHalt exit. This slice preserves that process-life
request; adding balanced cleanup would be a separate timer-policy change.

Display order is driver enumeration order; duplicates compare dimensions only.
The sole historical minimum is **16 bits per pixel**, not 800x600 dimensions.
Modes above either desktop dimension are excluded. Desktop comes from current
DEVMODE, falling back to system metrics. It is appended regardless of depth,
subject to the same 128-entry cap: a full list does **not** evict an entry for it.
The 800x600 empty-list fallback remains, although unconditional desktop append
makes it unreachable with this backend (even a zero-sized desktop was appended).
SetupRes still selects the first 800x600 entry or zero for an invalid OptRes.

Startup stays DPI, logs, window, 3D initialization, engine, audio, loading and
resources, initial sync, active state, then message loop. The loop consumes one
message or runs one active frame; inactive idle sleeps 100 ms. Normal shutdown
stays multiplayer, audio stop/shutdown, 3D, engine/GDI release, one ShowCursor(true),
logs. There is no explicit DestroyWindow in baseline cleanup: default close
processing destroys the window, WM_DESTROY posts quit. Evacuation uses DoHalt
and TerminateProcess after saving, with its distinct existing cleanup sequence.

## Implemented interface and integration

The public [Platform.h](../Hunt/Platform/Platform.h) includes only standard C++
headers. `PlatformWin32.cpp` includes no engine headers and reads/writes no game
globals. The engine owns its policies and calls these single-window functions:

| Portable API | Contract / migrated callers |
| --- | --- |
| `Size`, `Point` | Signed int32 dimensions/coordinates; no native structures |
| `DisplayMode`, `DisplayInfo`, `QueryDisplayInfo()` | Size + uint32 depth, desktop and raw ordered vector; EngineInit.cpp enumeration |
| `Tick`, `CounterFrequency()`, `Counter()` | Signed int64 native counter units/frequency; Hunt.cpp limiter |
| `BeginFrameTiming()`, `SleepMilliseconds(uint32_t)` | Original timer request and sleep; limiter/inactive loop |
| `KeyboardState`, `PollKeyboardState(KeyboardState&)` | Exact uint8[256] poll; returns native query success; Controls.cpp continues ignoring failure as before |
| `SetMouseCapture(bool)`, `WarpPointerInClient(Point)`, `PointerInClient()` | Clip/visibility, warp, client position; Controls.cpp and PlayerMovement.cpp |
| `EnableDpiAwareness()`, `CreateGameWindow()`, `HasGameWindow()` | Existing DPI/class/window setup; false creation result still means registration failure; Hunt.cpp and Interface.cpp |
| `ShowAndFocusGameWindow()`, `SetProcessActive(bool)` | Existing zero-size show/focus and high/idle priority; Hunt.cpp |
| `PumpResult`, `PumpOneEvent(int&)`, `RequestQuit()` | Idle/Dispatched/Quit and integer quit code, one message per iteration; Hunt.cpp |
| `WindowMode`, `ConfigureGameWindow(WindowMode, Size, Point)` | Existing Exclusive/Borderless/Windowed mechanics, including original video-center screen warp; Interface.cpp |
| `ClientSize()`, `ShowLoadingWindow(Size)`, `RestoreDesktopMode()` | Existing client query, centered loading window and exclusive restore; Interface.cpp |
| `LoadArrowCursor()`, `HideArrowCursor()`, `ShowCursorOnExit()` | Preserve original load point, counter normalization and single exit increment |

[DisplayModes.h](../Hunt/Game/DisplayModes.h) owns dimension deduplication,
depth/desktop filtering, order, capacity and fallback. EngineInit copies its
values into the unchanged ResolutionList/ResCount; SetupRes is unchanged.
[FrameTiming.h](../Hunt/Game/FrameTiming.h) owns only the existing integer target
interval and elapsed-counter arithmetic. LimitFPS retains initialization,
sleep threshold, polling, and final counter reset. Neither helper changes an
on-disk format or a renderer algorithm.

`HandleFocusChange(bool)` in Hunt.cpp contains the original engine response.
MainWndProc still observes focus before each message through the explicitly
native `Platform::Win32::IsWindowActive(HWND)`, then handles gameplay itself.
This preserves synchronous creation-message behavior; substituting a cached
window handle during creation would not be equivalent.

The only native bridge header is [PlatformWin32.h](../Hunt/Platform/PlatformWin32.h):
`Initialize(HINSTANCE, WNDPROC)`, `GameWindow() -> HWND`, and
`IsWindowActive(HWND)`. WinMain installs the existing callback. CreateMainWindow
copies GameWindow into the existing hwndMain alias for untouched GDI, DirectDraw,
audio and dialog code; GLRenderer obtains that same handle directly. Native
instance/cursor globals hInst/hcArrow are removed from GameState. This is temporary
scaffolding, not a portable handle API or a claim that all engine headers are portable.

### Final remaining native boundary

The inventory above was rechecked against the final source. Window creation,
styles/sizing/display switches, DPI, display enumeration, mouse coordinates,
clipping/visibility, keyboard polling, QPC/QPF, timer request, sleep, message
pumping and process priority have moved out of gameplay-facing implementations.

MainWndProc retains native parameters/constants, key repeat/extended bits,
GetKeyState(VK_SHIFT), KeyDownMatches/MapVirtualKey, paint validation and default
processing. WinMain retains the Windows entry signature, bridge initialization,
and hwndMain passed to legacy audio. Controls retains MessageBeep; Interface
retains old MapVirtualKey/GetAsyncKeyState UI helpers, GDI, DirectDraw and forced
termination. The deferred timeGetTime/calendar, file I/O, memory backing,
networking, screenshot output and standalone menu rows above remain unchanged.

WGL remains entirely in GLRenderer/GLUtils: HWND/HDC/HGLRC storage, GetDC,
32-bit RGBA double-buffered pixel format with 24-bit depth/8-bit stencil,
temporary context, WGL 3.3 core request and legacy fallback, make-current/delete,
opengl32.dll loading, WGL proc lookup with DLL fallback, and GLAD initialization.
Only the initial HWND source changed. GLRenderer::Present and GLUI::ShowVideo
retain SwapBuffers (GLUI also gets/releases a DC); GLUI retains foreground/focus
activation. No swap interval, context lifetime, shaders or renderer algorithm changed.

## Validation (2026-09-17)

Implementation revision: `77e7b6e`. Local MSVC 19.44.35229 / SDK 10.0.26100.0
under Wine 11.17, using the six established presets and environment helper in
[WINDOWS_BUILDS.md](WINDOWS_BUILDS.md). All default builds and PE architecture
checks pass. All asset-free tests were run, with these individual results:

| Configuration | Full build | Wine tests | Native Windows CI |
| --- | --- | --- | --- |
| x86 GL Debug | Pass | 226/229 | Pass |
| x86 GL Release | Pass | 226/229 | Pass |
| x64 GL Debug | Pass | 229/232 | Pass |
| x64 GL Release | Pass | 229/232 | Pass |
| x86 SOFT Release | Pass | 226/229 | Pass |
| x86 menu Release | Pass | 226/229 | Pass |

[Native CI run 35264994876](https://github.com/willvdb/carnivores2-mee-modernized/actions/runs/35264994876)
passes all 11 test executables in every job, including UI text. Local CTest
remains exit 8, 10/11 executables passing: only the three documented UIText
failures remain. A fresh run of preserved d62905a reproduces exactly the same
assertions (38 versus 38 twice, 145 versus 144, 290 versus 288). No test/warning
is suppressed. The initial x86 Debug core test printed all 136 passes but its
Wine process timed out while baseline builds shared the toolchain prefix; an
isolated core rerun passed in 0.45 s and a complete suite rerun in 8.73 s had
only the known font failures. The original timeout log is retained.
Native baseline and seam CI report the same C4805 (Controls), C4533 (existing
SKIPWEAPON goto), and C4005 (APIENTRY) warning classes; none was added or hidden.

The new test target adds nine portable cases (display filtering/order,
deduplication, desktop inclusion, fallback, capacity, unusual desktop values,
frame intervals and counter arithmetic) and two Win32 cases (all 256 polling
bytes, one-message pump and quit code). Native tests use thread-local keyboard
state and posted messages, with no visible desktop or timing assertions.
GCC C++17 ASan+UBSan passes all nine plus the 27 existing codec cases: **36/36**,
with leak checking and no diagnostics. Clang C++17 ASan+UBSan with
`-Wall -Wextra -Werror` passes the nine portable cases. Their translation unit
includes the public headers first and rejects a leaked windows.h include.
There is no Linux engine/test CMake target or CI change in this branch.

Reproduce Windows checks with the documented six `cleanup-msvc-<preset>`
configure/build commands and `ctest --test-dir build/cleanup-msvc-<preset>
-R '^Carnivores2Tests' --output-on-failure --timeout 60`. Portable reproduction:

```sh
gtest=build/cleanup-msvc-windows-x86-gl-release/_deps/googletest-src/googletest
g++ -std=c++17 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$gtest/include" -I "$gtest" tests/test_platform_policies.cpp \
  tests/test_legacy_{image,audio,map,resource,model,profile}.cpp \
  "$gtest/src/gtest-all.cc" "$gtest/src/gtest_main.cc" -pthread \
  -o /tmp/carnivores-platform-tests
/tmp/carnivores-platform-tests
```

### Bounded runtime checks

Copied user-owned Genesis Redux 1.1 HUNTDAT, profiles and matching OpenAL DLLs
are outside the repository at `/tmp/carnivores-platform-validation/`. Original
content is untouched. Copies use ordinary mouse sensitivity 32 (not the prior
validation's neutral -64), existing W/S/A/D and V bindings, AREA1, weapon 4,
1024x768 windowed launch and an isolated Wine/Gamescope prefix. Exact baseline
5863c4e was compiled separately for x86/x64 GL Release.

New x86 GL, x64 GL and x86 SOFT Release all create the window, load resources,
initialize OpenAL, enter the hunt, respond to injected normal keyboard movement,
show the weapon, and rotate the view with mouse motion. Compositor captures were
inspected. Pause/Escape and exit-prompt/dismiss transitions work; captures and
native probes record cursor visibility, clipping, active state, process priority,
client dimensions and style through focus loss/regain while paused and playing.
Alt+Enter changes to exclusive fullscreen and back. All three evacuate, save
both trophy files, shut down audio and exit 0. Nonzero saved Last.path confirms
movement; all 68 saved binding bytes remain unchanged. Separate WM_CLOSE runs
for all three also exit 0 and log normal engine/audio shutdown.

For **both** GL architectures, the complete 16-state probe transcript is identical
to the corresponding exact-main run. Source order and these comparisons retain
baseline quirks: initial clipping can still describe the loading window; focus
reactivation can leave clipping at desktop bounds; resuming focus while paused
still hides the cursor through renderer SetVideoMode. These are not fixed here.
SOFT retains its expected dithered appearance and passed the same input sequence.
No native Windows gameplay, subjective audio quality, exhaustive keys, monitors,
DPI settings, mods or long sessions are certified by these bounded runs.

Frame-cap measurements use the existing F11 GL performance capture (120 frame
interval samples), without changing the limiter or swap interval. At the default
60 Hz compositor, baseline/seam medians are 16.667/16.668 ms for both GL
architectures. That display also throttles higher caps. An additional headless
360 Hz compositor with external Mesa `vblank_mode=0` distinguishes the settings:

| x64 GL option | Baseline median ms | Seam median ms |
| --- | --- | --- |
| Unlimited | Not sampled at 360 Hz | 1.372 |
| 60 | 16.667 at 60 Hz | 16.667 |
| 120 | 8.334 | 8.333 |
| 240 | 4.165 | 4.162 |

These are plausibility checks, not frame-pacing benchmarks. The existing
integer limiter and process-life timer request remain unchanged. Source and
portable tests establish list filtering/capacity semantics; the runtime sample
is one virtual desktop, not exhaustive physical display-mode compatibility.

Build/CTest logs, baseline warning/font comparisons, sanitizer output, PE types,
native CI logs, probe transcripts, profiles and compositor captures are retained
under `/tmp/carnivores-platform-validation/evidence/`. Local scripts live in its
sibling `tools/` directory. This is temporary validation evidence, not a committed
asset archive. No HUNTDAT, profiles, binaries or instrumentation were added to git.

### SDL handoff

The recommended next branch is **SDL3 Windows GL backend**, with Windows as the
runtime comparison target. Implement the existing portable primitives using SDL
window/display, clock/delay, keyboard/mouse and event APIs. Translate key events
and the 256-byte polling state to the existing legacy binding schema, move native
message decoding out of MainWndProc, and feed the preserved engine focus response.
Replace WGL context/proc loading and both swap sites together, preserving the
requested GL format/profile and renderer startup/shutdown order. Remove the
native GL window accessor when that context boundary no longer needs it.

Handle relative mouse mode as an explicit, separately validated input behavior
step; do not disguise it as an equivalent pointer-warp implementation. Account
for the remaining GDI loading/HUD/text and audio HWND consumers on Windows before
removing the compatibility alias. Include the deferred 32-bit millisecond clock
adapter without changing gameplay units/wrap behavior. Keep filesystem, Linux
engine targets, audio loading/threading, networking, serialization, launcher and
rendering algorithms in their own subsequent slices. Focus quirks, the full-list
desktop limitation and timer-request cleanup deserve explicit decisions/tests,
not silent changes during backend substitution.
