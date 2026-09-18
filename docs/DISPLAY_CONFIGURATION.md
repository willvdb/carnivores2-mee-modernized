# Phase 4a: dimension-first resolution selection

This compatibility seam is based on `port/linux-bringup` at
`9133537bd36e9de774e1a01d66cd2f807c0d1ca4`. Runtime resolution identity is moving
toward width and height. The existing `ResolutionList[128]` now stores
`Platform::Size` values; `ResCount`, `WinW`/`WinH`, `CurRes` and `OptRes` remain.
There is no additional persistent display-state object.

## Engine policy and backend boundary

[ResolutionSelection.h](../Hunt/Game/ResolutionSelection.h) has two portable,
engine-layer operations:

- `FindResolution` returns the first exact dimension match in the current
  ordered list, or `-1`. It neither validates dimensions nor changes the list.
- `ResolveLegacyResolution` translates an ordinal into dimensions and the
  compatibility ordinal. A valid ordinal selects that entry. An invalid
  ordinal selects the first 800x600 entry, or index 0 if absent. A nonpositive
  count returns 800x600 and leaves the supplied ordinal unchanged.

`SetupRes()` uses the legacy adapter; config and command-line overrides use
the exact lookup. The helper includes only the portable `Platform.h` surface
and has no renderer, Menu, SDL or native window dependency.

[DisplayModes.h](../Hunt/Game/DisplayModes.h) retains the existing list policy:
backend order, depth of at least 16 bits per pixel, exclusion above either
desktop dimension, first surviving occurrence per dimension pair, desktop
append if absent, 128-entry capacity without eviction, and the historical
800x600 empty-list fallback. There is no 800x600 minimum-resolution filter.

`Platform::QueryDisplayInfo()` still supplies backend information only. The
Windows SDL `EnumDisplaySettings` ordering shim remains unchanged to keep
legacy ordinals aligned with the native reference and standalone Windows Menu
as closely as possible. Linux still preserves SDL's enumeration order.

## Persistence and source precedence

`OptRes` remains the legacy serialized ordinal adapter, not a stable resolution
identity across differently ordered display lists. The profile remains exactly
**1,660 bytes**, with the signed 32-bit resolution ordinal at byte offset
**1528** and the unchanged **68-byte** keybinding region at offset **1556**.
No serializer, fixture, format version, extension or Menu contract changes.

The effective startup order remains:

1. Enumerate displays, load the legacy profile, then `SetupRes()`.
2. Apply `config.cfg`, including its existing `resolution WxH` override.
3. Apply the final command-line pass, including `-res=WxH`, `/res=WxH` and
   `/vmode1` through `/vmode5`.

The earlier command-line pass for startup/session options is also unchanged.
Both dimension parsers still accept `x` or `X` and require positive dimensions.
The standalone Menu already writes the dimension-based config key.

| Dimension override | Exact list match | No list match |
| --- | --- | --- |
| config.cfg | Set `OptRes` to that index; leave `CurRes` alone | Set `OptRes = -1`; leave `CurRes` alone |
| command line | Set both `OptRes` and `CurRes` to that index | Preserve both previous ordinals |

In all four cases, `WinW`/`WinH` take the requested positive dimensions. The
unmatched config/CLI difference is deliberately preserved for a later explicit
compatibility decision. A subsequent save still writes the current `OptRes`.

## Validation and deferred work

The asset-free portable policy tests characterize exact lookup, ordinal
fallbacks, empty lists, each caller's unmatched assignment semantics, and a
profile -> config -> CLI policy composition. This is not an integration test
of the parsers or startup sequence. Existing display-list, complete/truncated
profile fixtures and serialized-layout tests remain intact.

Refresh-rate and monitor identity/selection remain deferred to later Phase 4
work. Windowed/exclusive/borderless modes, precedence, fullscreen transitions,
client-size synchronization, pointer warping, backend behavior, UI and renderer
behavior are unchanged. CI and Wine do not replace the still-pending physical
native-Windows acceptance gate for Phase 3b; this seam adds no window/backend
behavior requiring a separate physical-Windows gameplay gate.
