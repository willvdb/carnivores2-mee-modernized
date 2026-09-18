# SDL 3.2.28 X11 fullscreen mode ownership fix

The default bundled SDL includes a small local modification to fix an XRandR
mode-record leak. The upstream archive version and SHA-256 remain unchanged;
`cmake/patches/SDL3X11ModeLeak.cmake` applies the modification during configure.
This dependency fix is independent of Phase 4d display targeting.

## Cause and ownership

`X11_FillXRandRDisplayInfo` allocates both display data and desktop-mode data.
`X11_UpdateXRandRDisplay` ordinarily transfers the latter to
`SDL_SetDesktopDisplayMode`. While exclusive fullscreen is active, that setter
returns without taking ownership: the temporary fullscreen mode must not replace
the saved desktop mode. The X11 caller freed only the temporary display data.

The modification frees the unadopted mode data in that fullscreen case. Outside
fullscreen, the original ownership transfer remains. Bounds and content-scale
updates still run in both cases. The shared setter and other video backends are
unchanged; callers on other backends can have different ownership arrangements.

The original SDL-only OpenGL reproducer leaked 32 bytes in four allocations
after two exclusive/windowed cycles on a virtual secondary display. The leak
predates the game's display-targeting changes and also reproduced with the
installed SDL 3.4.16. Upstream SDL sources examined during diagnosis:

- [3.2.28 X11 caller](https://github.com/libsdl-org/SDL/blob/release-3.2.28/src/video/x11/SDL_x11modes.c)
- [3.2.28 desktop-mode setter](https://github.com/libsdl-org/SDL/blob/release-3.2.28/src/video/SDL_video.c)

## Dependency integration

The CMake-only patch runs on every bundled-SDL configure, including existing
populated build directories and `FETCHCONTENT_SOURCE_DIR_SDL3` overrides. It
accepts only the exact original file or the exact patched file, checks the result
before writing, and does not rewrite an already-patched file. Unexpected source
changes fail configure and require review. No external patch utility is needed
on Windows. Review/remove this patch when updating SDL to a release containing
an upstream fix.

`CARNIVORES_SYSTEM_SDL3=ON` continues using the installed package unchanged. It
does **not** patch the operating system's SDL; packagers choosing that option
must arrange an equivalent dependency fix themselves.

## Reproducing the regression

`Carnivores2SDLX11ModeLeakTest` links only SDL. It installs SDL allocation hooks,
enters/leaves an exact 800x600 exclusive mode repeatedly, checks the selected
display and saved/restored desktop dimensions, and requires zero SDL allocations
after shutdown. It uses an OpenGL-capable window, matching the original
reproducer and the game's video path. No proprietary assets are required.

An ordinary CTest run skips this test because it changes display modes. The
Linux CI job explicitly runs it on a new Xorg dummy server with Openbox, a
1920x1080 primary and a 1280x1024 secondary. Each display receives 20 cycles;
`xrandr` also checks desktop restoration after the processes exit.

On a system with Xorg, its dummy driver, Openbox, xrandr, and xprop installed:

```sh
cmake --preset linux-x64-sdl-gl-debug
cmake --build --preset linux-x64-sdl-gl-debug
bash tests/x11/run_mode_leak_test.sh \
  build/linux-x64-sdl-gl-debug/Carnivores2SDLX11ModeLeakTest
```

For an **already isolated** X11 server, a direct run accepts a display index and
optional cycle count (1–100). The default is 20 cycles. CTest can opt in using
`CARNIVORES_SDL_X11_LEAK_DISPLAY`; without that environment variable or a direct
index argument the executable exits with skip code 77.

```sh
DISPLAY=:93 SDL_VIDEODRIVER=x11 \
  build/linux-x64-sdl-gl-debug/Carnivores2SDLX11ModeLeakTest 1 2
```

For sanitizer validation, configure both C and C++ with
`-fsanitize=address,undefined -fno-omit-frame-pointer`, then run with
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1`. Instrument bundled SDL as well as the test.
Do not add leak suppressions or disable leak detection.

## Validation scope

Local GCC Debug/Release and Clang builds and ordinary asset-free suites pass.
The unpatched OpenGL regression fails with four outstanding SDL allocations and
a 32-byte LSan report after two cycles. With the patch, 20 cycles on each virtual
display finish with zero SDL allocations, clean ASan/UBSan/LSan results, and
restored desktop modes. This is controlled X11 validation, not physical monitor
or direct-Wayland acceptance.

The separately reproduced Linux/X11 initialization/teardown leak remains out of
scope. In a non-OpenGL variant, LSan still reports that separate allocation
family (50,016 bytes in 912 allocations on this machine). The OpenGL regression's
clean result does not establish that every SDL initialization path is leak-free.
No sanitizer suppression is part of this change.

Fully instrumenting SDL also exposes a separate UBSan failure in its dummy
video driver: `SDL_GetFullscreenDisplayModes` calls `memcpy` with a null source
and zero length when no fullscreen modes exist (`SDL_video.c:1341`). An SDL-only
catalog probe reproduces it with the unmodified 3.2.28 source. Consequently the
full sanitizer CTest run has 12 passing executables, one skipped opt-in runtime
test, and one failed SDL test executable; it is **not** an all-green sanitizer
suite. The X11 fullscreen regression itself passes with all sanitizers enabled.
The dummy-driver defect is recorded separately and is not patched here.

No engine policy, window placement, refresh selection, config/save/profile
format, or Menu behavior changes are included.
