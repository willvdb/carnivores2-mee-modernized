# SDL X11 disabled-output correction

Pinned SDL 3.2.28 retains a stale display when an XRandR connector remains
connected but its CRTC is disabled. `X11_CheckDisplaysRemoved` considers any
output listed in screen resources active, while `X11_FillXRandRDisplayInfo`
rejects `crtc=0`; the update failure leaves old bounds/modes alive. This makes an
application believe a window is still visible on a disabled monitor.

The bounded local patch removes only entries whose current native output info
is missing, disconnected or has no CRTC. It queries fresh state rather than the
queued event's CRTC because SDL's own fullscreen mode switch briefly disables
the CRTC. Re-enabled outputs use SDL's normal add path and receive new session
IDs. No persistent identity is inferred from those IDs. This is a local fix,
not claimed as an upstream backport; release-3.4.x still has this behavior in
the source inspected during this milestone.

The patch runs after the accepted allocation and native-mapping corrections.
Whole-source SHA256 guards, LF/CRLF handling, idempotence and tamper rejection
remain enforced across the complete chain. Installed SDL is never modified;
its X11 disable support depends on that installed library. Physical replug is
not certified by an Xorg dummy-output regression.

Source: [SDL X11 modes](https://github.com/libsdl-org/SDL/blob/release-3.2.28/src/video/x11/SDL_x11modes.c).
Validation is recorded in PHASE4I_CHECKPOINT.md.
