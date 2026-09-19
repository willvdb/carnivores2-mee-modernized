# Linux display behavior and direct Wayland gameplay (Phase 4g)

Current live Linux recovery and dimension semantics: [Phase 4i](LINUX_DISPLAY_RECOVERY.md). Earlier deferred descriptions below are historical.

Current Linux opt-in serial identity support is documented in [Phase 4h](LINUX_DISPLAY_IDENTITY.md). Earlier unsupported-identity descriptions below are historical.

The game supports an interactive direct Wayland route on compositors offering
relative pointer and pointer constraints, alongside the existing SDL X11 route.
Native Wayland is selected with `SDL_VIDEODRIVER=wayland`; `SDL_VIDEODRIVER=x11`
selects X11 or XWayland. With neither override, SDL chooses an available driver;
the game now logs its actual driver. SDL 3.2.28 prefers Wayland when available.
The configuration/profile/CLI precedence and generated default config are unchanged.
No monitor preference is automatically saved.

## Presentation semantics

All confirmation and diagnostic values are **SDL-reported state**, not independent
measurements of physical scanout. In particular, XWayland can expose emulated
modes, and Wayland never grants this application arbitrary physical mode/refresh
control. A `refresh_rate` request retains exact rational, target-specific selection
semantics; an unavailable rate uses automatic on the selected output.

| Mode | X11 / XWayland | Direct Wayland |
| --- | --- | --- |
| Windowed | Request a centered decorated window on the selected output; the window manager can redirect it. | Request a decorated client size; the compositor chooses placement. `-display=N` cannot place a normal toplevel. |
| Borderless | Retain the historical per-axis size clamp and centered undecorated window. | Same size request/clamp, but placement belongs to the compositor. This remains an undecorated normal window, not a new always-full-monitor mode. |
| Exclusive | Request the exact target mode and confirm settled SDL fullscreen flag, target, render dimensions and exact reported mode/refresh. XWayland modes may be virtual. | Request SDL's emulated fullscreen size on the selected output; confirm settled flag, output and render dimensions. The compositor scales the content and controls physical refresh. |
| Exclusive request rejected/unconfirmed | Leave fullscreen and request the existing target desktop-size popup. Report the actual output; never report a rejected target request as success. | Leave fullscreen, then make one target desktop-size fullscreen attempt. If that is also unconfirmed, request a decorated compositor-placed window and report the actual state. |

The Linux application no longer treats a successful fullscreen setter as proof
that asynchronous application completed. It synchronizes, inspects reported state,
and logs a failed/unconfirmed request before fallback. An SDL target that vanished
or became ambiguous still falls back to primary/automatic, using the existing
owned full-rectangle mapping. No ordinal, SDL ID, coordinate or display label is
promoted into a persistent identity. Explicit output indices remain session-only.

Each application logs requested mode/output, synchronization result, actual
fullscreen flag/output/render pixels, and SDL-reported mode/refresh. A redirected
normal window warns which actual output received it. Wayland logs explain that
normal-window placement and physical scanout remain compositor-controlled. Its
unsupported position call is no longer attempted. SDL's notion of primary on
Wayland is a backend heuristic, not a protocol guarantee of a compositor primary.
In the default borderless Weston test, growing the small loading window kept its
compositor-chosen position and left part of the game window off-screen. Normal
windows may need compositor-managed movement; `-fullscreen` uses the validated
output-sized presentation route. The engine cannot force normal-window placement.

There are no recursive or indefinite engine retries. SDL's dead-connection
pending-window loops receive a separate bounded correction documented in
[SDL_WAYLAND_DISCONNECT.md](SDL_WAYLAND_DISCONNECT.md). A live compositor that
stops replying can still block a native Wayland roundtrip; this patch does not
implement compositor recovery or a general event-loop timeout framework.

## Mouse-look and focus

Pinned SDL's Wayland warp implementation sets an advisory locked-pointer position
hint and unlocks again. In isolated KWin and Weston reproductions, ten equal
+25/+5 input movements yielded growing legacy look deltas (25, 50, …, 250).
Re-centering SDL's cached absolute coordinates did not recenter the real pointer.

Only the Wayland backend now enables relative mode for game capture and consumes
one frame of relative motion. Fractional motion is retained. Pausing/focus loss
releases capture; inactive reads and the existing reset calls drain stale motion.
A compositor without relative capture logs an explicit failure and supplies zero
look delta; use the X11 route there. There is no unreliable absolute-coordinate
fallback that can spin the view. Normal shutdown releases capture.

The game still owns sensitivity, inversion, smoothing, pause and focus policy.
Windows native/SDL and Linux X11 retain the previous integer pointer-minus-center
calculation and recentering. The 68 legacy VK binding bytes, .sav=1660 bytes,
.sab=7176 bytes and signed OptRes are unchanged. This is not a general input,
controller, renderer, scaling or settings redesign.

## Acceptance evidence and limits

| Environment | Validated here | Remaining limit |
| --- | --- | --- |
| Virtual Xorg dummy + Openbox, two outputs | Ten full hunts, primary/default/secondary, exact refresh and automatic fallback, borderless sizing, Alt+Enter, exit/profile checks; 20 fullscreen cycles/output restore both desktops with zero SDL allocations, including ASan/UBSan/LSan. | Virtual modes and software rendering do not certify physical monitor switches. |
| Nested KWin + XWayland, two outputs | SDL-only secondary desktop fullscreen; actual default/secondary/unsupported-size hunts and restoration. | This does not resolve the earlier real Hyprland/XWayland secondary-exclusive rejection. See checkpoint for exact input-transition coverage. |
| Nested Weston, direct Wayland, two outputs | Actual hunt rendering, relative look, movement, Pause, Escape/dismiss, Alt+Enter, focus loss/context teardown/regain through another native app, evacuation and byte-preserving saves; target desktop fallback; clean instrumented hunt. | Physical pointer feel, physical multi-monitor transitions, long hunts and other GPUs/compositors remain unaccepted. |
| Nested KWin, direct Wayland | Baseline warp defect reproduced; relative API enable succeeds. | Synthetic relative input in that nesting fixture was not accepted as gameplay evidence. |
| Real Hyprland/XWayland | Prior primary/secondary normal-window evidence retained. | Earlier SDL-only exclusive request reached primary after sync failure; new fallback has not been physically accepted there. |
| Windows native/SDL, SOFT, Menu | Existing 12-job Windows/Linux CI matrix and golden formats remain required at final head. | This milestone does not replace physical Windows multi-monitor acceptance. |

The disposable production-backend regression runs loading presentation, two cycles
per output, unsupported-size target fallback and restoration, without assets. A
separate SDL-only regression disconnects only its own Wayland socket while a
fullscreen request is pending. Both run in the existing Linux CI jobs through
`tests/wayland/run_presentation_test.sh`; ordinary CTest skips them unless opted in.
Delayed, rejected, timed-out, wrong-output, wrong-size and mismatched exact-refresh
responses are also tested through injected native operations used by production.

Local evidence lives under `/tmp/carnivores-phase4g/`. A rapid capture-churn test
crashed extracted Weston 15; that external compositor result is retained, and the
presentation regression is separated from it. Actual focus/capture hunts pass.
The known independent SDL X11 initialization/teardown leak and pinned dummy-video
zero-length memcpy UBSan failure remain unsuppressed; full sanitizer CTest is not
claimed wholly green. Standalone Wayland GL presentation/disconnect probes also
report unsuppressed third-party lifetime allocations (Fontconfig/Pango/GLib and
unknown unloaded-module frames); disabling optional libdecor narrows but does
not eliminate these reports. All their behavioral assertions pass; the full
game has its own font/renderer teardown and passes the instrumented hunt. No
claim of a completely leak-free standalone SDL/driver stack is made.
Linux persistent identity, hotplug recovery and mixed-scale
logical/pixel reconciliation are explicitly deferred to 4h/4i.

Primary references: [SDL 3.2.28 Wayland source](https://github.com/libsdl-org/SDL/blob/release-3.2.28/src/video/wayland/SDL_waylandwindow.c),
[pinned pointer implementation](https://github.com/libsdl-org/SDL/blob/release-3.2.28/src/video/wayland/SDL_waylandmouse.c),
[pinned mode emulation](https://github.com/libsdl-org/SDL/blob/release-3.2.28/src/video/wayland/SDL_waylandvideo.c),
[SDL synchronization contract](https://wiki.libsdl.org/SDL3/SDL_SyncWindow),
[relative capture contract](https://wiki.libsdl.org/SDL3/SDL_SetWindowRelativeMouseMode).
