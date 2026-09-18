# Display configuration, discovery and selection

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

Phase 4b left refresh policy and display targeting to later milestones. Phase
4c below consumes only the refresh portion. Display targeting, monitor
persistence, UI changes, window movement, hotplug handling and DPI policy remain
deferred. No highest-refresh or desktop-refresh preference is implied here.
Native physical Windows acceptance of Phase 3b completed successfully on
2026-09-18. Direct Wayland is still not certified interactively; this discovery
milestone does not expand Linux/Wayland gameplay or multi-monitor switching
acceptance.

## Phase 4c: optional exclusive-fullscreen refresh

Based on integrated `main` at **4253c7acab0d999172c2a4ac3c2444891c4b8ddb**,
verified locally and on origin before creating `port/refresh-policy`.

### Configuration and precedence

The optional modern `config.cfg` key is:

```text
# Exclusive-fullscreen refresh rate.
# 0=automatic; integer Hz or exact fraction such as 60000/1001.
refresh_rate 0
```

`0` means automatic and preserves the legacy behavior. An absent key also
means automatic. Positive decimal integers mean `N/1` Hz; positive decimal
fractions mean `N/D` Hz. Each component must fit an unsigned 32-bit integer.
Fractions are kept as written numerically, without reduction. Leading zeroes
on positive components are allowed; the automatic spelling is exactly `0`.
There is no floating-point persistent or authoritative runtime representation.

Examples: `60`, `120`, `144`, `60000/1001`, `120000/1001` and `60000/1000` are
valid. `-60`, `+60`, `60.0`, `59.94`, `60/0`, `0/1`, `0/0`, `abc`, `1/2/3`,
missing values and component overflow are invalid. Invalid input logs the
expected syntax and **clears any earlier preference to automatic**. An invalid
later duplicate therefore cannot leave an earlier explicit setting active.
The config parser accepts outer line whitespace and trailing `#` comments,
but consumes the entire value rather than a truncated token. Extra tokens or
whitespace inside the number/fraction are rejected. Other config parsers are
unchanged.

Command-line equivalents accept both existing prefix styles:

```text
-refresh=60
/refresh=60
-refresh=60000/1001
/refresh=60000/1001
-refresh=0
```

The option name is case-insensitive, consistent with the other display flags.
Numeric syntax is identical; command-line values have no whitespace/comment
syntax. Repeated refresh options use the last occurrence. The startup order
remains default/legacy state -> `config.cfg` -> final command-line pass. Thus
config `60000/1001` plus CLI `-refresh=120` requests `120/1`; CLI `-refresh=0`
restores automatic. Invalid CLI refresh overrides also restore automatic.
The earlier command-line pass applies the same assignment idempotently, and
the final pass restores CLI precedence after config loading. A recognized
refresh argument, including an invalid one, never reaches the legacy
substring-based hunt/session or resolution argument processing.

The preference is the standalone runtime `Platform::RefreshRate
PreferredRefresh`, initially `0/0`. It is passed to `ConfigureGameWindow` and
consumed only in the exclusive branch. It is not an `OptRes` replacement and
is not in any serialized structure. Both first-launch and release-script
config templates document automatic as the default.

The Windows Menu does not own this key. Its existing `Menu/Resources.cpp`
`SaveConfig` list of fresh keys excludes `refresh_rate`; the merge writes
unowned lines verbatim in their existing positions. Source inspection and an
asset-free execution of the **unmodified SaveConfig function body**, with
small options/path test doubles, confirmed rational refresh lines and comments
survive two rewrites while an owned `display_mode` updates. No Menu code, UI,
merge algorithm or binary profile change was needed. This characterization
was not a physical Menu UI test.

### Exact selection policy

[RefreshSelection.h](../Hunt/Game/RefreshSelection.h) has no SDL, Win32,
renderer or parsing dependency. Known rates have both components positive.
`EqualRefresh(a,b)` compares `uint64(a.numerator) * b.denominator` with
`uint64(b.numerator) * a.denominator`. Every product of two uint32 values fits
in uint64. Unknown/invalid pairs never equal real rates (or each other).

Consequently `60/1`, `60000/1000`, `120000/2000` and `120/2` match, while
`60000/1001` does not match `60/1`. No discovered or preferred fraction is
normalized to enable comparison.

`FindRefreshMode` returns the first backend-order mode with exact width,
exact height, at least 16 bpp and an equal refresh value. Automatic and
unavailable requests return no selection. Its shared `MatchesRefreshMode`
predicate also drives the SDL native-mode adapter. There is no sorting,
nearest-rate search, resolution substitution or highest/lowest/desktop/current
refresh preference. The primary/default display remains authoritative; no
monitor identity or index is configured or persisted.

### SDL backend

For automatic refresh, the existing call remains
`SDL_GetClosestFullscreenDisplayMode(primary, width, height, 0, false, ...)`,
followed by the unchanged exact-width/height check and mode application.
No new enumeration or policy chooses a refresh on this path.

For an explicit preference, the backend enumerates the primary display's raw
fullscreen modes and applies the first native mode passing the shared exact
predicate. It passes that original native mode to `SDL_SetWindowFullscreenMode`
and then enters fullscreen. An unavailable request logs its dimensions and
rational rate and executes the original automatic exact-WxH path. A supported
mode whose application fails retains the existing desktop-popup exclusive
failure behavior. It does not attempt a merely nearby refresh. The preference
remains configured for the next exclusive entry, including Alt+Enter.

The checksum-pinned SDL **3.2.28** contract was checked in both
[`SDL_video.h`](https://github.com/libsdl-org/SDL/blob/release-3.2.28/include/SDL3/SDL_video.h)
and [`SDL_video.c`](https://github.com/libsdl-org/SDL/blob/release-3.2.28/src/video/SDL_video.c):
`SDL_GetFullscreenDisplayModes` returns one allocation containing the pointers
and mode copies; `SDL_SetWindowFullscreenMode` copies the requested mode into
window state. The backend retains the enumeration allocation through both
application calls and releases it once with `SDL_free` through RAII. It stores
no SDL mode pointer. SDL's enumeration order is used as supplied, with no
engine-side sorting. Discovery and legacy `QueryDisplayInfo` are unchanged.

### Native Win32 reference backend

`IntegerRefreshHz` permits only an exactly divisible positive rational. For
example `60000/1000` is exactly 60 Hz; `60000/1001` cannot be expressed and is
never rounded. The backend logs nonrepresentable requests and uses automatic.
`DEVMODE` also reserves frequency 0 and 1 as hardware-default sentinels, so an
explicit 1-Hz request uses automatic with a warning rather than claiming exact
1-Hz support.

A representable request adds `DM_DISPLAYFREQUENCY` to a separate `DEVMODE` and
tries the existing 32-bpp then 16-bpp order. If both attempts fail, it logs and
retries the original automatic 32-bpp then 16-bpp block, using a fresh zeroed
`DEVMODE` with **no `DM_DISPLAYFREQUENCY`**. That automatic block is unchanged.
If automatic also fails, existing desktop-popup behavior remains. Native
Win32 is the compatibility/reference backend; SDL Windows is the long-term
cross-platform path for rational refresh discovery and selection.

### Compatibility boundary

Windowed and borderless sizing/placement, loading presentation, desktop
restoration, Alt+Enter state transitions, focus, mouse capture/warp,
client-size synchronization and DPI behavior retain their implementations.
Only exclusive entry receives optional refresh application. No display bounds
are newly used for placement. Vsync, frame limiting and renderer behavior are
unchanged.

`ResolutionList`/`ResCount`, the primary compatibility projection, Windows SDL
ordinal shim, depth/dimension filtering, deduplication, desktop append,
128-entry cap, 800x600 fallback and Phase 4a config/CLI/`OptRes` translation
are unchanged. A dimension entry may have many raw refresh variants.

`.sav` remains **1,660 bytes**, `.sab` **7,176 bytes**, `OptRes` the same signed
32-bit ordinal, and the keybinding region the same **68 bytes**. No serializer,
fixture, padding/reserved field, extension block or format version changed.
All existing golden profile tests still pass. Content stays user-owned and
outside the repository.

### Engineering validation — 18 September 2026

Local host: Allosaurus, native Linux x86_64, AMD RX 7900 XT, GCC 16.2.1,
Clang 22.1.8. Commands, logs, extracted Menu characterization, X11 probes and
runtime artifacts are retained under `/tmp/carnivores-refresh-policy/` and
are not committed. Screenshots and copied profiles there contain user content.

| Local validation | Result |
| --- | --- |
| GCC Debug, pinned SDL 3.2.28 | Game builds; 283 cases in 12 CTest executables pass. |
| GCC Release + LTO, pinned SDL 3.2.28 | Game builds; 283 cases in 12 executables pass. |
| Clang Debug, system SDL 3.4.16, `-Wall -Wextra` | Game builds; 283 cases in 12 executables pass. |
| GCC Debug, system SDL 3.4.16, `-Wall -Wextra`, ASan/UBSan/LSan | Game builds; 283 cases in 12 executables pass. |
| Standalone GCC and Clang portable policy/catalog tests | 35/35 each, C++17, `-Wall -Wextra -Werror`, ASan/UBSan/LSan; no diagnostics. |
| Pinned SDL real-X11 catalog probe | 2/2 tests pass, exit 0. |
| System SDL sanitized real-X11 catalog probe | 2/2 assertions suites pass; exit 1 from the separately reproduced teardown leak below. |

Full-engine warning builds retain existing unrelated warnings; no warning
suppression or unrelated cleanup was added. Sanitizer runs use
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1`. The new portable tests cover parser grammar,
overflow, automatic clearing, full config values, CLI precedence/idempotence,
unrelated/invalid argument isolation, exact rational equality near uint32
limits, backend-order mode selection/depth filtering and exact integer-Hz
conversion. A synthetic SDL test selects native mode identities with deliberately
misleading float fields, equivalent unreduced fractions, low-color candidates,
fractional values and unavailable/automatic requests. Existing catalog
ownership and legacy-order tests are retained unchanged.

Representative commands (use the repository's configured build directories):

```sh
cmake --build --preset linux-x64-sdl-gl-debug --parallel
ctest --preset linux-x64-sdl-gl-debug
cmake --build --preset linux-x64-sdl-gl-release --parallel
ctest --preset linux-x64-sdl-gl-release
ctest --test-dir build/linux-x64-sdl-gl-clang --output-on-failure
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/linux-x64-sdl-gl-sanitized --output-on-failure
CARNIVORES_TEST_DISPLAY_DRIVER=x11 \
  build/linux-x64-sdl-gl-debug/Carnivores2SDLTests --gtest_filter='SDLDisplayCatalog.*'
```

The real-X11 system-SDL teardown probe reproduces Phase 4b's report:
**100,032 bytes in 1,824 allocations**. An independent executable with only
two SDL video-init/quit cycles reproduces the same amount. This involves no
new refresh application, window, engine or catalog code. Dependency ownership
remains unresolved; it is not established as a lower-phase engine defect.
No suppression was used. The complete dummy-video sanitizer suite is clean.

### Native Linux runtime validation

Pinned SDL 3.2.28 ran the actual GL game with user-owned Genesis Redux content
under Gamescope 3.16.25 headless/X11 on the AMD GPU. Content was linked and
profiles copied into temporary directories outside the checkout; originals
were not modified. Debugger observations recorded the configured rational and
window mode on each entry; X11 geometry, rendered hunt screenshots, backend
logs and normal process exits supplied separate runtime evidence.

Nine scenarios passed:

- First launch with no config creates `refresh_rate 0`; an 800x600 windowed
  launch has the existing centered geometry and automatic runtime `0/0`.
- A windowed launch with a configured explicit refresh stays windowed, then
  Alt+Enter applies that preference and returns to windowed successfully.
- Borderless 800x600 with an explicit preference stays centered at 800x600
  and does not attempt refresh selection.
- Exclusive automatic at 1024x768 survives out/back Alt+Enter transitions.
- Exclusive explicit at the discovered unreduced **63500000/1059744 Hz**
  applies successfully at 1024x768, including re-entry through Alt+Enter.
- Unsupported **999/1 Hz** logs and takes automatic at the same 1024x768,
  including repeated exclusive entry. No refresh-driven resolution change.
- Config explicit plus CLI `-refresh=0` yields runtime `0/0`.
- Config `999` plus CLI `/refresh=63500000/1059744` applies the supported rate.
- Config `60.0` logs invalid syntax and launches with runtime `0/0`.

All nine game processes exited normally; `.sav`/`.sab` sizes remained
1,660/7,176 bytes and all 68 keybinding bytes matched the input profiles.
A rendered explicit-refresh hunt was visually inspected. Gamescope exposed
one timing per resolution, so this verifies an exact supported timing and
unavailable-request behavior, not switching between several physical timings
at one resolution.

Two additional native runs used the sanitized system-SDL build: supported
explicit refresh and unsupported-refresh fallback, each with out/back
Alt+Enter and normal close. Both exited **0** without ASan/UBSan/LSan
diagnostics. The compositor's virtual display behavior is not physical
monitor mode-switch acceptance, and this adds no direct-Wayland claim.

### Hosted CI and physical acceptance

The engineering checkpoint at `b0e38c5982f2b8349afeb73c2076e4206f7f8a3a`
passed all **12/12** jobs in
[Build and Test run 35318013948](https://github.com/willvdb/carnivores2-mee-modernized/actions/runs/35318013948):
native Win32 GL x86/x64 Debug/Release, SDL GL x86/x64 Debug/Release,
x86 SOFT Release, Menu Release, and Linux SDL Debug/Release. The final branch
report records the later branch-tip run after the additional argument-isolation
test and this documentation. CI has no proprietary content or physical refresh
acceptance requirement.

Phase 3's previously completed native Windows acceptance remains separate.
Before accepting the new optional refresh behavior on physical Windows, check:

- Both native and SDL Windows automatic startup against existing configs;
  windowed/borderless geometry, Alt+Enter, focus and desktop restoration.
- SDL exclusive application on a display exposing multiple timings at the
  same dimensions, including equivalent unreduced integers and a fractional
  timing if exposed; unsupported rates must warn and use automatic.
- Native Win32 exact integer and equivalent rational requests, rejection
  fallback through 32-bpp/16-bpp attempts, and fractional requests that warn
  without rounding; restoration on exit and Alt+Enter.
- A supported native/SDL timing rejected at application time, when a driver
  setup permits that check, to exercise the respective failure paths.

No newly established defect in accepted Phase 3/4a/4b behavior was found.
The previously reproduced SDL/X11 dependency leak remains separately tracked.
Display targeting is deferred: monitor selection/identity/persistence,
window-to-current-monitor policy, window movement and hotplug handling are
not implemented. DPI, direct-Wayland acceptance, VRR, swap interval, frame
limiting, launcher UI and other excluded work remain outside this milestone.
