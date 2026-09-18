# Portable display discovery and resolution selection

## Phase 4a: dimension-first resolution selection

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

Phase 4a did not add refresh or monitor discovery/selection. Phase 4b below adds
discovery only. Windowed/exclusive/borderless modes, precedence, fullscreen transitions,
client-size synchronization, pointer warping, backend behavior, UI and renderer
behavior are unchanged. CI and Wine did not replace physical Windows
acceptance; that Phase 3b gate subsequently completed successfully on
2026-09-18. This seam adds no window/backend behavior requiring a separate
physical-Windows gameplay gate.

## Phase 4b: richer raw display discovery

Based on `port/display-config-core` at
`17b017caae3b268528215e3862713573dc892d55`, this milestone extends runtime
platform values only. Phase 4a remains the dimension-first engine selection
seam; there are no new engine selection call sites or renderer changes.

### Portable API

[Platform.h](../Hunt/Platform/Platform.h) exposes these small value types:

```cpp
struct RefreshRate { std::uint32_t numerator = 0, denominator = 0; };
constexpr RefreshRate MakeRefreshRate(std::uint32_t numerator, std::uint32_t denominator);
struct DisplayMode {
    Size size{};
    std::uint32_t bitsPerPixel = 0;
    RefreshRate refresh{};
};
struct DisplayBounds { Point origin; Size size; };
struct Display {
    std::optional<DisplayBounds> bounds;
    std::optional<DisplayMode> desktopMode;
    std::optional<DisplayMode> currentMode;
    std::vector<DisplayMode> modes;
};
using DisplayIndex = std::size_t;
struct DisplayCatalog {
    std::vector<Display> displays;
    std::optional<DisplayIndex> primaryDisplay;
};
DisplayCatalog QueryDisplayCatalog();
DisplayInfo ProjectDisplayInfo(const Display&, Size fallbackDesktop = {});
DisplayInfo ProjectPrimaryDisplayInfo(const DisplayCatalog&, Size fallbackDesktop = {});
DisplayInfo QueryDisplayInfo(); // Retained legacy primary/default query.
```

`Size` and `Point` retain signed 32-bit components. Bounds preserve backend
coordinates, including negative origins and a primary display away from zero.
They are discovery data, never used for window placement by this milestone.
Bounds are the full display bounds, not the usable work area; SDL coordinates
are kept as supplied, without DPI/pixel-density normalization.

A display's position in `displays` is its `DisplayIndex`. It identifies an entry
**only within that owned snapshot**, with no stability promise between queries,
let alone reboots or hardware changes. `primaryDisplay` references that vector,
so no redundant per-display primary flag can disagree. SDL IDs, OS handles,
monitor ordering, coordinates and names are not persistent identities. No
display identifier is written to config or saves.

Queries run on the application main thread after platform initialization.
The result owns every mode and metadata value. Unavailable bounds or modes
remain disengaged optionals; an unavailable display enumeration produces an
empty catalog. A mode enumeration with no results (including failure) is an
empty vector. There is no hotplug subscription, retry, atomic-topology guarantee
or selection fallback to another display. A missing/out-of-range primary index
projects to the caller-supplied fallback dimensions and an empty mode list.

Projection uses desktop-mode dimensions, then bounds, then the explicit fallback.
It copies modes without filtering, sorting, deduplicating, limiting capacity or
discarding refresh/depth variants. The default projection fallback is `{0, 0}`;
the compatibility queries explicitly retain their historical backend fallbacks.

### Refresh representation and backend differences

Refresh is a rational number of Hz. Positive numerator/denominator pairs are
copied without reduction or rounding: `60/1`, `60000/1001` and `60000/1000` retain
their backend representation. `MakeRefreshRate` maps either zero component to
the canonical unknown value `0/0`; unknown is not a zero-Hz mode. No ordering,
equality, conversion-to-Hz or preferred-refresh policy is introduced. Numerators
and denominators are unsigned 32-bit values, sufficient for the backend fields.

The common SDL implementation enumerates with `SDL_GetDisplays`, identifies
the primary with `SDL_GetPrimaryDisplay`, and copies each display's bounds,
desktop/current modes and fullscreen modes. SDL's two refresh fraction fields
are used instead of its rounded floating-point convenience field. Pinned SDL
3.2.28 finalizes those rational fields, including for drivers that initially
supply only a float. Thus the catalog preserves **SDL-reported** precision; it
does not claim that every driver supplies an exact hardware timing fraction.
Nonpositive fraction components become unknown. See SDL's
[mode fields](https://wiki.libsdl.org/SDL3/SDL_DisplayMode) and the pinned
`src/video/SDL_video.c` implementation of `SDL_FinalizeDisplayMode`.

SDL fullscreen-mode arrays are single allocations freed once with `SDL_free`;
desktop/current mode pointers are borrowed. All values are copied before their
storage can expire. RAII also releases the arrays if a vector allocation fails.
No SDL pointer, ID or native type appears in the portable interface.

Native Win32 uses `EnumDisplayMonitors`/`GetMonitorInfoA` for bounds and primary
metadata and `EnumDisplaySettingsA` for per-device modes. The primary continues
using the default-device query; secondary queries use their native device names
internally. Its `DEVMODE` refresh is integer Hz: values above one become `N/1`,
and the hardware-default values zero/one become unknown. This cannot reconstruct
fractional hardware refresh. See Microsoft's
[DEVMODE documentation](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-devmodea).

The native reference's `desktopMode` and `currentMode` both describe
`ENUM_CURRENT_SETTINGS`, preserving its historical meaning of desktop. Unlike
SDL's desktop/current distinction during exclusive fullscreen, this stateless
native query has no retained pre-exclusive desktop mode. Registry settings are
not substituted as an assumed active desktop. This backend difference is
intentional and must be considered before future policy consumes these fields.

Mode vectors retain all reported entries, including same-dimension refresh/depth
variants and any duplicates resulting from metadata outside this small model
(such as SDL pixel layout/density). They have no legacy 128-entry limit.

### Compatibility and behavior boundary

`QueryDisplayInfo()` uses the same per-display discovery routine as the catalog,
but still queries only the primary/default display. It does not depend on
successful multi-display enumeration or on window position. SDL retains its
desktop -> bounds -> 800x600 fallback. Native Win32 retains current settings ->
primary system metrics. No existing engine caller is migrated to a new policy.

On Windows SDL, the unchanged `SDLCompatibility::OrderDisplayModes` applies
**only to that legacy projection**. It prepends first dimension matches in
`EnumDisplaySettings` order, then appends all SDL modes. The rich catalog itself
retains SDL order and every refresh variant. Linux's ordering shim remains a
no-op. No new refresh sort can alter saved resolution ordinals.

The unchanged `GameDisplay::SelectResolutions` retains backend order, minimum
16 bpp, rejection above either desktop dimension, dimension-only deduplication,
first surviving occurrence, desktop inclusion, the 128-entry cap with no
eviction for desktop, the historical empty-list 800x600 fallback and no 800x600
minimum. Phase 4a exact-dimension selection remains unchanged.

There are **no new config keys**, save/profile/trophy fields or formats.
`OptRes` is still the legacy serialized ordinal; the 68-byte keybinding area
and all serialization code/tests remain unchanged.

Windowed, Borderless, Exclusive, Alt+Enter, FULLSCREEN/BORDERLESS globals,
`ConfigureGameWindow`, `RestoreDesktopMode`, `CenterWindow`, exclusive fallback,
borderless sizing, client-size synchronization, mouse warping, focus and DPI
behavior are unchanged. SDL still requests exact width/height with refresh `0`
in `SDL_GetClosestFullscreenDisplayMode`; native `ChangeDisplaySettings`
refresh behavior is unchanged. Refresh is discovery data only.

### Validation and deferred Phase 4c policy

Asset-free tests cover multiple displays without requiring multiple monitors,
nonzero/negative origins, explicit primary projection, distinct desktop/current
modes, mode order/depth/refresh preservation, missing metadata, raw lists over
128 modes and legacy list filtering/capping. Portable tests include the public
header without a backend and reject backend-header leakage. SDL tests compare
all discovered metadata with SDL and access the copied catalog after shutdown.
Native Windows tests compare primary catalog/projection against native mode
enumeration. The existing Windows SDL versus native/Menu ordinal test is intact.

Local Linux GCC Debug and Release (pinned SDL 3.2.28), Clang Debug (system SDL
3.4.16), and GCC ASan/UBSan (system SDL) each pass **273 cases in 12 CTest
executables**. Sanitizers use leak detection and halt on error. A bounded X11
probe also passes both catalog tests, without creating a window or applying a
mode. Commands and logs for this milestone are recorded in the implementation
report; hosted CI remains the Windows matrix authority, not physical acceptance.

Run all asset-free tests with `ctest --preset linux-x64-sdl-gl-debug` (or the
Release preset). To probe local SDL X11 metadata without a window:

```sh
CARNIVORES_TEST_DISPLAY_DRIVER=x11 \
  build/linux-x64-sdl-gl-debug/Carnivores2SDLTests --gtest_filter='SDLDisplayCatalog.*'
```

That environment variable belongs solely to the test executable, not game
configuration. Normal CI tests use SDL's dummy video driver; Windows retains
its additional real-driver ordinal comparison.

Standalone GCC 16.2.1 and Clang 22.1.8 builds of the portable catalog/policy
tests also pass **26/26** each with C++17, `-Wall -Wextra -Werror`, ASan/UBSan
and leak detection. Local build/test logs are in
`/tmp/carnivores-display-catalog/` (not versioned).

The additional **sanitized real-X11** probe passes its two assertions suites,
but exits 1: system SDL 3.4.16/X11 teardown produces a LeakSanitizer report
(100,032 bytes / 1,824 allocations). A minimal executable containing only
`SDL_Init(SDL_INIT_VIDEO)`/`SDL_Quit` also reports a leak (50,016 bytes / 912
allocations), without any engine or catalog code. This is a separate system
dependency investigation, not a confirmed lower-phase engine defect or a
catalog fix. Its precise dependency ownership remains unresolved. No leak
suppression or dependency change is included here; the full 273-case dummy
video sanitizer run is clean.

Phase 4c must explicitly decide how to consume this catalog: monitor selection,
refresh matching/preference and any fallback rules. Monitor/refresh persistence,
config/UI changes, window movement, hotplug handling and DPI policy remain
deferred. No highest-refresh or desktop-refresh preference is implied here.
Native physical Windows acceptance of Phase 3b completed successfully on
2026-09-18. Direct Wayland is still not certified interactively; this discovery
milestone does not expand Linux/Wayland gameplay or multi-monitor switching
acceptance.
