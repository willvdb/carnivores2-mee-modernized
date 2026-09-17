# Win32 shell seam

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
