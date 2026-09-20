# Native session isolation handoff

## Branch and integration

Task branch: `codex/native-session-isolation`, in its existing dedicated worktree.
Current follow-up code: `480e7362577beb0f06d6adad4f0db20f257bf49d`;
the following documentation commit records acceptance limits. The current pushed
head and its CI results are recorded in PR #13, not the historical checkpoints below.

- Starting local main: `a2cfec8ef3590c6c8d56d16c7a69e4c0e5327f5a`.
- Fetched main/base: `08c219fdb877a1fe30ce431eef8ac7b38a4987b3`.
- Prerequisite frontend branch, local and remote:
  `46252588a76db705ed7b7148041f4f91c44897b5`.
- Normal no-conflict merge preserving that history:
  `1219fc54e5c4a2c1cebc9ad55e006e7e2c0ae7e3`.
- Implementation checkpoint before final Windows screenshot compatibility correction:
  `5f19cbb6a5afd018525ab6bfcb939d054f2c824b`; following changes document delivery.

Draft review: [PR #13](https://github.com/willvdb/carnivores2-mee-modernized/pull/13).
Only the task branch was pushed. No main, engine-development or existing frontend
branch was modified, rebased or force-pushed. Original main worktree still has
its untouched untracked `carnivor.log` and `render.log`.

## Follow-up acceptance and pre-merge correction — 2026-09-19

### Local state and reported acceptance

At the start of this pass, both the existing task worktree and the remote PR head
were `7097eecb12daacf86053092d30853511a773030c`, with no staged/unstaged changes,
untracked files or additional commits on that branch. All other available
registered worktrees were clean apart from the two existing main-worktree logs.
The local-only backup branch contains older x64 audit/build work, not native-session
follow-up fixes. No reset, clean, stash, rebase, force-push or main change was used.

The user reports that a local test worked after a couple of small changes.
**Those changes and the successful run's evidence were unavailable in this
environment.** They were not reviewed, included, reverted or assigned an invented
purpose. This correction is independent of those missing fixes.

| Acceptance detail | Evidence available for the reported local success |
| --- | --- |
| Exact tested revision / uncommitted edits | Unverified; neither a revision nor the reported local edits were available |
| Platform / backend | Unverified; this correction was developed on Linux, which does not identify the reported run's platform |
| Direct engine vs frontend native observer | Unverified |
| World entry and normal exit | User-reported success only; neither event independently established |
| Session ID / final reconciliation status | Unavailable; no clean-candidate claim |
| Original, source and baseline hash checks | Not established for the reported run |
| Purpose of the small local fixes | Unknown; no code delta available to review |

The retained earlier Genesis planning report explicitly records
`real_engine_executed: false`; its two unchanged native-file checks belong to that
historical planning pass. The available main-worktree logs predate this milestone,
and the retained adapter logs describe automated fixture tests. None establishes
the later reported interactive run. No personal paths, raw logs, saves or assets
are added by this follow-up.

### Correction and automated evidence

Code and regressions are committed as
`480e7362577beb0f06d6adad4f0db20f257bf49d`. Previously, ancillary config/output
failure skipped native capture, then empty defaults were reported as unreadable
state and both native members changed. Now all ancillary findings still quarantine,
while an independently safe state path is inventoried, captured and inspected.
Unsafe paths and failed capture produce explicit unavailable stages, `unknown`
readability and `null` changed-members comparison. Observed missing/truncated
members still produce actual missing/unreadable findings. The full additive
schema-1/2 reporting contract is in
[NATIVE_OBSERVER.md](../Frontend/docs/NATIVE_OBSERVER.md#return-observation-and-unavailable-evidence).
Terminal historical journals are not rewritten.

Both unchanged-pair/output-anomaly and changed-SAV/config-anomaly regression cases
were run against the reviewed `7097eecb` reconciliation module in an isolated Python
process; both reproduced the false `no` readability. They pass with the correction.
Recovery tests verify partial-copy completion, durable ancillary findings after
an injected crash, refusal to overwrite divergent work/returned evidence, and no
engine query, launch, PID signal or live-state capture on recovery. Linux symlinks
and injected Windows reparse attributes are covered locally; the same suite uses
real junctions on Windows CI.

The final code was validated with these complete standalone commands:

```sh
cmake -S Frontend -B /tmp/c2-pr13-followup -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/c2-pr13-followup --config Debug
ctest --test-dir /tmp/c2-pr13-followup -C Debug --output-on-failure --no-tests=error
python3 -m compileall -q Frontend
git diff --check
```

Result: **116 frontend tests passed, no local skips**, with the mandatory real
compiled C++ profile probe and native fixture. Compilation and whitespace checks
passed. These tests verify unchanged fixture source/content, managed snapshot,
manifest and immutable baseline bytes/hashes, and candidate-only authority.
The Genesis policy double remains explicitly fixture-only; the unmodified policy
still rejects fixture content. No interactive game or normal-hunt acceptance was
performed in this pass, and no Windows local-runtime result is claimed.

No engine sources changed, and no user-local engine fix was available; a new local
engine rebuild/startup run was therefore not required or claimed. Engine/menu,
platform/display and standalone frontend CI workflows are unchanged. Final-head
hosted CI results are recorded in the PR, separately from the historical CI below.

Final incremental review found no changed output allowlist, content/trust gate,
legacy launch or Phase 4 display policy; no new profile creation, state promotion,
installation sync, assets or generated files; and no renderer, controller,
networking, GUI or progression expansion. Remaining acceptance limitation:
locate/review the reported small fixes and bind the successful interactive run to
its actual code/platform/path evidence. Success on one platform would not certify
the other, normal-hunt progression, all mods or authoritative save promotion.
PR #13 remains open and unmerged, with auto-merge disabled.

## Historical completed checkpoints and decisions

- [x] Read repository/portability/frontend/display contracts and trace write sites.
- [x] Integrate prerequisite preserving history.
- [x] Versioned pre-startup policy and strict argument consumption.
- [x] Independent state/config/output, safe roots and complete baseline validation.
- [x] Production file routing, strict profile loads, sticky I/O exit failures.
- [x] Production regression harness and actual Linux engine build.
- [x] Standalone frontend Linux/Windows CI with mandatory real codec probe.
- [x] Separate explicitly trusted native observer adapter and schema-2 recovery.
- [x] Distinct self-review, read-only independent review, fixes and affected retests.

Interface/layout: [ENGINE_SESSION.md](ENGINE_SESSION.md).
Experimental frontend commands, asset-free smoke and human native prerequisites:
[Native observer](../Frontend/docs/NATIVE_OBSERVER.md).

V1 requires existing complete 1660-byte SAV / 7176-byte SAB pairs, independent and
byte-identical in source, baseline and work/state. No missing companion synthesis,
substitute profile or malformed-baseline repair. Cwd remains read-only content
context. Configuration is only work/config/config.cfg, engine files only work/output;
performance captures, multiplayer and legacy Windows audio DLL selection are
disabled. Original format/keybinding bytes and legacy display/config precedence
are preserved. This is trusted engine I/O isolation, not an OS sandbox, hostile
filesystem race defense, pair transaction or containment of driver/library writes.

Native preparation/run each require the explicitly selected reviewed executable,
its trusted hash, supported queried contract and experimental gate. Existing exact
Genesis content/policy gates remain. Schema 2 pins config and execution evidence;
schema 1 keeps the fixed synthetic fixture. Native timeout defaults to 900 seconds
(range 30..3600), not the synthetic five seconds. Returned state stays candidate-only;
no promotion, source synchronization or ownership/progression inference exists.
Interrupted journals never trigger a PID signal or automatic relaunch. Return and
recovery revalidate native execution evidence without executing the engine.

## Historical implementation validation (through reviewed head 7097eecb)

Baseline before implementation: actual GCC Linux Debug engine with pinned SDL
built; 17 runnable CTest entries passed, five existing opt-in display entries
skipped. Frontend baseline: 90 tests passed, no skips, with the real probe.

Final local commands, all successful:

```sh
cmake --preset linux-x64-sdl-gl-debug
cmake --build --preset linux-x64-sdl-gl-debug --parallel 12
ctest --preset linux-x64-sdl-gl-debug

cmake -S Frontend -B /tmp/c2-native-frontend -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/c2-native-frontend --parallel 6
ctest --test-dir /tmp/c2-native-frontend --output-on-failure
python3 -m compileall -q Frontend
git diff --check
```

Engine: 24 CTest entries, 19 passed and five opt-in skips. The 16 GoogleTest
executables run **400 tests**, including **24 production session tests**. The
actual-engine Python startup/termination suite has seven tests: six passed and
one Windows-only junction case skipped on Linux. Other entries cover the platform
boundary and pinned SDL patch chain. The five local opt-in skips are X11Identity,
SDLX11ModeLeak, DisplayRecoveryRuntime, SDLWaylandPresentation and SDLWaylandDisconnect.
Local openbox/weston are absent; their virtual display scripts were run successfully
by hosted Linux CI instead. No physical monitor/interactive acceptance was attempted.

Frontend: **106 tests passed, no local skips**, with real C++ codec probe and a
separate compiled native fixture using production Session/Files APIs. Existing
90 synthetic/discovery/policy tests remain. Probe-dependent tests cannot silently
skip through the CTest runner; the native fixture is mandatory too.

Sanitizers used Clang ASan + UBSan, leak detection and halt-on-error:

```sh
cmake -S . -B /tmp/c2-session-sanitized -G Ninja \
  -DRENDERER=GL -DCARNIVORES_PLATFORM=SDL3 -DCARNIVORES_SYSTEM_SDL3=ON \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all' \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all'
cmake --build /tmp/c2-session-sanitized --parallel 6 \
  --target Carnivores1 Carnivores2Tests Carnivores2SessionTests Carnivores2SessionExitProbe
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /tmp/c2-session-sanitized \
  -R '^Carnivores2Tests(\.Session|\.SessionStartup)?$' --output-on-failure

cmake -S Frontend -B /tmp/c2-native-frontend-sanitized -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all'
cmake --build /tmp/c2-native-frontend-sanitized --parallel 6
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /tmp/c2-native-frontend-sanitized --output-on-failure
```

Sanitized engine checks passed: 142 core + 24 production session GoogleTests,
plus the actual instrumented engine's earliest startup and production exit probe
(six Python cases run, Windows junction skipped). All 106 frontend cases also
passed using instrumented C++ probe/native child. Python orchestration itself is
not sanitizer-instrumented. No full graphics hunt was sanitized; immediate fatal
termination intentionally skips global destruction after explicit shutdown.

Hosted GitHub Actions at `4d47648361d601f20690b61b0a1e0e6f9769ec43`:
[engine/menu/platform matrix](https://github.com/willvdb/carnivores2-mee-modernized/actions/runs/35464043715)
and [standalone frontend](https://github.com/willvdb/carnivores2-mee-modernized/actions/runs/35464043717)
both completed successfully. Engine matrix retained all ten Windows jobs (x86/x64,
Win32/SDL, Debug/Release, software and menu) and two Linux jobs, including existing
X11/Wayland regression scripts. Frontend passed Linux and Windows; Windows retains
one existing case-sensitive-filesystem skip, with no codec-dependent skips.
The frontend recovery follow-up at `5f19cbb` also passed both hosted jobs. Its
stronger Windows screenshot test exposed the legacy `.BM` name noted below:
`ctest --test-dir build/windows-x64-gl-debug -R "^Carnivores2Tests" --output-on-failure`
failed only the expected `.BMP` filename assertion; other Windows presets reported
the same failure. The compatibility correction retains actual production naming,
adds content checks and another native adapter test (106 total). Corrected CI
results are reported in the draft PR/final delivery rather than presumed here.

Logs are in the build trees' Testing/Temporary/LastTest.log and
`/tmp/c2-session-*.log`, `/tmp/c2-native-adapter-tests.log`,
`/tmp/c2-frontend-sanitizer-tests.log`.

## Historical review findings fixed and retained limits

Separate read-only review and self-review found and fixed:

- Fatal shutdown must use immediate `_Exit` after closing logs/services; `exit`
  could deadlock during MEM_DEBUG global destruction after allocation failure.
  A real production subprocess exit probe checks clean=0, I/O=3, fatal/early=1,
  closed logs and an atexit sentinel.
- Malformed option values now have independent tests, not only duplicate-option
  rejection. Slot 7 has a real trophy read/write regression.
- MSVC needs an exact production-body extraction harness instead of unresolved
  entire gameplay translation units. CMake regeneration tracks the originals;
  graphics/audio are doubled, I/O/config/trophy/screenshot functions are not.
- Windows rooted paths without a drive are not `is_absolute()`; project validation
  now rejects every root path. Windows TEMP short aliases use canonical fixtures.
- Windows case-insensitive discovery tests exercise real native behavior while
  retaining path-escape assertions.
- Native return/recovery now reconstructs the pinned spec, rejects capability
  type drift, quarantines changed evidence and never executes a query on recovery.
  Partial returned-copy recovery is covered and remains candidate-only.
- The Windows screenshot regression now calls the real production screenshot
  function with only GPU readback doubled. It exposed the existing 12-byte Windows
  filename buffer's `.BM` suffix. Tests assert that actual file and BMP signature;
  the adapter admits precisely the Windows four-digit `.BM` form, retaining legacy
  engine behavior. Extra/unknown suffixes still quarantine. Windows screenshot
  naming beyond 9999 shots remains unsupported by this validation adapter.

Final write audit: trophy/config/screenshot/render/debug outputs use the routed
Platform APIs; structured logs use routed text APIs with flush/close checks;
GLPerf file-opening paths are disabled before initialization. Shader and script
streams are read-only. RunGame establishes policy before platform/log/config
startup; Win32 entry setup only assigns instance/procedure. Module/cwd config
fallbacks are confined to legacy mode. No disk/network layouts, display policy,
keybinding codecs, renderer or menu implementation were redesigned.

Tests compare source/content/baseline inventories and bytes, and the actual-engine
subprocess suite hashes copied engine-directory files and config sentinels before
and after. Fixtures deliberately expose old cwd/module fallback behavior. No user
assets or existing personal profiles were selected. No proprietary files added.

An actual asset-free **native fixture process** ran through production session I/O
and frontend candidate reconciliation. Actual game binaries ran capability,
invalid-startup and platform-failure paths only. The fixture explicitly doubles
the Genesis policy and keeps its real synthetic content hash; the unmodified
policy rejects that hash. **No actual Genesis hunt/world entry was validated.**

Next human step: review the draft diff, then supply the already-owned exact pinned
Genesis content and an eligible existing complete managed snapshot to the separate
gated native command. Use disposable session copies, observe world entry/observer
controls and clean exit on Windows and Linux, and verify original hashes. Keep
results candidate-only. Review native save-pair coherence and real audio/display
behavior before considering any broader launch gate. General launch/promotion
remain disabled pending that consequential acceptance decision.
