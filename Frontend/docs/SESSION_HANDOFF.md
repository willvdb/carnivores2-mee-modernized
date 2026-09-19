# Genesis session lifecycle handoff

Branch: `frontend/genesis-session-adapter`.
Base main: `7ab7d47c77c5968ae1e501a1dd2cdfaf406edceb` (merged foundation PR #10).
The branch was created in an independent worktree. No engine branch was merged,
rebased or rewritten; main was not modified. No open PRs were present during the
branch checks. All changes are under `Frontend/`.

## Delivered contract

* Stable session UUID and journal schema 1, separate from the unchanged manifest.
* `prepared -> launching -> running -> returned -> inspecting -> candidate` or
  `quarantined`; `failed` and `interrupted` preserve error/recovery evidence.
* Pins for hunter/instance/association, native slot, complete content revision,
  engine evidence/review state, policy/selection, source/baseline bytes, interpreter,
  fixture and codec helper. Preflight drift blocks the child.
* `sessions/<UUID>/{journal.json,baseline/,work/state/,returned/,logs/}`; actual
  child cwd is `work/`. Copies are independent, without links. Immutable source
  and previous good state remain the original managed snapshot.
* Authority strategy B: clean returns are durable candidates, never promotion.
  No schema migration, source normalization, original provenance replacement,
  synchronization to native files, or authoritative state-history redesign.
* Fixed trusted Python fixture only, explicit executable/argv, no shell, bounded
  timeout/cancellation and 64 KiB retained stdout/stderr each. Excess logs are
  drained and reported as truncated. Child handles are reaped before inspection.
* Recovery never relaunches or signals a journal PID. A potentially live child
  yields `interrupted` without reading its state; returned/inspecting recovery
  resumes capture without overwriting divergent earlier evidence.
* Independent capability fields: synthetic success does not certify Genesis,
  native observer execution or a complete hunt/save round trip.

CLI: `session prepare-synthetic`, `run`, `inspect`, `recover`, `reconcile`, plus
`genesis-observer-plan`. See [README](../README.md) and
[SESSION_MODEL](SESSION_MODEL.md) for operational details and lock recovery.

## Native status and exact policy

**No actual Genesis observer process was launched.** The exact local content
revision was re-fingerprinted and matched the prior audit: 344 files, 768,073,101
bytes, SHA-256 `9c6fc5221744ad8e9a74689d308ba572b6aefe6cd6c317e030e5774757c2bf65`.
Counts remained 8 areas, 9 licenses, 8 weapons, 4 priced accessories, 9 maps.
Both native state files were hashed before/after and remained unchanged. No
assets, saves, binaries or user-specific locators were committed or downloaded.

`genesis-current-mee-observer-v1` requires that entire revision, observed newer-MEE
grammar and the pinned catalog structure. It permits only a validated area,
native slot 0..7 and dawn/day/night; `din=0`, `wep=0`, `-observ`, no equipment.
It emits all six evidenced default `smod` values and refuses overrides. Area
affordability is checked without mutating score; rank/progression equivalence is
unverified. All eight actual advertised area plans passed a read-only structural
check with an explicitly synthetic score input, not a personal progression claim.
An optional selected engine is hash evidence only, never build certification.
`process_launch_allowed` remains false.

The blocker is specific: fixed cwd-relative HUNTDAT reads and trophy reads/writes,
plus module-directory configuration reads/default writes and other output sinks.
The smallest recommended seam is an opt-in writable session root for **all**
profile reads and writes/config/logs/screenshots/debug/perf output, with no native
fallback. Cwd can remain the read-only content context; an additional explicit
content root is an option, not a required broad refactor. See
[ENGINE_SESSION_SEAM](ENGINE_SESSION_SEAM.md) and [GENESIS_POLICY](GENESIS_POLICY.md).

## Validation

Standalone CMake configure/build and CTest on Linux:

| Toolchain/check | Result |
| --- | --- |
| GCC 16.2.1 Debug | 90 Python/codec tests pass, no skips |
| Clang 22.1.8 with ASan/UBSan | same 90 tests pass, no skips or sanitizer findings |
| Python 3.14.7 compileall | pass |
| git diff --check | pass |
| Original frontend suite | all 55 tests retained and passing |
| Engine file overlap | none; no files outside Frontend changed |

Sanitizers use `-fsanitize=address,undefined -fno-omit-frame-pointer
-fno-sanitize-recover=all`, leak detection and halt-on-error. CTest supplies the
built C++ codec probe to Python. The new Python runner itself is not C++ code;
the sanitizer coverage applies to the real profile probe used throughout tests.

Demonstrated process scenarios: clean unchanged state; SAV-only and SAV+SAB
updates; nonzero exit unchanged/changed; truncated SAV/SAB; missing SAB after SAV
update; deleted SAV; unexpected companion; registration mismatch; hang timeout;
cancellation; self-termination; bounded log flooding. Failure/robustness checks
also cover spawn failure, preflight identity/content/engine/codec/state drift,
reference/bundled/unknown rejection, journal replacement failure, lost process
ownership, failure writing the running journal with child cleanup, actual
frontend process exit after durable return, resumable partial capture, divergent
captured bytes, links/aliases/oversized files, and source/baseline immutability.

Read-only comparison included 39 local/remote engine refs and active worktrees,
including `port/linux-display-behavior`, `port/linux-display-persistence` and
`port/display-recovery-scaling`. No Frontend overlap was found. Engine/Menu/
platform/renderer/root CMake/CI/tests were untouched and engine tests unmodified.

## Limits and next milestone

Native Windows execution/durability remains unvalidated. The fixed fixture does
not spawn descendants; this is not an arbitrary-process supervisor or an OS
sandbox. A hard frontend kill can leave a live child and stale writer lock;
recovery conservatively retains evidence and requires operator verification.
No automatic cleanup/promotion or acceptance of engine relocation reviews exists.
Readable, quiescent SAV/SAB pairs still do not prove atomic history or correct
progression. Full HUNTDAT hashes still include presentation assets, as before.

Ready for human review as a synthetic session-lifecycle backend with a blocked,
pinned Genesis observer policy. Recommended next milestone: after coordinating
with active engine work, implement/test the smallest engine session-output seam,
pin a reviewed engine build, and run one disposable Genesis observer session
through the existing candidate-only reconciliation. Do not add GUI or broader
edition support before that proof.
