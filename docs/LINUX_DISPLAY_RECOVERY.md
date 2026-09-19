# Live Linux display recovery and dimensions (Phase 4i)

This extends the accepted [Linux presentation](LINUX_DISPLAY_BEHAVIOR.md) and
[serial-backed preference](LINUX_DISPLAY_IDENTITY.md) contracts. Windows native,
Windows SDL, SOFT, Menu, .sav (1660 bytes), .sab (7176 bytes), signed OptRes and
68 binding bytes retain their compatibility paths. No settings are written by
resize, focus, scale or topology handling.

## Requested and applied state

On Linux, `DisplayConfiguration.size`, mode, monitor preference and rational
refresh retain the user's request. `WinW/WinH`, pitch, centers, CPU overlay,
OpenGL viewport, camera projection and readback dimensions describe the effective
drawable. A clamp, unsupported-size fallback or interactive resize no longer
becomes a different requested resolution. Alt+Enter reuses the retained request.
Windows retains the earlier actual-client-size configuration projection.

The existing vertical FOV and square-pixel projection formulas are unchanged.
The scene, HUD, sky scissor, night texture, screenshot and depth-readback paths
already share WinW/WinH; publishing those dimensions with the CPU allocation and
viewport before a frame keeps these paths consistent. There is no new render
scale, FOV, HUD multiplier or intermediate framebuffer.

| Value | Units and authority |
| --- | --- |
| Requested resolution | Existing config/profile/CLI dimensions, retained as intent |
| SDL window size | SDL window-coordinate units from SDL_GetWindowSize; Wayland may already map surface coordinates for emulated fullscreen |
| Drawable / WinW / WinH | Actual pixels from SDL_GetWindowSizeInPixels; never multiply by content scale |
| SDL bounds and modes | SDL-reported coordinates and nominal mode dimensions; not universally physical scanout pixels. Wayland desktop mode pixel_density can differ from content_scale |
| SDL mouse data | SDL window-coordinate units, including SDL's Wayland emulated-fullscreen conversion |
| Platform pointer / mouse-look | Drawable-pixel space; convert using the actual queried window-to-pixel ratio, and invert it for legacy recenter warps |

The window deliberately keeps its existing **non-high-pixel-density** flag.
On Wayland that preserves configured rendering resolution while the compositor
scales the surface, including fractional output scales. Enabling high density
would implicitly change resolution/HUD behavior. SDL's actual drawable remains
authoritative if a backend, fullscreen mode or resize supplies different pixels.
Wayland keeps relative mouse-look; fractional deltas survive conversion. X11 and
Windows SDL keep absolute recentering, while native Windows remains unchanged.

A minimized, zero/invalid logical size, or invalid drawable suspends rendering
and capture. Valid drawable dimensions are 2..8192 per axis and at most 16,777,216
pixels (32 MiB CPU overlay, 64 MiB RGBA readback). Last safe storage is retained;
no zero-size projection/division or huge compositor-driven allocation occurs.
A non-minimized invalid drawable can still recover to a live output. Restore
resumes at valid metrics; topology recovery keeps capture released until those
metrics are valid and the window is reachable; unsupported larger drawables remain suspended until
the user/compositor returns to valid dimensions. This is not GL-context recovery
from a permanently dead or frozen compositor.

## Recovery policy

Native event handling only forwards owned notices. The Game idle/frame boundary
reads current metrics and coalesces display events: 150 ms quiet time, or a 500 ms
burst deadline checked at that boundary. It never reconfigures from an SDL
callback. Geometry updates also cover pixel-size changes with unchanged logical
size, normal resize, output scale/orientation and compositor-directed movement.

A fresh owned catalog is used when the occupied output was removed, the window
is no longer reachable on any live output, or an applied saved identity becomes
missing/ambiguous/unsupported. That request resolves saved intent again. Missing
identity **and missing/out-of-range/unusable session targets** fall back to
primary/default with automatic refresh; a former target's rational rate never
transfers to the fallback. This intentionally corrects earlier session-index
fallback behavior. Intent remains unchanged. An empty catalog suspends until an
output returns; it is never handed to a native fullscreen retry.

The currently occupied output is tracked privately through SDL and differs from
the preference. On X11, bounds intersection uses signed wide arithmetic for
negative/rotated layouts. On Wayland, placement belongs to the compositor; the
reported occupied output is used because global window positions do not exist.

A returning preferred monitor does **not** automatically pull a reachable window
back. The next explicit mode request resolves it again. Reordering similarly
does not move a reachable window; a session index refers to the newly enumerated
catalog on the next explicit application/recovery, never to an inferred panel
identity. A user-moved window and its dimensions survive focus/context restoration.
Primary-only changes do not steal the window either; fresh primary metadata is
used on the next application/recovery.

Rejected recovery is attempted once per settled topology. Self-generated mode
notifications and duplicate loss events cannot generate an endless fullscreen
loop. A distinct topology or deliberate user mode request permits a new attempt.
The previous 4g bounded fullscreen confirmation/fallback policy remains in place.
Externally changed sizes/modes are reconciled without repeatedly fighting the
compositor's physical mode/refresh choice.

## Application-time revalidation and native limits

An identity-selected Linux target now carries an owned identity precondition in
addition to bounds. Before remapping to a private SDL ID, the backend refreshes
native identities against that exact SDL enumeration. A replacement occupying
the same rectangle cannot inherit the absent panel's requested rate. After SDL
restoration/application, lost native targets still receive one automatic primary
attempt. Discovery and application cannot be an atomic OS topology transaction;
subsequent notices trigger frame-boundary recovery.

Bundled X11 uses the existing borrowed mapping to read current RandR primary,
since SDL 3.2 otherwise keeps its initial primary order. Missing native mapping
or an unset RandR primary keeps SDL's default. Wayland primary remains SDL's
backend heuristic. No native connection/ID crosses into Game state.

The separate [disabled-output correction](SDL_X11_DISABLED_OUTPUTS.md) is required
for bundled SDL to remove a connected connector whose CRTC is disabled. It
queries current state so SDL's temporary disable during a mode switch is not
mistaken for removal. Installed SDL is never patched; its disabled-output and
live-primary capabilities depend on the available library/mapping. Linux serial
identity remains conditional on the 4h protocols/metadata, not universal.

Physical replug/docking, physical mixed-DPI pointer feel, GPU/compositor diversity,
Windows multi-monitor acceptance and the existing Hyprland/XWayland secondary
exclusive limitation remain unaccepted. A virtual Xorg secondary 1920x1080
native mode attempt also exposed an inherited RandR BadValue path; this work does
not redesign SDL's multi-output native mode setting. Tested unsupported-size
fallback and monitor removal do not rely on that failing mode.

## Regression evidence

- The accepted 4h game kept an 800x600 viewport after its virtual X11 client
  became 640x480. The corrected game uses 640x480 pixels while retaining an
  800x600 request; subsequent mode toggles request 800x600 again.
- Actual X11 hunts cover user movement, unrelated layout changes, minimize /
  restore, output removal/return, primary replacement and preserved profile bytes.
- Supervisor review caught a viewport publication omission and invalid-size
  early return. A corrected actual-game probe injects a transient zero drawable
  during removal, changes fallback 1280x1024 -> 1920x1080, and reads GL_VIEWPORT
  directly to verify it equals each rendered size while request stays 777x555.
- The production asset-free X11 fixture covers disable/re-enable, event delivery,
  automatic fallback, current primary and transient fullscreen switches. Linking
  it to the accepted unpatched SDL is a failing negative control (disabled output
  incorrectly remains in the catalog). The new patch passes. The inherited 40
  fullscreen cycles retain zero SDL allocations and restore both desktops.
- Nested Weston uses genuinely different 1x/2x desktop pixel densities and tests
  both outputs through production configuration; drawable resolution remains
  800x600. Existing presentation/fallback/disconnect checks remain in CI.
- An actual hunt on a private headless Sway compositor verifies 1.5x -> 2x ->
  1.25x, rotation, output removal and return from compositor readback, continues
  rendering, performs one recovery, and exits normally with format/binding
  preservation. This is virtual compositor evidence, not physical acceptance.
  A nested KWin X11 attempt ignored configuration requests despite zero exit
  status; it is explicitly excluded from scale/rotation acceptance.

Commands, exact build/test results, sanitizer limits and CI are recorded in
[PHASE4I_CHECKPOINT.md](../PHASE4I_CHECKPOINT.md). Opt-in runtime regressions use
`tests/x11/run_mode_leak_test.sh` and `tests/wayland/run_presentation_test.sh` with
`Carnivores2DisplayRecoveryTest` as their final argument. They create disposable
servers and do not change the host compositor.
