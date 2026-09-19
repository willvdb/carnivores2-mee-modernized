# SDL Linux native display mapping properties

Bundled SDL remains at checksum-pinned 3.2.28. The independent
`cmake/patches/SDL3DisplayMapping.cmake` extends only its native display creation:

- X11 RandR displays expose `Carnivores.display.x11.connection` (borrowed
  `Display*`), `.output` (RROutput), and `.root` (root Window). These are explicitly
  project-specific mapping properties, not upstream SDL API or persistent IDs.
  They come from the exact arguments used to construct that SDL display.
- Wayland displays expose `SDL.display.wayland.wl_output` (borrowed `wl_output*`)
  at both initial enumeration and hotplug creation. This backports the public
  property in newer SDL. It carries no serial or cross-session identity.

The properties live as long as their SDL display. Native connections/objects stay
owned by SDL; the game never closes, releases, serializes or dereferences an SDL
private structure. Discovery consumes the mapping on the main thread and copies
only validated metadata into the portable catalog. No SDL layout/ABI assumptions,
new runtime symbol, ordering, window or mode behavior are introduced.

The source patch verifies whole-file hashes before and after every replacement.
Reads canonicalize CRLF to LF before hashing and replacement because Windows
CMake writes CRLF. No other source-content differences are accepted. The patch
chain regression checks both newline forms, repeated application and deliberate
source alteration; pristine pinned source was also checked with Linux and actual
Windows CMake.
The existing X11 ownership patch recognizes the exact combined patched hash so
subsequent configurations remain idempotent; it accepts no arbitrary source.
Its fullscreen correction remains independent. The Wayland disconnect correction
touches a different source file; its behavior remains unchanged. Windows compiles neither
native backend. Both initial and subsequent configure passes are tested locally.

Installed SDL is never patched. Current system SDL's public Wayland property can
support discovery; a system build without it fails closed. X11 system SDL without
the project-specific mapping properties explicitly cannot provide persistence.
There is no fallback using names, rectangle matching or another X connection.

References: [pinned X11 source](https://github.com/libsdl-org/SDL/blob/release-3.2.28/src/video/x11/SDL_x11modes.c),
[pinned Wayland source](https://github.com/libsdl-org/SDL/blob/release-3.2.28/src/video/wayland/SDL_waylandvideo.c),
[current public SDL display properties](https://wiki.libsdl.org/SDL3/SDL_GetDisplayProperties).
The latter must not be mistaken for the older pin's API. That pin exposes the
Wayland global connection, but does not expose per-display native handles until
this backport is applied. Production tests check exact X11 mapping with synthetic
EDID on a disposable Xorg server; real Wayland enumeration checks the backport.
