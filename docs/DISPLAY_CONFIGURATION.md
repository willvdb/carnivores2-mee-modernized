# Display configuration, discovery and selection

The sections below record each milestone's boundary. Current opt-in persistence
and consolidated engine state are described in [Phase 4e](#phase-4e-opt-in-registered-display-identity)
and [Phase 4f](#phase-4f-one-engine-configuration-with-legacy-projections).
[Phase 4d](#phase-4d-explicit-runtime-display-targeting) defines the retained
session-targeting contract. Earlier primary-only descriptions are historical.

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
PreferredRefresh`, initially `0/0`. On explicit exclusive entry, the engine
selects an eligible primary-display mode and passes that owned value to
`ConfigureGameWindow`. Automatic/unsupported requests pass no explicit mode.
The preference is not an `OptRes` replacement and is not in any serialized
structure. Both first-launch and release-script config templates document
automatic as the default.

The Windows Menu does not own this key. Its existing `Menu/Resources.cpp`
`SaveConfig` list of fresh keys excludes `refresh_rate`; the merge writes
unowned lines verbatim in their existing positions. Source inspection and an
asset-free execution of the **unmodified SaveConfig function body**, with
small options/path test doubles, confirmed rational refresh lines and comments
survive two rewrites while an owned `display_mode` updates. No Menu code, UI,
merge algorithm or binary profile change was needed. This characterization
was not a physical Menu UI test.

### Exact selection policy

[RefreshSelection.h](../Hunt/Game/RefreshSelection.h) owns engine selection
policy and has no SDL, Win32, renderer or parsing dependency. Rational value
operations live in the portable `Platform.h` API, below that policy: known
rates have both components positive, and `Platform::EqualRefresh(a,b)` compares
`uint64(a.numerator) * b.denominator` with
`uint64(b.numerator) * a.denominator`. Every product of two uint32 values fits
in uint64. Unknown/invalid pairs never equal real rates (or each other).

Consequently `60/1`, `60000/1000`, `120000/2000` and `120/2` match, while
`60000/1001` does not match `60/1`. No discovered or preferred fraction is
normalized to enable comparison.

`FindRefreshMode` returns the first backend-order mode with exact width,
exact height, at least 16 bpp and an equal refresh value. Automatic and
unavailable requests return no selection. `SetVideoMode` queries the Phase 4b
catalog only for explicit exclusive entry, and `SelectPrimaryRefreshMode`
calls `FindRefreshMode` on the primary display's raw modes. A missing/out-of-range
primary, missing match or low-color-only match logs and uses automatic; neither
secondary displays nor desktop/current metadata substitute for fullscreen modes.
Automatic, windowed and borderless paths skip this new catalog query entirely.

The engine passes an optional owned `DisplayMode`, including its selected depth,
through the portable API. Carrying depth prevents native remapping from selecting
a low-color variant that the engine rejected. Backends do not import game policy:
SDL maps these portable fields back to a native mode, while Win32 expresses the
selected rate with DEVMODE. There is no sorting, nearest-rate search, resolution
substitution or highest/lowest/desktop/current refresh preference. The
primary/default display remains authoritative; no monitor identity or index is
configured or persisted.

### SDL backend

For automatic refresh, the existing call remains
`SDL_GetClosestFullscreenDisplayMode(primary, width, height, 0, false, ...)`,
followed by the unchanged exact-width/height check and mode application.
No new enumeration or policy chooses a refresh on this path.

For a caller-selected mode, the backend enumerates the primary display's native
fullscreen modes and maps the exact selected dimensions, depth and rational
rate value, preserving first-match native order. This mapping has no minimum-bpp
or game eligibility policy. It passes that original native mode to
`SDL_SetWindowFullscreenMode` and then enters fullscreen. The engine logs
unsupported preferences before passing automatic. If a selected native mode
disappears between discovery and application, the backend also logs and executes
the original automatic exact-WxH path. A found mode whose application fails
retains the existing desktop-popup exclusive failure behavior. It does not
attempt a merely nearby refresh. The preference remains configured for the next
exclusive entry, including Alt+Enter.

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

`Platform::IntegerRefreshHz` permits only an exactly divisible positive rational. For
example `60000/1000` is exactly 60 Hz; `60000/1001` cannot be expressed and is
never rounded. The native catalog exposes integer rates, so fractional
preferences normally fail the engine eligibility check and use automatic with
a warning. The backend also retains its exact-conversion guard and warning for
nonrepresentable values passed directly to the platform API.
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

### Review correction: engine policy above platform mechanisms

Review of `f3919482a8f2aa60b239085df8bd963167e3bf7d` identified an architectural
issue: the platform backends imported `Game/RefreshSelection.h` and owned the
call to the engine eligibility predicate. The correction moves eligibility to
`SetVideoMode` -> `SelectPrimaryRefreshMode` -> `FindRefreshMode`, using the
owned Phase 4b catalog. Backend-only rational operations now live in `Platform`;
neither backend imports `Game/...` or calls `GameDisplay` policy.

The optional selected `DisplayMode` carries color depth as well as refresh so
SDL can map exact native fields without repeating the engine's >=16-bpp rule.
If that mode disappears, the backend still takes automatic; if application of
a found mode fails, the existing popup path remains. Native Win32 retains exact
integer conversion, 32/16-bpp attempts and automatic retry. Unsupported-request
logging now normally occurs in the engine. The original preference is retained
for later exclusive entry. Parsing, config/CLI precedence and serialization are
unchanged. The automatic blocks and nonexclusive backend branches were also
compared against the reviewed tip and remain unchanged.

Regression validation for this correction:

- GCC Debug/Release with pinned SDL and Clang/GCC-sanitized builds with system
  SDL each pass **286 GoogleTest cases in 12 executables**, plus the new source
  boundary check: **13/13 CTest checks**. GCC/Clang standalone portable tests
  pass **37/37** each with `-Wall -Wextra -Werror` and ASan/UBSan/LSan.
- The boundary check scans every platform `.cpp` and header, including inactive
  backends, for game-policy includes. A negative probe with the original include
  fails as intended. New tests exercise primary-only catalog selection, missing
  primary/metadata, owned selected values, caller-selected native depth, and a
  mode disappearing before native remapping. SDL remapping deliberately has no
  minimum-color policy; only the engine imposes that requirement.
- Both x64 Windows GL variants build with local MSVC under Wine. Native Win32
  policy tests pass **40/40** plus the boundary check. SDL tests pass **26/27**;
  the existing French keyboard-layout test fails under this Wine prefix. An
  untouched earlier SDL Release test binary also fails that same test (**20/21**),
  including the same A/Q and AltGr expectations. No input change or suppression
  is included; hosted Windows CI remains the Windows regression authority.
- The same nine native Linux/X11 runtime scenarios pass, followed by two clean
  sanitized supported/unsupported-refresh hunts. Debugger probes confirm **zero
  catalog queries** for automatic, windowed and borderless paths, and no explicit
  mode reaches the backend for unsupported refresh. Alt+Enter reselects supported
  modes on exclusive entry, and every process exits normally.

Correction logs and runtime evidence are in `/tmp/carnivores-refresh-layering/`.
Physical-Windows refresh acceptance remains separate from Wine and hosted CI;
the checks listed above still apply. The prior standalone SDL/X11 teardown
leak record is unchanged and no suppression has been introduced.

## Phase 4d: explicit runtime display targeting

Branch: `port/display-targeting`. Exact base:
`ba556538ac97cfe8ec3035bed46740ce70d13737` (integrated Phase 4c).
Main was verified against that SHA before starting and again when resuming.
This milestone does not merge or rewrite main.

### Command line and deliberately ephemeral identity

```text
-display=0
-display=1
/display=2
-display=1 -fullscreen -res=800x600 -refresh=120
```

`-display=N` and `/display=N` select the zero-based entry in the current owned
`DisplayCatalog::displays` snapshot. The option name is case-insensitive.
The value is decimal digits only, with checked uint32 overflow; leading zeroes
are accepted. Negative/plus signs, empty values, floats, whitespace, fractions,
and trailing text are rejected. Repeated options use the **last occurrence**.
A malformed last occurrence clears any earlier display request, warns, and
uses primary/default placement. Recognized malformed options are consumed
before legacy substring-based hunt/session argument processing.

An out-of-range index or selected display without positive usable bounds also
warns and uses primary/default placement. Duplicate full bounds are ambiguous
and use primary/default with automatic refresh. Neither syntactic nor catalog
validation changes the requested resolution or aborts startup. Omission retains
the existing primary behavior; index zero is not assumed to be primary.

**These indices are ephemeral, not persistent monitor identities.** Enumeration
order can change between launches, reboots, reconnects, driver changes, topology
changes, and even successive snapshots within one session. The runtime request
is re-evaluated against a fresh snapshot when applying a game video mode. It
does not promise to follow the same physical monitor after a topology change.

There is no display-selector config key, profile field, remembered monitor, or
remembered window position. No coordinates, SDL IDs, HMONITORs, device names,
enumeration numbers, EDID, or monitor serial numbers are serialized. The existing
`resolution`, `display_mode`, and `refresh_rate` keys retain their behavior.
Persistent monitor selection needs a separate identity and fallback design in
a later milestone; serializing this runtime index would not provide one.

### Engine selection and owned portable values

The parsing state is only `std::optional<std::uint32_t> RequestedDisplayIndex`.
After platform initialization, `SetVideoMode` queries **one owned catalog per
operation** when there is a display request or an explicit exclusive-refresh
preference. [DisplaySelection.h](../Hunt/Game/DisplaySelection.h) resolves the
effective display and eligible refresh mode together from that snapshot.

Default/fallback selection uses `primaryDisplay` only when it references a valid
entry. An explicit entry is usable only when its positive bounds match
**exactly one display in that same snapshot**. Compare the complete rectangle:
signed X/Y origin, width, and height. Equal dimensions at different origins are
distinct targets. A unique explicit entry can be selected even if the catalog
has no primary metadata. Otherwise missing or invalid primary metadata passes
no target or selected mode, retaining the backend's established safe default path.

Two or more matches produce `AmbiguousBounds`: keep the valid primary index if
available, clear the portable target and selected exclusive mode, and skip
refresh selection entirely. Even a primary that supports the requested explicit
refresh uses automatic. The original session index and refresh preference remain
available for the next video-mode application. With no explicit display request,
primary bounds need not be unique and normal primary refresh selection is unchanged.

The portable API receives owned values:

```cpp
struct DisplayTarget { DisplayBounds bounds; };
void ConfigureGameWindow(WindowMode mode, Size size, Point videoCenter,
    std::optional<DisplayMode> exclusiveMode = std::nullopt,
    std::optional<DisplayTarget> target = std::nullopt);
```

The engine copies the selected entry's bounds and optional eligible mode before
the snapshot goes away. Its index is used only for selection and diagnostics;
it never crosses this API. Bounds preserve signed origins and the complete
rectangle. Each backend independently requires exactly one matching rectangle
in its current native topology, with no normalization or assumption that primary
starts at `(0,0)`. Zero matches mean disappearance; two or more mean ambiguity.
Both fall back to primary/default with automatic refresh. This second uniqueness
check rejects a target that becomes duplicated after the engine's snapshot.
SDL IDs, HMONITORs, DEVMODEs, device-name pointers, HWNDs, and other native
handles remain private to backend implementation. Platform sources do not
include Game policy; the Phase 4c source-boundary CTest remains enabled.

No stateful display manager was introduced. With no override and automatic
refresh, no new catalog query or target-remapping operation occurs. Primary
window centering, automatic exclusive selection, and loading-window behavior
retain their existing paths. An explicit primary refresh still uses primary.
The loading splash is not moved to a secondary: the target takes effect at
game video-mode application, without startup reordering.

### Resolution and refresh compatibility

`ResolutionList[128]` still comes from the primary/default compatibility
projection, **even when presentation targets a secondary display**. The saved
`OptRes` ordinal and the Windows Menu's resolution ordering must retain their
existing interpretation. A session-only display choice changes presentation
location, not the serialized list identity. Use `-res=WxH` for an explicit size.
An unavailable exclusive size uses the selected backend/display's existing
fallback; it does not rebuild the legacy list from that display's modes.

Refresh eligibility remains engine policy: exact width and height, at least
16 bpp, exact rational refresh **value**, and first eligible backend-order mode.
Lookup uses the effective presentation display, including primary after an
out-of-range or missing-bounds fallback. Ambiguity bypasses lookup and always
uses automatic refresh. A primary-only timing is never borrowed for a secondary.
Unsupported refresh passes automatic on the same target; there is no
nearest/highest-rate selection. A disappeared or ambiguous target discards its
selected mode before primary automatic fallback.

Alt+Enter retains `RequestedDisplayIndex` and `PreferredRefresh`. Each entry
resolves a new owned snapshot; each exclusive re-entry re-evaluates the eligible
target-specific refresh. Secondary exclusive -> windowed -> exclusive therefore
keeps the requested target when the topology remains unchanged.

### SDL window and fullscreen mechanisms

Explicit windowed presentation centers the existing decorated outer rectangle
within target bounds while retaining the requested **client** size, including
negative desktop coordinates. Borderless preserves the existing per-axis rule:
a positive requested dimension that fits is retained; otherwise that dimension
uses the target desktop dimension. The resulting window is centered there.
Borderless is not redefined to always fill the monitor.

For exclusive mode, bounds are remapped before leaving the previous fullscreen
state, since the snapshot may describe that state's changed dimensions. After
restoration, the backend refreshes the bounds using the resolved native ID.
It moves the window while windowed, synchronizes that move, sets the native
fullscreen mode, and enters fullscreen. This ordering was checked against the
checksum-pinned SDL **3.2.28**, including
[`SDL_video.c`](https://github.com/libsdl-org/SDL/blob/release-3.2.28/src/video/SDL_video.c)
and [`SDL_x11window.c`](https://github.com/libsdl-org/SDL/blob/release-3.2.28/src/video/x11/SDL_x11window.c).
The selected mode's display ID determines exclusive targeting; window position
does not choose the user's target.

Automatic lookup calls the target's `SDL_GetClosestFullscreenDisplayMode` with
refresh **0**, then requires exact dimensions and the same native display ID.
Explicit-mode remapping enumerates only that target's modes and matches the
caller-selected dimensions, depth, and rational rate. A missing native timing
uses automatic on the same display. An unavailable exact resolution or failed
native application retains the desktop-popup fallback on the intended target.
No merely close resolution is accepted.

If bounds match zero or multiple native displays, `MapWindowDisplay` returns
primary with both target and explicit mode cleared. The backend distinguishes
"disappeared" from "matches multiple displays" in its warning,
completes the old fullscreen transition, and centers on primary before automatic
application. A target disappearing later in the operation gets one primary
automatic attempt. It never substitutes an arbitrary secondary. This handles
the discovery/application race; no ongoing hotplug or drag-to-monitor policy
is added.

### Native Win32 targeting and restoration

The backend requires exactly one full `MONITORINFOEXA::rcMonitor` rectangle
match and owns a copy of that device name internally. `MapDisplayTarget` returns
no device for zero or multiple matches. The latter logs "matches multiple
displays" rather than disappearance; both clear explicit refresh and use the
established primary/default path. Neither matching secondary receives the selected
mode. Previously changed devices are still restored before another application,
including this fallback. Windowed placement retains
`AdjustWindowRect` client-size calculation; borderless retains the same per-axis
sizing rule. Both center within the selected rectangle, including negative
origins, instead of using primary `SM_CXSCREEN`/`SM_CYSCREEN` dimensions.

An explicit mapped exclusive target uses `ChangeDisplaySettingsExA` with that
device name. No explicit target retains the original `ChangeDisplaySettingsA`
default-device path. The 32-bpp then 16-bpp attempt order, exact integer-refresh
conversion, and automatic retry after refresh rejection remain. Fractional
rates are never rounded. Failure to apply a mode retains the existing popup
failure path; it does not move to another available secondary.

The backend records an owned device name only after a successful targeted mode
change. Leaving exclusive and shutdown restore **that device** using null
DEVMODE and flags zero, consistent with the
[ChangeDisplaySettingsExA contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-changedisplaysettingsexa).
It clears the record after successful restoration, retains it after failure for
a shutdown retry, and clears a stale record if the device is no longer attached.
A failed restore of an attached device stops another mode application rather
than losing its restoration record. No native state is exposed or persisted.

### Failure behavior and diagnostics

| Situation | Effective behavior |
| --- | --- |
| No display override | Existing primary/default path. |
| Unique valid explicit secondary | Presentation and refresh lookup use that entry. |
| Malformed argument | Consumed; warning; prior request cleared; primary fallback. |
| Out-of-range index | Warning; primary fallback; resolution unchanged. |
| No primary metadata | Existing backend default fallback, unless an explicit unique usable entry was selected. |
| Selected entry lacks usable bounds | Warning; primary fallback. |
| Duplicate full bounds in engine snapshot or native remapping | Ambiguity warning; primary/default fallback; clear target and selected mode; automatic refresh even if primary supports the request. Never choose the first matching secondary. |
| Target disappears before application | Warning; primary fallback; discard selected refresh and use automatic. |
| Exact exclusive size unavailable | Existing desktop-popup fallback on target where feasible. |
| Dimensions supported, requested refresh unavailable | Automatic refresh on target. |
| Native mode application fails | Existing failure path; no arbitrary display substitution. |

Explicit requests log the requested runtime index, effective primary/secondary
catalog index, selected bounds, and any fallback reason. Normal launches do not
gain full catalog dumps or additional display-selection logging.

### Validation and dependency disposition — 18 September 2026

Parser tests cover zero, one, large valid integers, overflow, malformed values,
case-insensitive slash/dash forms, last-wins semantics, malformed-option
isolation, and untouched unrelated arguments without physical monitor counts.
Synthetic engine catalogs cover nonzero primary indices, missing primary/bounds,
negative origins, owned results after catalog destruction, target-only refresh,
low-color rejection, and a timing supported only by primary. The ambiguity
correction adds two identical secondary rectangles with a distinct primary:
selecting either secondary clears target and mode, even with the requested rate
available on primary. Missing/invalid primary metadata keeps backend-default
fallback. Duplicate primary bounds reject explicit targeting but leave the
no-override path unchanged. Changing any rectangle component restores uniqueness,
and the retained session request can succeed on a later snapshot.

SDL helpers test full-rectangle matching, missing targets, target-scoped native
mode mapping, mode disappearance without monitor substitution, primary fallback,
and automatic refresh zero with exact size enforcement. Win32 pure helpers test
rectangle matching, device-name ownership, successful/failed targeted restore
bookkeeping, and the original default path. Hosted tests need no physical second
monitor. The dependency regression additionally uses disposable virtual X11
outputs in Linux CI.

Both native-remapping regressions first select a unique negative-origin target
through the production engine helper, then duplicate its bounds in synthetic
native topology. SDL returns primary with target and mode cleared; Win32 returns
no device. Swapping the matching secondaries' enumeration order cannot change
the result. Existing exact-match, zero-match, different-origin/equal-size, and
restoration tests remain; SDL also preserves normal primary refresh in a duplicate
topology when no target was requested. These are production-helper tests, not
physical monitor-count assumptions or physical hotplug acceptance.

| Earlier follow-up validation at `a3bcb033` | Result |
| --- | --- |
| GCC Debug/Release, patched bundled SDL 3.2.28 | Both game builds; 302 GoogleTests plus boundary guard pass (13 CTest checks); opt-in X11 regression skipped in ordinary CTest. |
| Clang, patched bundled SDL, `-Wall -Wextra` | Game build and the same 13 checks pass; opt-in regression skipped. |
| GCC/Clang portable tests, `-Wall -Wextra -Werror`, ASan/UBSan/LSan | 49/49 each, leak detection enabled. |
| SDL backend translation unit, GCC/Clang | `-Wall -Wextra -Werror` passes. |
| Fully instrumented bundled SDL + engine | Build passes; full CTest has 12 passes, one known dummy-driver UBSan failure, and one opt-in skip. |
| Virtual X11 full game matrix | 13 scenarios, normal exits, expected placement/refresh, Alt+Enter, and restored desktop modes. |
| Sanitized secondary supported/unsupported refresh hunts | Both exit 0 after Alt+Enter out/back, with ASan/UBSan/LSan enabled. |

Duplicate-bounds review correction: Linux GCC Debug/Release and Clang builds
pass **308 GoogleTests** plus the boundary guard (13 CTest passes and the opt-in
X11 skip). GCC/Clang portable strict-warning sanitizer tests pass **53/53** each;
both compilers also accept the SDL backend with `-Wall -Wextra -Werror`.
MSVC x64 under Wine passes **60/60** platform tests, including the Win32 ambiguity
regression, plus the boundary guard. The fully instrumented bundled SDL/engine
build still has the same one dummy-driver UBSan failure, 12 CTest passes, and one
opt-in skip; its six synthetic SDL target-remapping tests pass independently with
ASan/UBSan/LSan. The existing X11 mode-leak/restoration regression also passes
with sanitizers: 20 fullscreen/windowed cycles on each virtual display, zero
outstanding SDL allocations, and both desktop modes restored.
These results do not claim a clean full sanitizer suite or
physical Windows acceptance. Final-SHA hosted matrix results are recorded in the
branch report. Correction logs are under `/tmp/carnivores-display-ambiguity/`.

All golden serialization fixtures remain unchanged. `.sav` is **1,660 bytes**,
`.sab` **7,176 bytes**, `OptRes` remains the same signed ordinal, and all **68
keybinding bytes** remain unchanged in runtime profile checks. No extension
block, format version, persistent display selector, Menu code, Menu UI, monitor
dropdown, or refresh dropdown is added.

Work paused when the separate SDL-only fullscreen leak was isolated. The user
authorized its separate fix, then resumed Phase 4d. The reviewed dependency
commit from `fix/sdl-x11-mode-leak` is included as a distinct cherry-pick, with
the source version/checksum unchanged. See [SDL_X11_MODE_LEAK.md](SDL_X11_MODE_LEAK.md):
32 bytes/four allocations after two fullscreen cycles became zero after 20
cycles per virtual display. `CARNIVORES_SYSTEM_SDL3=ON` does not patch installed
SDL and can still expose the unfixed dependency behavior.

Two separately reproduced dependency issues remain documented, without
suppression: the X11 initialization/teardown leak (50,016 bytes/912 allocations
per observed cycle), and SDL 3.2.28's zero-mode dummy-driver null-source,
zero-length memcpy UBSan error. The latter reproduces in unmodified SDL alone
and prevents claiming an entirely clean fully instrumented CTest suite.
No further lower-phase engine defect was discovered in this follow-up.
Unrelated legacy warnings were neither fixed nor suppressed.

### Physical acceptance limits and deferred work

Earlier Phase 4d validation on Allosaurus's real three-display Hyprland/Xwayland
session exercised primary placement, secondary windowed/borderless placement,
oversize borderless fallback, and invalid/repeated arguments. All exposed origins
were nonnegative; negative-origin placement remains synthetically tested only.
Physical exclusive targeting is **not accepted** in that session: an independent
SDL-only program also requested a secondary but presented on primary after a
failed SDL synchronization. This is recorded as an SDL/compositor limitation.
No direct-Wayland acceptance is claimed.

The controlled native Linux Xorg dummy/Openbox setup exposes primary 1920x1080
at `(0,0)` and secondary 1280x1024 at `(1920,0)`. Full hunts exercise secondary
800x600 exclusive with automatic, supported **40000000/663168** rational refresh,
and unsupported **999/1** falling to automatic on that secondary; unsupported
777x555 uses its desktop popup. Alt+Enter retains the target and refresh request.
Injected stale-mode and stale-bounds probes exercise same-target automatic and
primary-automatic fallback respectively, with desktop restoration. These are
controlled race probes, not physical cable hotplug acceptance.

Hosted Windows CI covers native GL and SDL GL x86/x64 Debug/Release, x86 SOFT,
and Menu; Linux CI covers Debug/Release and the virtual mode-leak/restoration
test. The final branch report records the exact tip and hosted run. Hosted CI
does **not** establish physical Windows multi-monitor acceptance. Still check:

- Native Win32 and SDL Windows windowed/borderless on secondary monitors,
  including negative coordinates and a primary not at catalog index zero.
- Supported size/refresh, unsupported refresh remaining on target, unavailable
  size using target popup, and native application rejection.
- Alt+Enter, focus loss/re-entry, normal shutdown, and independent restoration
  of a changed secondary. Native fractional refresh must never be rounded.
- Target disappearance between selection/application and named-device restore
  failure/retry where the physical setup permits them.

Persistent identity/fallback design, monitor-selection UI, remembered monitor
or position, hotplug handling, automatic window-follow-monitor policy, DPI,
direct-Wayland policy, always-full-monitor borderless, VRR, vsync, frame limiting,
and the other excluded subsystems remain deferred. Runtime evidence stays under
`/tmp/carnivores-display-targeting/` and `/tmp/carnivores-display-targeting-followup/`;
no proprietary assets or copied profiles are committed.

## Phase 4e: opt-in registered display identity

The owned `Platform::DisplayIdentity` contains a schema version, identity domain,
and opaque value. It describes an **OS device registration**, not a physical
monitor serial number. It never contains an HMONITOR, SDL ID, runtime index,
position, display label, or mode. Platform discovers facts;
`Game/MonitorPreference.h` parses preferences and `SelectMonitor` decides policy.
Backend placement still uses the Phase 4d exactly-one full-rectangle mapping.

### Evidence and supported capability

The checksum-pinned [SDL 3.2.28 public display API](https://github.com/libsdl-org/SDL/blob/release-3.2.28/include/SDL3/SDL_video.h)
has HDR and KMSDRM orientation properties, but **no public HMONITOR, wl_output,
EDID, or persistent display identifier**. Newer online SDL documentation lists
properties not present in this pin; the implementation does not depend on them.
Its [Windows implementation](https://github.com/libsdl-org/SDL/blob/release-3.2.28/src/video/windows/SDL_windowsmodes.c)
stores HMONITOR/device names privately and obtains friendly display labels.

Windows native and SDL's actual `windows` driver support domain
`win-monitor-interface`, version 1. Native discovery calls
[EnumDisplayDevicesW with EDD_GET_DEVICE_INTERFACE_NAME](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdisplaydevicesw),
which returns the registered GUID_DEVINTERFACE_MONITOR interface in DeviceID.
Only exactly one active monitor child with a nonempty, bounded path is accepted.
The lowercase Windows path is encoded as four lowercase hexadecimal digits per
UTF-16 code unit; the path remains opaque and is never disassembled. Windows
[registers interfaces across reboots](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-ioregisterdeviceinterface).
That guarantee concerns the registration, not survival of driver reinstall,
OS reinstall, changed connector/dock/GPU, or a replacement physical panel.

Both Windows backends associate those native registrations to the owned catalog
only when the full rectangle occurs exactly once in **each** enumeration.
Missing, duplicated, truncated, clone, or failed native metadata stays absent.
Failure to read any native monitor rectangle aborts identity association for
the entire snapshot: incomplete bounds cannot establish uniqueness.
SDL dummy/offscreen drivers never borrow real Windows monitor identities even
when rectangles happen to match. No identity is invented from bounds.

Linux persistence is explicitly **unsupported in this implementation**; Linux
retains session `-display=N`. The pinned [X11 implementation](https://github.com/libsdl-org/SDL/blob/release-3.2.28/src/video/x11/SDL_x11modes.c)
keeps its RandR output/connector private and constructs labels using EDID. A
label is not a unique serial. Native RandR output IDs are server resources;
EDID may omit serials or be duplicated, and mapping DRM sysfs to SDL/Xwayland
outputs is not a portable identity contract. Wayland's
[wl_output.name contract](https://wayland.freedesktop.org/docs/html/apa.html#protocol-spec-wl_output-event-name)
explicitly does not promise persistence across sessions. No new private SDL ABI,
compositor extension, DRM access, EDID parser, or physical-panel identity claim
is introduced. This is completed partial-platform persistence, not universal
monitor identity support.

### Discovery, consent and precedence

Run the game executable with `-list-displays` (or `/list-displays`). It initializes
the platform and writes current indices, bounds, primary status, and complete
copyable `display_identity` lines to stdout and `render.log`, then exits **before**
creating a game window, loading assets/config/profiles, or saving settings. On
Windows GUI launches, use `render.log`. No monitor mode is changed. Displays
without an identifier say persistence is unavailable; use their session index.

To opt in, copy the desired complete line into the existing `config.cfg`:

```text
# Example shape only: use the complete token printed on your own machine.
display_identity v1:win-monitor-interface:00610062
```

The example's opaque value is synthetic and will not select a real monitor.
No configuration writer automatically records the last selected monitor.
Absent key and `display_identity primary` retain the primary/default behavior.
The legacy Menu does not own this key: its existing writer preserves these
lines, unknown keys/versions and comments verbatim through repeated rewrites.
The new asset-free test compiles that actual production writer body with small
state/path doubles, without modifying Menu or frontend sources.

The order is defaults/profile -> config -> final CLI pass. Config duplicates
are last-wins. Values accept outer whitespace and `#` comments, but no extra
tokens. Identity syntax is `v<positive uint32>:<domain>:<opaque lowercase hex>`;
domain is 1–48 lowercase letters/digits/hyphens, value is 2–2048 hex digits of
even length. Unknown versions/domains remain valid saved intent but cannot
resolve. Empty/malformed/oversized values reset effective preference to primary
and log the error; config is never rewritten by the engine.

Session overrides (dash/slash, case-insensitive option name) are:

- `-display=N`: existing uint32 session index, never persisted.
- `-display=primary`: ignore any saved preference for this session.
- `-display-id=<token>`: use another identifier for this session; never persisted.
  `-display-id=primary` also clears the effective preference.

The last recognized display option wins, including invalid options clearing an
earlier request to primary. Malformed values are consumed before legacy
substring-based session parsing. Explicit primary does not suppress an
independent refresh preference; use `-refresh=0` to request automatic refresh.
Existing out-of-range session-index behavior is unchanged (primary refresh
lookup is permitted); **failed saved-identity resolution always uses automatic**.

### Resolution and fallback contract

| Situation | Result |
| --- | --- |
| Same registration, reordered catalog or moved/rearranged monitor | Match identity, use its current unique rectangle and its own modes. |
| Disconnect/missing registration/changed connector path | Primary/default plus automatic refresh; preserve saved intent. |
| Registration returns on a later launch or mode application | Resolve again; saved preference recovers. No hotplug subscription added. |
| Duplicate identifiers, including identical monitors reporting the same value | Reject ambiguity; primary/default plus automatic. Never take first match. |
| Clones or duplicate rectangles in either discovery layer | Identity unavailable or targeting rejected; primary/default plus automatic. |
| No serial / identical panel models | No physical-serial claim. Distinct OS registrations may work; ambiguous registrations do not. |
| Backend changes between native Windows and SDL Windows on same installation | Same identity domain; may resolve if registration and unique mapping remain. |
| Linux/another OS, unsupported domain/version, dummy backend | Retain config line; primary/default plus automatic. Session index remains available. |
| Registration changes after docking, GPU/driver/OS changes | No heuristic relocation; rediscover and explicitly update config if desired. |
| Target disappears during backend mapping | Existing 4d primary/automatic fallback. |

A saved target resolves only on an exact version/domain/value match and exactly
one catalog entry. Missing bounds also clear selected refresh; a primary-only
timing can never leak from a failed saved target. The preference itself remains
unchanged. A valid target uses target-specific refresh selection and keeps 4d
loading-window, Alt+Enter, owned lifetimes and named-device restoration behavior.
There is no atomic hotplug transaction: exactly-one current rectangle remapping
is the existing race boundary, not a promise of persistent identity validation
inside native fullscreen APIs.

No legacy profile, save, trophy, keybinding, OptRes or Menu binary layout changes.
The 1,660-byte `.sav`, 7,176-byte `.sab`, signed OptRes and 68 keybinding bytes
remain covered by existing golden tests. Physical Windows multi-monitor and
Hyprland/Xwayland exclusive acceptance remain outstanding; CI/synthetic tests
do not resolve those earlier limits.

## Phase 4f: one engine configuration with legacy projections

`Game/DisplayConfiguration.h` defines `GameDisplay::Configuration`: dimensions,
`Platform::WindowMode`, monitor preference, and rational refresh. The engine's
`DisplayConfiguration` object owns these values. Startup adapters and parsing
write this object; `SetVideoMode` selects presentation/refresh from it and passes
only owned portable values into Platform. No Platform source imports Game policy.

### Characterized behavior retained

Before consolidation, the only writers of WinW/WinH were profile resolution
translation, config/CLI dimensions, `SetVideoMode`, and valid actual-client-size
synchronization. FULLSCREEN/BORDERLESS were written by defaults, config/CLI mode
selection, and Alt+Enter. These writes now converge through
`SyncLegacyDisplayState` / `ProjectLegacyPresentation`. Renderer, SOFT, native
message consumers and buffer geometry still read their familiar globals; those
globals are compatibility projections, not separate preference authorities.
Derived VideoCX/VideoCY, pitch, viewport, FOV and DIB allocation stay unchanged.

The startup sequence is preserved:

1. Explicit defaults: unspecified `{0,0}` dimensions, exclusive mode, primary
   monitor, automatic refresh. `{0,0}` matches the old zero-initialized globals,
   not an added new request. The missing/invalid-profile path does not call
   SetupRes; this refactor deliberately does not invent an 800x600 first-run
   setting. The existing backend fallback and default config behavior remain.
2. The early CLI pass still supplies legacy session options.
3. Enumerate the primary-based legacy ResolutionList, load a valid profile, and
   adapt OptRes through `ApplyLegacyProfileResolution`. This changes dimensions
   and OptRes only. Invalid ordinals still use 800x600/first-mode; an empty list
   still uses 800x600 and leaves OptRes unchanged.
4. Apply config, including resolution, display_mode, display_identity and
   refresh_rate. The automatically created legacy config still chooses
   borderless mode; no default/template behavior is changed.
5. Run the final CLI pass, which wins over config. Publish compatibility values
   before creating the game-sized DIB and entering gameplay.

`ApplyConfigResolution` sets OptRes to the exact primary-list match or -1, and
leaves CurRes alone. `ApplyCommandLineResolution` sets both ordinals only when
an exact match exists; otherwise both prior ordinals survive. Both update
dimensions. The parsers, accepted legacy `/vmodeN` flags and unmatched-resolution
difference are unchanged. ResolutionList's Windows ordering shim remains solely
for profile/Menu compatibility. There is no replacement binary profile record.

Window mode has one enum instead of two independently authoritative flags.
Config `display_mode 2` still maps to exclusive on SOFT; existing CLI borderless
behavior is retained separately. Alt+Enter maps borderless -> exclusive,
exclusive -> windowed, windowed -> exclusive. It keeps monitor/refresh intent.
On a valid backend client-size adjustment, the configuration and compatibility
globals both follow the actual dimensions, so later Alt+Enter and restart
applications keep using the same actual-size behavior as before. Invalid client
sizes do not replace dimensions. Loading artwork/window sizing never writes
the gameplay configuration. Desktop restore order and backend calls are unchanged.

### Requirement-to-implementation coverage

| Requirement | Production seam and regression coverage |
| --- | --- |
| Owned version/domain identity and native uniqueness | Platform identity values, Windows discovery and `DisplayIdentityDetails::Associate`; portable mapping plus injected Windows enumeration tests. |
| Opt-in config, CLI override, malformed isolation | MonitorPreference parsers and production config/CLI call sites; persistence parser/precedence tests and full-game launch traces. |
| Missing/duplicate/reordered/moved/reconnected targets | `SelectMonitor`; synthetic identity/rectangle/recovery cases, retained preference assertions, primary-refresh-leak mutation regression. |
| Preserve modern and unknown config lines | Asset-free build of the actual Menu SaveConfig body; two rewrites preserve identity, rational refresh, comments and unknown version/key. |
| Dimensions/mode/monitor/refresh consolidated | Configuration and its production startup/application callers; profile/config/CLI composition tests. |
| Legacy ordinal and SOFT adapter semantics | ApplyLegacyProfileResolution, ApplyConfigResolution, ApplyCommandLineResolution, ApplyConfigWindowMode and projection tests. |
| Alt+Enter, actual client size, loading/restoration | ToggleWindowMode/ApplyClientSize tests; virtual-X11 full-game scenarios and existing 20-cycle allocation/restoration regression. |
| Save/trophy/keybinding ABI | Unchanged serializers and golden .sav/.sab/layout tests; copied runtime profiles retain 1,660/7,176-byte sizes and all 68 keybinding bytes. |
| Game above Platform | Existing executable source-boundary check remains in every CTest build. |

The checkpoint records exact SHA/CI evidence and limitations. Physical Windows
and Hyprland/Xwayland exclusive acceptance remain outstanding. The existing
SDL dummy-driver zero-length memcpy UBSan failure and independent real-X11
initialization/teardown leak remain unsuppressed dependency limitations.
