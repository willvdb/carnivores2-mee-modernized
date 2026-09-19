# Phase 4g checkpoint

## Review state

Implementation and first review correction complete; new exact-head CI and
supervisor re-review are the next gates. No 4h work has started.

- Branch: `port/linux-display-behavior`.
- Worktree: `/home/willvdb/code/games/carnivores2-phase4-linux-display`.
- Accepted unmerged 4f base: `14563c9f9570b94a8dcd77464fa86ab4ce860fa6`.
- Main verified before work: `a2cfec8ef3590c6c8d56d16c7a69e4c0e5327f5a`.
- Initial implementation head: `7804380f709b27c44e795cbac2b4de32cbfe22c4`.
- Initial review head: `fffdea57eaf1c0936142a296913f82043dbf109b`; all 12 CI jobs
  passed: https://github.com/willvdb/carnivores2-mee-modernized/actions/runs/35413390812.
- The narrow supervisor correction below is included in this subsequent commit.
  Its final pushed SHA and exact-head CI results are supplied in the handoff;
  the previous green run is not acceptance for this new code.

Ordered implementation commits:

1. `1f18a48297371e5f873ef5102172ddc93dcc3e1d`: Wayland relative mouse-look,
   retaining the legacy Windows/X11 integer delta and game sensitivity policy.
2. `312f6a5859e665d47aa86a65405ea24c9c061b17`: isolated checksum-guarded SDL
   3.2.28 dead-connection window-wait correction, with socket fault reproducer.
3. `7804380f709b27c44e795cbac2b4de32cbfe22c4`: Linux fullscreen confirmation,
   compositor-aware fallback, actual-state diagnostics, production Wayland
   runtime regressions in existing Linux CI jobs, tests and documentation.

Other checkouts, original profiles/assets and host configuration are untouched.
No merge, history rewrite, persistent display identity, hotplug or mixed-DPI work.
Read repository AGENTS.md, PORTING.md, Linux/display docs and accepted checkpoint.
Current behavior and acceptance matrix: `docs/LINUX_DISPLAY_BEHAVIOR.md`.
Dependency correction: `docs/SDL_WAYLAND_DISCONNECT.md`.

## Local validation

All evidence below is under `/tmp/carnivores-phase4g/`; it is deliberately outside
Git and includes user-owned asset screenshots. Assets were referenced by symlink;
profiles were copied. Builds use copied dependencies, not another checkout's
mutable dependency/build tree.

- GCC Debug/Release and Clang full builds: 14 active CTest checks pass, three
  opt-in runtime checks skip (17 total). Logs `final-{debug,release,clang}-*.log`.
- GCC/Clang strict SDL translation-unit compilation passes (`strict-*.log`).
- Portable strict ASan/UBSan/LSan tests: 73/73 for each compiler
  (`portable-*-{build,tests}.log`).
- Fully instrumented build: 13 active CTest checks pass, three skip; the known
  pinned SDL dummy-video null-source zero-length memcpy at SDL_video.c:1341
  still fails unsuppressed (`final-sanitized-tests.log`). All ten new SDL
  input/fullscreen tests pass separately (`final-sanitized-new-tests.log`).
- Production Wayland asset-free regression: loading GL presentation, two
  fullscreen/windowed cycles on each output, unsupported-size target desktop
  fallback and restoration pass. Its separate client-socket disconnect test
  passes. `wayland-script.log`; durable runner is
  `tests/wayland/run_presentation_test.sh` (ordinary CTest opts out).
- X11 fullscreen allocation test: 20 cycles on each of two virtual outputs,
  zero outstanding SDL allocations, both desktop modes restored with full
  ASan/UBSan/LSan instrumentation (`x11-allocation-sanitized.log`).
- Standalone instrumented Wayland GL probes satisfy behavior assertions but
  report unsuppressed Fontconfig/Pango/GLib and unknown unloaded-module lifetime
  allocations. Disabling optional libdecor narrows but does not eliminate them.
  Logs: `wayland-script-sanitized.log`, `disconnect-sanitized.log`,
  `wayland-no-libdecor-sanitized.log`. These are not claimed leak-free.
- Actual fully instrumented game hunt (own font/renderer cleanup), outside a
  debugger, leak detection enabled: exit 0, SDL cleanup, no sanitizer diagnostic.
  `game-wayland-sanitized/`, final invocation `game-wayland-sanitized-final.log`.

No sanitizer suppression or security setting change. Kernel-denied debugger
attach was abandoned; later debugging launched the program as a debugger child.

## Runtime evidence and precise scope

**Virtual Xorg + Openbox:** ten actual hunts cover default windowed, secondary
exact-refresh exclusive, borderless oversized, missing identity/automatic,
primary/secondary CLI, unknown identity, unsupported 777x555 config/CLI and
invalid option. Geometry, fullscreen/windowed/fullscreen transitions where
scripted, normal exit and desktop restoration pass. See `x11/runtime-results.json`
and `x11-runtime.log`. Earlier fixture retries lacked the extracted Openbox data
and theme environment; the final fixture includes them.

**Nested KWin + XWayland:** SDL-only desktop fullscreen targets secondary
(`xwayland/raw-sdl.log`). Three full hunts load and exit normally, preserve profiles
and restore desktops (`xwayland/game/runtime-results.json`). Secondary exclusive
800x600 is observed at (1280,0), then windowed at (1520,240), then exclusive at
(1280,0), with production configure/actual-state logs. Unsupported 777x555 falls
back to the secondary 1280x1024 desktop popup. The latter's synthetic Alt+Enter
was not delivered reliably; the harness's transition assertion correctly fails
for that case. Do not count those repeated identical geometry samples as toggle
acceptance. This nested success does not resolve the prior real Hyprland/XWayland
secondary-exclusive failure.

**Nested Weston direct Wayland:** baseline SDL-only warp reproduction gives
increasing 25,50,...250 look deltas for equal +25/+5 input, whereas relative mode
returns equal signed deltas (`weston-pointer-{0,1}.log`). Actual production hunts
show equal +/-10,3 look motion, movement, Pause stopping the view, Escape/dismiss,
Alt+Enter secondary/windowed/secondary, focus loss/context teardown and regain
through another native SDL app, and Escape/Y evacuation. Hunt screenshot was
inspected. Unsupported 777x555 uses secondary desktop fullscreen. See
`game-wayland-focus/exclusive-focus/results.json`, `game-wayland-final/`,
`game-wayland-sanitized/`. The game's evacuation route logs legacy ABNORMAL_HALT
with an empty message but exits 0 with completed SDL cleanup; it is not the
separate WM-close log path. Copied .sav/.sab remain 1660/7176 bytes; all 68 saved
VK binding bytes remain identical.

**No-override startup:** generated default borderless config launches an actual
Wayland hunt; SDL chooses Wayland. In Weston, growing the loading window keeps
its compositor placement and partly clips the normal toplevel. The engine logs
requested versus actual output honestly. Keyboard movement/pause/escape, focus,
toggles and exit work; this case's early mouse injection fell outside its window,
so it is not an independent look acceptance. The explicit fullscreen route above
validates look and fully visible output presentation. See
`game-wayland-default/default-no-overrides/results.json`. Documented compositor
placement limitation; no arbitrary normal-window positioning is promised.

**Nested KWin direct Wayland:** baseline warp defect reproduced; relative enable
succeeds but the fixture's synthetic relative input is not gameplay acceptance.
No new physical Hyprland, GPU, physical pointer-feel or real multi-monitor
acceptance is claimed from any virtual/nested result.

## Fault ownership and limits

Rapid capture churn in the standalone presentation fixture crashed extracted
Weston 15 (compositor coredump). A terminal helper also crashed independently;
actual focus validation instead uses the native SDL helper. Neither is reported
as an engine crash or used to claim successful focus testing.

The dead compositor exposed SDL pending-window loops ignoring roundtrip errors.
A standalone reproducer shuts down only its own Wayland connection with a
fullscreen request pending: unpatched SDL hangs until forced timeout, exit 137
(`disconnect-unpatched.log`); the isolated patched SDL returns false and exits 0.
The patch checks both sync and flush loops. Inspected SDL 3.4.0 retains those
unchecked loops, so no speculative version upgrade was made. This is not a
universal timeout for a live compositor that stops responding inside a roundtrip.

Known independent SDL X11 initialization/teardown lifetime allocations and the
dummy-video UBSan issue remain unsuppressed. New standalone Wayland lifetime
reports are documented above. Full game instrumented gameplay is clean; broader
vendor/driver cleanup is outside this milestone.

## Next safe action

Push this branch, require the full 12-job Windows/Linux matrix at that exact SHA,
report CI plus this evidence to the supervisor, address review findings in new
commits and wait for acceptance. Only the supervisor may dispatch a fresh 4h
agent. Physical acceptance remains pending for Will when a suitable session is
available; it must not be inferred from nested fixtures.

## Supervisor correction — effective target after disappearance

Independent review found that the existing disappearance-during-exclusive path
retried primary but retained the vanished secondary in `mapped.id`/`mapped.target`.
If primary also rejected the requested size, the new Wayland desktop fallback
queried the vanished secondary; diagnostics likewise named that stale output.

The application now uses one native WindowDisplay value for ID, target and exact
mode. Refreshing a vanished target resets that value to primary/automatic in both
existing race paths. Retry, desktop fullscreen fallback and final diagnostics all
consume that value. This changes only the current application; caller-owned/saved
intent is retained. There is no new hotplug/recovery framework.

A focused injected regression first observes a valid secondary, then loses it,
rejects the primary's requested exact size, and confirms primary desktop fullscreen
with primary timing. It verifies cleared secondary target/rate and unchanged caller
preference. The production desktop helper is used by both the test and fallback.

Post-correction validation: GCC Debug/Release and Clang full builds/CTest pass;
strict GCC/Clang platform compilation passes; all 11 changed SDL tests pass with
ASan/UBSan/LSan; the disposable production Wayland presentation/fallback/restoration
and socket-disconnect runtime passes. Logs `review-fix-*.log` in the evidence root.
Earlier full-game, X11 runtime and known sanitizer limitations remain as above.
New exact-head 12-job CI and supervisor re-review are required before acceptance.
