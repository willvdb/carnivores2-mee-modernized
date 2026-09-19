# Vendored Wayland protocol descriptions

Licensed upstream XML; notices remain in each file. Generated client/server code
is produced by wayland-scanner during the build, not checked in. The application
binds wlr manager version 3 and xdg-output manager version 2. It never calls output
configuration requests. C interface symbols are prefixed locally to avoid SDL
static-link collisions; wire protocol names and messages are unchanged.

Sources retrieved 2026-09-19 UTC:

- wlr-output-management-unstable-v1.xml: https://gitlab.freedesktop.org/wlroots/wlr-protocols/-/raw/master/unstable/wlr-output-management-unstable-v1.xml
- xdg-output-unstable-v1.xml: installed wayland-protocols source, https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/unstable/xdg-output/xdg-output-unstable-v1.xml

SHA-256 of the exact vendored bytes:

- `wlr-output-management-unstable-v1.xml`: `65b0f82a6cf129bf1a1c31a2428795abd33886c15ddd5f3ad97e5922d7bdc3a7`
- `xdg-output-unstable-v1.xml`: `363d547c3eefe8959160cac903ff90b311d4b183005557d73640d9df2cfd7f79`
