# Phase 4i: live display recovery and drawable dimensions

Base: dc00b56a056af17aed794379d7433777e083d2ce (accepted 4h).
Branch: port/display-recovery-scaling.
Worktree: /home/willvdb/code/games/carnivores2-phase4-display-recovery.
Evidence: /tmp/carnivores-phase4i. No predecessor/frontend changes; main untouched.

## Audit and accepted design

Production PumpOneEvent discards topology, window-size and pixel-size events.
SetVideoMode alone reconciles geometry and overwrites Configuration.size with
actual pixels, making a fallback size become later requested intent. Focus
reactivation reconfigures placement even if the user moved the window.

Keep existing SDL low-density rendering: pinned Wayland GetBufferSize and
ConfigureWindowGeometry maintain configured pixels and let the compositor scale.
Adding HIGH_PIXEL_DENSITY would change configured rendering/HUD semantics.
Read actual SDL window and drawable sizes; never infer pixels from content scale.
Game handles topology bursts at frame boundaries (150 ms quiet, 500 ms deadline).
Recover lost/unreachable output or invalidated applied identity. Reconnect alone
does not steal a reachable window; next explicit mode request resolves intent.
Session indices remain fresh-catalog ordinals, never implicit persistent identity.
No automatic config/profile writes. Physical acceptance remains unproven.

Implementation and runtime evidence are complete; exact-head CI and supervisor review are the final gates.

## Implemented policy and supervisor corrections

See docs/LINUX_DISPLAY_RECOVERY.md for the final units/recovery contract.
Recovery runs at frame boundaries, retains configuration intent and uses fresh
owned selection/native identity preconditions. No snap-back on reconnect. Failed
placement is bounded to one attempt per settled topology. Linux focus restoration
preserves user placement. Current RandR primary is queried via the exact borrowed
mapping; native handles remain private. Windows compatibility paths remain.

A narrowly guarded SDL prerequisite removes disabled (connected/no CRTC) X11
outputs after querying current native state. It preserves temporary mode-switch
disables; the patch chain retains exact hashes and LF/CRLF/tamper checks.

Supervisor found and worker corrected (before functional commit): topology
SetVideoMode could bypass viewport publication when fallback changed size, and
zero-drawable suspension occurred before recovery. Linux derived geometry now
publishes viewport in the same helper; non-minimized transient invalid metrics
can recover before rendering is suspended. Actual-game GL_VIEWPORT/zero-metric
injection evidence is in game-x11-reviewed/.

## Evidence completed so far

- Debug full suite: 16 active passes, 5 explicit opt-in runtime skips (21 total).
- GCC Release and Clang full suites pass after running protocol fixtures outside
  the restrictive socket sandbox. Earlier sandbox-only private Wayland socket
  creation failures are environment failures, not accepted green tests.
- Strict GCC/Clang portable ASan/UBSan/LSan: 95/95 including the final invalid-logical
  regression; strict compilation of all four Linux platform/identity sources passes.
- Fully instrumented full CTest retains the one inherited unsuppressed SDL dummy
  null-source memcpy UBSan at SDL_video.c:1341. Other active suites pass. No blanket
  sanitizer suppression; separate prior X11/Wayland lifetime limits remain.
- x11-runtime.log: production identity/recovery plus inherited 20 cycles/output,
  zero SDL allocations and independently restored server desktops.
- x11-negative-control.log: same new production recovery executable linked with
  accepted predecessor SDL fails on disabled display count. No source/policy change
  in that negative control; only dependency library differs.
- game-x11/ baseline: actual client 640x480 while accepted game renders 800x600.
  game-x11-final/ corrected actual hunts: requested 800x600 survives resize,
  toggles, user movement, layout, minimize/restore and output loss/return.
- game-x11-reviewed/ records the reviewed changed-size fallback, direct GL_VIEWPORT
  readback and transient-zero injection. Debugger-only earlier failures are retained
  separately; they are not evidence of normal game shutdown.
- wayland-runtime.log: nested Weston production 1x/2x density, presentation,
  unsupported-size fallback and connection-loss behavior pass.
- sway-runtime/: real private headless compositor state confirms mixed/fractional
  scales 1/1.5 -> 1/2 -> 1/1.25, rotation, target disable/re-enable. Actual game
  renders throughout, performs one recovery and exits normally. .sav=1660,
  .sab=7176 and all 68 binding bytes preserved. KWin trial did not apply settings
  and is excluded from fractional/rotation acceptance.

All runtime servers/compositors are disposable and cleaned up. Their configuration
and copied game profiles live under /tmp/carnivores-phase4i. Original content is
read-only linked; no proprietary assets or original profiles are changed/committed.
The extracted Sway/Weston test runtimes are under /tmp only, not host installations.

Remaining: final build/review checks after the last corrections, exact pushed-SHA
12-job CI, independent supervisor review and any requested corrections. Physical
acceptance and full sanitizer cleanliness are explicitly not claimed.

## Reproducible final local checks

All build trees and dependency copies belong to this milestone under the evidence
root. Debug, Release and Clang final CTest: 16 active passes and 5 opt-in skips.
Installed SDL Debug: 15 active passes and 5 skips (the bundled patch-chain test is
not applicable); no installed dependency changes. Fully instrumented final CTest
with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1`: 15 active passes, the one inherited dummy UBSan
failure, and 5 skips. No sanitizer exclusions or suppressions were added.

- Configure: `cmake -S . -B /tmp/carnivores-phase4i/build-debug -G Ninja
  -DCMAKE_BUILD_TYPE=Debug -DRENDERER=GL
  -DFETCHCONTENT_SOURCE_DIR_SDL3=/tmp/carnivores-phase4i/deps/sdl3-src
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/tmp/carnivores-phase4i/deps/googletest-src`.
  Release changes build type; Clang selects clang/clang++; sanitized adds
  address/undefined sanitizer and frame-pointer flags to C/C++ and link flags.
- `cmake --build /tmp/carnivores-phase4i/build-debug --parallel 6` and
  `ctest --test-dir /tmp/carnivores-phase4i/build-debug --output-on-failure`;
  equivalent commands for build-release/build-clang/build-sanitized/build-system.
- `/tmp/carnivores-phase4i/portable.sh`: both compilers, strict warnings, full
  address/undefined/leak instrumentation, 95 tests each.
- `/tmp/carnivores-phase4i/strict.py`: production platform/identity source strict
  warnings under GCC and Clang, using exact build compilation definitions.
- Private Xorg wrapper `runtime/x11/run_mode_leak_test.sh`: mode-leak, identity,
  game CLI and recovery binaries as four arguments; final sanitizer evidence
  `x11-sanitized-final.log`.
- Private Weston wrapper `runtime/wayland/run_presentation_test.sh`: presentation,
  disconnect and recovery binaries as three arguments; final evidence
  `wayland-runtime-final.log`. Local wrappers only adapt extracted test runtime
  module paths; repository wrappers are used by Ubuntu CI.
- `game_x11.py`, `game_x11_review.py`, `sway_worker.py` retain exact isolated
  actual-game procedures; their JSON and debugger/game logs retain dimensions,
  native state, viewport and profile verification. Final reviewed X11 probe
  reports `normalExit: true`, retained request and matching viewport at both
  fallback sizes. No proprietary files are included in this branch.

Ordered commits: f1a95a7 is the guarded SDL prerequisite; 4742421 implements
owned events/recovery/units and regressions; the evidence/docs
commit records the complete contract. The final pushed SHA, CI run URL and
12-job conclusions are recorded in the supervisor's durable Phase4i handoff
(the checkpoint cannot contain its own commit hash). CI includes 10 Windows
native/SDL/SOFT/Menu variants and 2 Linux variants with native X11/Wayland fixtures.
No additional milestone is authorized by this handoff.

Final scoped runtime reruns: x11-sanitized-final.log and
wayland-runtime-final.log both exit 0. X11 performs 20 fullscreen/windowed
cycles on each output with zero remaining SDL allocations. Weston passes
production mixed-density, presentation/fallback and connection-loss checks.

## Final capture-order audit correction

The first pushed review candidate is dbdf41ee81b2a8c8ed78f8f8dbb19b98d1972470.
A subsequent bounded fix keeps `drawableSuspended` true across a recovery attempt
and reacquires capture only at the existing usable/reachable frame boundary.
Previously an already suspended window could reacquire capture while its recovered
metrics were still invalid, then skip the release because it was already marked
suspended. `game_capture.py` keeps injected zero metrics across native recovery
and logs actual `Platform::SetMouseCapture` calls: only releases occur while
invalid, and capture returns once valid. `game_capture_negative.py` uses the saved
pre-fix actual-game executable under the same isolated fixture: it logs
`CAPTURE_WHILE_INVALID 1`, while the corrected executable never does.
Final acceptance must use the subsequent corrected pushed SHA and its own CI run.
