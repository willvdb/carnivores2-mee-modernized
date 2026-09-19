# SDL Wayland disconnect wait correction

Bundled SDL remains checksum-pinned at 3.2.28. This isolated local correction
checks the result of `wl_display_roundtrip` in `Wayland_SyncWindow` and
`FlushPendingEvents`. A negative result means the connection cannot deliver
pending fullscreen/maximize callbacks. Synchronization now returns false with
an error; the flush stops instead of retrying forever. Successful roundtrips and
live-compositor transition behavior are unchanged. No engine format changes.

The correction is applied with CMake and before/after whole-file SHA-256 guards
in `cmake/patches/SDL3WaylandDisconnect.cmake`. It runs on every configure and
accepts only the original or already-patched pinned file. Windows does not build
this Wayland source. Installed/system SDL is not modified. The existing X11
fullscreen ownership patch remains separate and intact.

## Reproducer and ownership

A nested Weston 15 compositor crashed during rapid cross-output pointer capture
in a display test. The compositor coredump is an external fixture issue; the
engine's actual hunts, input and focus tests had already passed. The disconnected
client then exposed an independent SDL defect: both loops ignored roundtrip
failure while their callback counters stayed nonzero. A normal SIGTERM timeout
could not stop that loop; the disposable client required SIGKILL.

`tests/sdl_wayland_disconnect.cpp` reproduces the SDL issue without crashing or
killing any compositor: it creates its own SDL window, queues fullscreen, and
shuts down only its own Wayland socket. Unpatched 3.2.28 never returns from sync;
the same test with this correction promptly reports failed sync, completes a
pending-state flush, destroys its context/window, and exits 0. The test is opt-in
and is run by the disposable Wayland regression harness. The harness has a
hard-kill deadline so the negative control cannot strand CI.

Source inspection found the same unchecked loops in upstream release-3.4.0;
there is no evidence-backed reason for a dependency upgrade to fix this issue.
References: [pinned source](https://github.com/libsdl-org/SDL/blob/release-3.2.28/src/video/wayland/SDL_waylandwindow.c),
[3.4.0 source](https://github.com/libsdl-org/SDL/blob/release-3.4.0/src/video/wayland/SDL_waylandwindow.c).
This is not recovery from a crashed compositor or a deadline for a live server
that stops replying. It prevents spinning after a reported connection failure.
SDL's ordinary disconnect/quit path remains responsible for application exit.
No sanitizer suppression or unrelated vendor cleanup is included.
