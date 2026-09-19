# Phase 3c: native x86_64 Linux OpenGL bring-up

**Current display support:** Phase 4g adds interactive direct Wayland input,
confirmed Linux fullscreen outcomes and compositor-aware fallback. See
[Linux display behavior](LINUX_DISPLAY_BEHAVIOR.md) for launch choices and the
current acceptance matrix. The Phase 3c validation record below is historical.

Phase 3c runs the actual game through the shared SDL3 backend. The validated
interactive route is **SDL X11, including XWayland**. The standalone launcher,
software renderer and multiplayer remain Windows-only.

This is a stacked milestone, not a replacement for Phase 3b acceptance:

```
main -> port/sdl3-windows-gl -> port/linux-bringup
```

The original reviewed 3b tip was `b9459af9523dd93f4e2b4ec9576f0c2506f75442`.
The current stacked base is **`77c5a59f6b244ec96c4148b2f7f2abfc8dfdb75c`**.
Its only addition is a 3b **test-only** correction: the unknown-scancode test
used `static_cast<SDL_Scancode>(-1)`, which itself invokes undefined enum behavior
under UBSan. It now tests an out-of-array index within the enum's representable
range. That commit was made and pushed on `port/sdl3-windows-gl` first; 3c was
rebased onto it. No SDL Windows implementation bug was found or hidden in 3c.
The stack was subsequently accepted and integrated into `main`. Native
interactive Windows acceptance completed successfully on 2026-09-18, closing
the Phase 3b gate that had held this stacked merge.

## Build and launch

Use x86_64 Linux, a C++17 GCC/Clang toolchain, CMake 3.21+, Ninja, pkg-config,
FreeType, Fontconfig, an installed font such as Liberation Sans, and development
packages for SDL's X11/Wayland/OpenGL backends. OpenAL Soft is loaded at runtime
from `libopenal.so.1`; its absence disables audio with a log entry.

Ubuntu 24.04 dependencies (also used in CI):

```sh
sudo apt-get install build-essential cmake ninja-build pkg-config \
  libfreetype6-dev libfontconfig1-dev fonts-liberation libopenal1 \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev \
  libxi-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev \
  libegl1-mesa-dev libgl1-mesa-dev libdecor-0-dev
cmake --preset linux-x64-sdl-gl-debug
cmake --build --preset linux-x64-sdl-gl-debug --parallel
ctest --preset linux-x64-sdl-gl-debug
```

Use `linux-x64-sdl-gl-release` for Release. CMake fetches the same checksum-pinned
SDL **3.2.28** source as Windows. `-DCARNIVORES_SYSTEM_SDL3=ON` accepts an installed
SDL CMake package, version 3.2.28 or later. These are GL game targets, not helper
compilation targets. Unsupported architectures/renderers are rejected.

The executable is `build/<preset>/bin/Carnivores1_GL`; CMake copies the repository's
`shaders/` next to it. Run from an existing, writable game data directory with
user-owned content and an existing profile. For example, from that directory:

```sh
SDL_VIDEODRIVER=x11 /absolute/path/to/build/linux-x64-sdl-gl-release/bin/Carnivores1_GL \
  reg=0 'prj=HUNTDAT\AREAS\AREA1' din=1 wep=4 dtm=1 -windowed -res=800x600
```

The particular map, dinosaur and weapon arguments must exist in that mod.
There is no Linux menu or new profile format. `reg=0` uses `trophy00.sav`.
The engine retains the existing current-directory asset/save/log/screenshot
locations; it does not migrate files to XDG directories. Configuration lookup
prefers the executable directory, then the working directory. Shader lookup
prefers the working directory, then the executable directory. Keep a mod's
configuration in the same locations it already expects.

## Boundaries and compatibility

| Area | Phase 3c implementation |
| --- | --- |
| Window, events, clock, input, GL | Common `PlatformSDL.cpp`, `LegacyKeyboardSDL.cpp`, `EntrySDL.cpp`; no duplicate Linux engine/platform API. |
| OS compatibility | `PlatformSDLWin32.cpp` retains HWND borrowing, Win32 display ordering and layout projection. `PlatformSDLLinux.cpp` supplies Linux layout projection and leaves SDL display order intact. |
| Memory | `Platform/Memory.*`: Windows heap/page behavior retained; Linux malloc/calloc and mmap backing, with recognizable existing ownership. |
| Files and paths | `Platform/Files.*`: opaque handles, fixed-width transfer counts, native file operations, module/shader lookup and legacy path resolution. |
| CPU overlay | `CPUText::Canvas` is a small standard-C++ text boundary. Windows uses GDI; Linux uses FreeType/Fontconfig and a top-down RGB555 buffer. |
| Loading presentation | GL presents the existing CPU loading pixels through the existing UI shader and platform swap. No shader or HUD-layout change. |
| Audio | `SharedLibrary.*` uses LoadLibrary/GetProcAddress or dlopen/dlsym. Linux uses the same OpenAL calls and samples with a 70 ms worker, recursive mutex and joined shutdown. |
| Startup/system | Portable argv, calendar fields, config tokenization, error display and explicit BMP screenshot writing. Native entry points stay in their own files. |
| Networking | Linux excludes WinSock sources and rejects multiplayer startup; disabled lifecycle stubs do not implement a networking port. |

`Platform.h`, the new support headers, `CPUText.h` and `UIText.h` contain no OS
or SDL types. Tests include them before framework/platform headers and reject
header contamination. SDL types remain in private adapter headers. The GL
renderer continues to use only platform context creation/destruction, procedure
lookup and buffer swap; it has no GLX/EGL/X11/Wayland logic. The RGB8/alpha8,
depth24/stencil8, double-buffer and OpenGL 3.3 Core requests are preserved, as are
GLAD initialization, shaders and renderer algorithms.

The CPU buffer remains RGB555 with a pitch in **pixels**, and byte pitch equal
to twice that value. Linux allocates exactly width times height, including odd
widths, with clipping at the raster boundary. GL uploads explicitly use two-byte
unpack alignment so an odd pixel pitch is not rounded to a four-byte stride. Windows retains its DIB path.
FreeType uses monochrome glyph coverage, proportional advances and the existing
font-cell requests. Fontconfig resolves Liberation Sans with normal fallback.
Install a usable font: unavailable fonts cannot produce readable HUD text.

The existing 256-byte keyboard contract remains: bit 7 down, bit 0 lock state,
legacy VK values, sided modifiers and the existing mouse slots. Linux uses SDL
logical ASCII letters, Windows-compatible number-row digit identities even on
AZERTY, physical US letter fallback for non-Latin layouts, and the shared OEM
fallback. No SDL scancode enters a profile. Input policy and sensitivity remain
in the engine, including its absolute-position/warp mouse-look mechanism.

No CAR, model, map, resource, WAV, profile, trophy or keybinding serializer was
changed. Existing byte fixtures and runtime layout assertions still run.
Runtime native types were replaced with explicit C++ types where Linux needed
that separation; no fake Win32 typedefs were introduced.

### Filename behavior

Both path separators are accepted. On Linux each existing component is first
looked up exactly. If absent, an ASCII case-insensitive directory search is used.
A unique match is accepted; ambiguous case-folded matches fail. Exact spelling
wins even if another differently cased name exists. Writes reuse the spelling
of an existing file; only the final leaf can be newly created. Parent directories
are not invented and user content is not renamed. Symlinks follow normal
filesystem behavior. Unicode/locale-specific Windows case folding is not
emulated. This works with the real mod's uppercase directories and lower/mixed
case command-line/resource paths without converting its data.

### Narrow native-libc corrections

Linux's larger `RAND_MAX` exposed overflowing products in model dithering,
random-map creation and signed random scaling. Only intermediates were widened;
formulas, seeding and the valid Windows arithmetic results are preserved.
Sequences need not be identical across different C libraries.

Native sanitizers also exposed overlapping array copies in ambient filtering
and projectile removal. Isolated commits replace those copies with `memmove`,
using the same indices/counts. These inherited routines predate Phase 3b; they
are not SDL Windows bugs. Linux also skips unused DirectSound geometry building:
OpenAL discards that data, and the legacy routine can index before the map on
the first GL frame. Windows keeps its prior geometry path. These changes address
observed native load/fire failures, not a general gameplay cleanup.

## Validation record — 17 September 2026

Local host: Allosaurus, CachyOS x86_64, AMD RX 7900 XT, Mesa 26.2.2, GCC 16.2.1,
Clang 22.1.8, FreeType 2.14.3, Fontconfig 2.18.3, OpenAL Soft 1.25.2.
Both pinned SDL 3.2.28 and installed SDL 3.4.16 were exercised.

| Linux matrix | Result |
| --- | --- |
| GCC Debug, pinned SDL 3.2.28 | Real game builds; 253 GoogleTest cases in 12 CTest executables pass. |
| GCC Release + LTO, pinned SDL 3.2.28 | Real game builds; all 12 suites pass. |
| GCC Debug, installed SDL 3.4.16 | Build and all asset-free suites pass. |
| Clang Debug, installed SDL 3.4.16 | Build and all 12 suites pass. |
| GCC Debug + ASan/UBSan, installed SDL 3.4.16 | All 12 suites pass with `detect_leaks=1` and `halt_on_error=1`. |
| Native sanitized hunt | Launch, hunt, movement, weapon/fire, pause, Escape and normal close pass; exit 0, no ASan/UBSan/LSan diagnostics. |
| Hosted Ubuntu 24.04 GCC Debug/Release | Both jobs build the real executable and run asset-free tests, with pinned SDL and no proprietary assets. |

The 253 count is GoogleTest test cases, not individual EXPECT/ASSERT expressions.
Coverage includes path separators/case/ambiguity/creation, module lookup,
synthetic legacy loaders, serialized bytes, 32-bit millisecond wrap, legacy key
projection, event/focus clearing, CPU pitch/clipping/RGB555 pixels, padded BMP
bytes, library missing/symbol/reload behavior, and incomplete OpenAL cleanup.

All ten existing hosted Windows jobs remain in CI: Win32/WGL x86/x64
Debug/Release, SDL3 GL x86/x64 Debug/Release, x86 SOFT Release and x86 menu Release.
All build and asset-free test jobs passed at the implementation checkpoints.
Check the final branch-tip run before merging; CI is not native interactive
Windows acceptance. Local MSVC-under-Wine builds were useful cross-checks, but
Wine's font/layout behavior and process lifetime make hosted Windows the
regression authority for the full matrix.

### Actual game runtime

Only already available user-owned Genesis Redux content was used. The source
checkout/CI contains no proprietary assets. The game ran as an ELF Linux process,
with SDL X11 under a headless Gamescope compositor backed by the real AMD GPU.
This is native Linux execution; Wine was not in this runtime path.

The validated Debug/Release passes include:

- SDL initialization, 3.3 Core context request, GLAD and shader initialization;
  the driver reports a compatible OpenGL 4.6 Core context.
- Existing map/resources/CAR/TGA/WAV and the existing 1,660-byte profile load;
  actual loading artwork, world, weapon, HUD and FreeType text visibly render.
- Native `libopenal.so.1`, PipeWire playback device/context, sample sources and
  EFX initialization, followed by OpenAL shutdown. Subjective listening was not
  performed.
- Keyboard movement changes player coordinates; XTEST mouse motion changes view
  angles; weapon use reduces the chamber count from two to one. Pause/unpause,
  Escape/dismiss, and debug text overlay were captured.
- Real focus transfer to a second X window sets `blActive=0`, `NeedRVM=1`,
  destroys GL state, then recreates the context and resumes on regain. A movement
  key released while unfocused does not remain down. Pause also survives focus
  loss/regain without being dismissed.
- Windowed/borderless launches and Alt+Enter transitions. Gamescope does not
  offer the requested 800x600 exclusive mode, so the documented common-backend
  1024x768 desktop-popup fallback was exercised; physical monitor mode switching
  was not certified. The legacy mode policy can retain the resulting size on
  return to windowed mode.
- Escape/Y evacuation exits 0 and writes a 1,660-byte `.sav` and 7,176-byte `.sab`.
  All 68 keybinding bytes at offsets `[1556,1624)` are unchanged. A subsequent
  native hunt loads the resulting profile and normal close leaves it unchanged.
  A direct native `LoadTrophy2` probe also reloads the saved `.sab`: all 7,176
  runtime bytes match, as do all saved key bytes and total-stat bytes. Populated
  trophy-room rendering/collection was not playtested; byte fixtures cover
  populated trophy serialization.
- An 801x601 hunt verifies odd-width RGB555 uploads: ammo, compass, Pause and
  Escape render correctly. The initial check exposed row skew with the default
  four-byte GL unpack alignment; the explicit two-byte upload fixes it.
- Normal close orders OpenAL, GL resources/context, engine and SDL cleanup;
  Debug logs report no tracked allocation leaks.

A separate **native Wayland** launch with Gamescope's exposed Wayland socket
also loads and renders a hunt, then exits normally through a debugger-requested
platform quit. It is only a launch/render/shutdown smoke test. Interactive
Wayland pointer warping, keys, focus and mode changes remain unverified.

Local screenshots, logs, save comparisons and debugger probes are retained under
`/tmp/carnivores-linux-validation/` (not versioned): `debug-pinned-final/`,
`reload/`, `release-final/`, `sanitized-runtime-final/`, `save-focus-final/`, `odd-pitch-final/`, and
`wayland-smoke/`.
They include proprietary-content screenshots and local profile copies and must
not be added to the source repository. Build/test logs are in the same directory.

## Remaining validation limits

- The owner completed native interactive Windows acceptance successfully on
  2026-09-18; the former Phase 3b merge gate is closed.
- Linux interactive acceptance here is an automated smoke hunt on one AMD system
  with one mod. Longer hunts, kills/trophy collection, all mod combinations,
  Intel/NVIDIA drivers, other desktops and physical exclusive mode switches need
  broader testing. There is no packaging/installer/launcher work in this phase.
- Use SDL X11/XWayland for the validated route. Direct Wayland interactive input,
  fractional scaling, multi-monitor transitions and unusual compositor policies
  are not certified. No raw/relative-input redesign was made.
- Linux fonts are substituted, not pixel-identical GDI. English CP1252 bytes are
  decoded; the optional Russian build maps basic Cyrillic/Yo, but has not had
  runtime localization validation. Font weight rendering is not identical.
  The unchanged box fitter can round five rows up to two pixels beyond its
  nominal height with narrower Linux fonts; tests record this explicitly.
- Non-US OEM/dead-key/IME identity is the US physical fallback rather than exact
  Windows layout projection. Synthetic AZERTY/non-Latin tests pass; physical
  non-US keyboard acceptance remains outstanding.
- Resolution enumeration order differs from Windows, so the old saved ordinal
  may choose another supported resolution. Use `-res=WxH`/config resolution;
  no saved format or binding identity was changed.
- Linux always selects OpenAL, including profiles that request the legacy
  DirectSound DLL. Windows retains both original audio backend choices.
- Win32/GDI/WGL, DirectDraw/SOFT, launcher code, native Windows layout/DPI helpers,
  and WinSock remain in explicitly selected/guarded Windows code. Linux does not
  initialize multiplayer; it has no DirectSound backend or 32-bit target.
- Legacy warnings and untested gameplay paths remain. In particular, the
  pre-existing Survival jump in `CheckAfraid` bypasses fear-metric assignments;
  declarations were moved only to make the control flow valid C++, without
  inventing a gameplay fix. This milestone's runtime coverage is normal hunting.

No known 3c-specific blocker remains for the tested single-player X11 route.
The former Windows acceptance hold is closed; the narrower scope of Linux
runtime coverage remains visible for future validation.
