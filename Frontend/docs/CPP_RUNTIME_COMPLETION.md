# C++ runtime completion (Python-free production frontend)

Maintained, resumable record for the runtime-completion sprint. Terms are kept
distinct: **implemented** (native library code exists), **wired** (reachable
through `c2-frontend-native`), **tested** (differential and/or end-to-end
tests pass at a recorded SHA), **reviewed** (separate reviewer context
inspected it), **merged** (never, in this sprint; `main` is not touched).

## Identity (final, 2026-09-26)

- **Frozen candidate code SHA: `a0ea7cb63f6c1a7fb9f569e691ca446fec2da385`.** Every
  final gate below ran against exactly this SHA. Commits after it touch only this
  record.
- Main: `a86be96faec2aacb4594d253bb9385091a0c2739`, untouched. Nothing was merged.
- Base: `frontend/cpp-planning-completion` `38b398a`.
- Integration branch `frontend/cpp-runtime-completion`, worktree
  `~/code/games/carnivores2-runtime-completion`.
- Worker branches A `31cb3a7`, B `c34678a` and C `fcb121f` are fully integrated.
  `runtime_pending.cpp` is deleted and no stubs remain.

## Canonical native executable

`c2-frontend-native` (unchanged target name) is the production frontend. It is
staged with `c2-profile-probe` (codec helper) and `c2-frontend-synthetic-child`
(compiled developer synthetic fixture). The former CMake custom target
`c2-frontend`, which only ran `python frontend.py --help`, is retained as
optional reference tooling behind `C2_FRONTEND_REFERENCE_TOOLS` (default: the
value of `BUILD_TESTING`). `python3 Frontend/frontend.py` remains the reference
CLI for differential testing, not the product.

## Workstreams

| Stream | Scope | State |
| --- | --- | --- |
| A session lifecycle | preparation, native adapters, run/recover, reconciliation, compiled synthetic child | implemented, wired, differentially tested, reviewed (R4), fixes integrated |
| B store operations | hunters, settings, backup recovery, register/relocate/refresh/discover, associate/import, upgrade | implemented, wired, tested, reviewed (R2), fixes integrated |
| C supervisor + acceptance | owned child supervisor; preview/accept/recover-acceptance | implemented, wired, tested (incl. fault matrix), reviewed (R3), fixes integrated |
| Coordinator | interfaces, CMake/CI, dispatcher, E2E, this record | dispatcher reviewed (R1), fixes integrated |

## Command / operation matrix

Every `frontend.py` command is **implemented and wired** in
`c2-frontend-native`. "CLI diff" = `test_cli.py` twin-store parity with
`frontend.py` (stdout, status, error text, every store byte); "Module diff" =
the workstream's differential suite; "E2E" = native-only
`test_native_workflow.py`.

| Command | Native implementation | CLI diff | Module diff | E2E |
| --- | --- | --- | --- | --- |
| `status`, `hunter list`, `expedition list`, `host-settings` | `Manifest::export_json` | yes | yes | yes |
| `managed-state inspect` | `resolve_generation` | yes (refusal) | yes | yes |
| `profiles`, `catalog`, `refresh-state`, `simulate-return`, `launch-dry-run` | existing libraries | yes | yes | yes (not simulate-return) |
| `genesis-observer-plan`, `native-hunt plan` | `planning_store` | yes (real refusal + double) | yes | yes (plan) |
| `hunter create/select/rename/archive`, `host-settings --json`, `recover-backup` | `store_ops` | yes | yes | yes (not recover-backup) |
| `expedition register/relocate/refresh/discover [--register-managed]` | `store_ops` | yes | yes | register/refresh/discover |
| `associate [--import-copy]`, `managed-state upgrade` | `store_ops` | yes | yes | yes |
| `session prepare-synthetic/run/reconcile/recover/inspect` | `sessions`, `session_runner`, `reconciliation` | inspect only | yes | yes |
| `native-observer prepare/run` | `native_session`, `session_runner` | no | yes | yes |
| `native-hunt prepare/run/inspect` | same | no | yes | yes |
| `managed-state preview/accept/recover-acceptance` | `acceptance` | no | yes (+fault matrix) | yes |

## Open findings and decisions

- CLI argument parsing reproduces the argparse surface (options in any
  position after the subcommand, `--opt=value`, repeated `append` options,
  choices, int/float types, required options, unique long-option prefixes).
  Argparse usage errors are reported as the JSON error envelope with exit 2
  instead of argparse's usage text (documented difference).

- Output: the reference `json.dumps(indent=2, ensure_ascii=True)` + LF on
  stdout, only after the operation (and its transaction) completed. Errors:
  the reference envelope `{"error": "..."}` (default separators) on stderr,
  exit 2; resource exhaustion exit 3. Python lets non-`FrontendError`/
  `OSError`/`ValueError` exceptions escape with a traceback (exit 1); native
  reports every exception through the envelope (exit 2). OS error texts use
  the native `filesystem_error` wording, not CPython's `[Errno N]` text.
- Ctrl-C: native sets a flag instead of raising. A running owned session
  treats it as cancellation (the reference `KeyboardInterrupt` path: stop the
  owned child, record `cancelled`); every other command finishes its bounded
  operation, so an interrupt never abandons a held writer lock mid-write.
- Test-only seams: `c2-frontend-native-fixture` (BUILD_TESTING only, never
  staged) is the production dispatcher with the labelled asset-free policy
  doubles that the Python tests patch in. `reference_cli.py --fixture-policy`
  installs the identical doubles in the reference. Production has no seam.

## Review record

| Review | Scope | Blocking findings | Disposition |
| --- | --- | --- | --- |
| R1 (separate context) | dispatcher `cli.cpp`/`main.cpp` at `1f7a059` | repeated-option checks, `Path('')`, discover positional, `--version`, unconsumed `--` | fixed `21969c9`, regression steps in `test_cli.py` |
| R2 (separate context) | workstream B `store_ops.cpp` at `fc78714` | B1 POSIX Path spelling (`./C:x` accepted), B2 `--json` duplicate keys | fixed `276bed2` (dispatcher normalization, `parse_last_wins`), parity steps added |
| R3 (separate context) | workstream C supervisor + acceptance at `4216a4d` | B1 spawn-failed text, B2 log error text, B3 Windows unsigned exit codes (+N4 post-exec failure) | fixed `fcb121f`, integrated `468679d` |
| R4 (separate context) | workstream A lifecycle at `9631d66` | B1 capability query ran in caller cwd (reference: fresh temp dir), B2 CLI Ctrl-C before launch still launched | fixed `31cb3a7` (fresh `c2-contract-*` dir; `Interrupted`, CLI exit 130), integrated `3962cd8`; re-reviewed in R5 |
| R5 final integrated review (separate context) | CLI -> prepare -> trust/capability -> owned run/cancel -> reconcile -> candidate -> accept/recover -> G0/G1/G2 -> Python-free cutover at `c1d4b88`, including both R4 fixes and every closure-session harness correction | B1 a terminal Ctrl-C (SIGINT to the whole foreground process group) killed the capability query during run preflight; native then recorded `failed` and exited 0 instead of leaving the session `prepared` and exiting 130 | fixed `5270af6` (a preflight failure seen with the interrupt flag set raises `Interrupted` without writing); POSIX process-group regression in `test_native_workflow.py`; the same reviewer re-verified the fix at `5270af6`: **clean** |

Accepted deviations from R3/R4 (nonblocking): native keeps draining and
counting after a log open/write failure (reference stops reading, its child
may stall); one 2 s log-finish bound for both pipes (reference 2 s each);
Windows terminates the job (descendants) after the run and uses
`CREATE_NO_WINDOW` like the probe runner; only SIGPIPE is reset in the child;
preview with `reconciliation: null` fails closed (reference AttributeError);
OSError texts in preview/diagnostics use native `filesystem_error` wording;
journal duplicate-key message lacks the key; NT overlap check uses
`fs::path` iteration (believed unreachable with canonical paths; not compiled
locally). Accepted helper restrictions from planning (explicit-path helper
lookup, 16 MiB helper output bound, Linux `close_range`/Windows only,
direct-child POSIX termination) carry into the CLI unchanged.

Accepted deviations from R5 (nonblocking):
- A terminal Ctrl-C during `native-observer`/`native-hunt prepare` kills the
  capability query. The command then exits 2 with "capability query failed",
  where the reference's KeyboardInterrupt exits 130. Nothing is written in
  either case.
- The native capability-query directory is created with mode 0777 minus the
  umask. Python's `mkdtemp` uses 0700. The directory is empty and removed on
  every path.
- On Windows, CTRL_BREAK kills the Python reference (0xC000013A) because
  CPython raises KeyboardInterrupt only for CTRL_C. The reference therefore
  cannot demonstrate cancellation there. The native workflow asserts
  cancellation on Windows, and the reference-mode harness asserts only the
  killed-supervisor outcome.
- OS-error diagnostics keep the native `filesystem_error` wording. The
  differential harness requires the OS text for the reference's errno or
  WinError code and the identical path.

Accepted, documented differences from R2 (nonblocking):
- `recover-backup` reads manifests through the safe-path policy: a
  symlinked/hard-linked or directory `lodge.json`/`.bak` is refused (the
  reference restores through it). Accepted hardening consistent with the
  contract's link/hardlink rule; recovery of such a store is manual.
- A managed import of a state below a subdirectory containing `:` (or `\` on
  POSIX) is refused by `write_blobs` before any snapshot is written (message
  `unsafe captured state path`); the reference first publishes an orphan
  snapshot, then refuses with `unsafe state member path`. Safer; text differs.
- Error texts: nonfinite encoding (`nonfinite journal number` vs CPython's
  `Out of range float values are not JSON compliant: nan`), `--json` decode
  errors, and `repr()` of store paths containing control characters or
  undecodable bytes. Outcomes and side effects match.
- `hunter archive` on a manifest without `active_hunter`: reference KeyError
  traceback (exit 1); native error envelope (exit 2).

## Verification evidence (exact SHAs)

- `9631d66` local: Debug 43/43; Release 43/43; Clang ASan/UBSan 13/13
  focused (sessions, session-process, session-journal, acceptance, store-ops,
  native-cli, native-workflow, probe); logs `~/.local/state/c2-runtime-completion/{asan,release}-9631d66.log`.
- `9631d66` native-only Release build (`BUILD_TESTING=OFF`,
  `CMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE`), staged with `cmake --install`:
  `test_native_workflow.py <stage>/bin ... --sandbox` passed in a bubblewrap
  namespace containing only the staged binaries, their libraries, the engine
  fixture and the test directory; negative control confirms the host Python
  and `/bin/sh` cannot execute there.
- `9631d66` CI run 36206718668: Linux success; Windows 55/58 — native
  workflow CTest **passed**, staged-Release Python-free workflow step
  **passed**, CLI/acceptance/reconcile passed. Failures (harness only):
  `frontend-native-sessions-prepare` / `-run` expect `python3.exe` where CI's
  interpreter is `python.exe` (A's harness builds the reference synthetic spec);
  `frontend-native-workflow-reference` exits `3221225786` (0xC000013A): the
  **Python reference** CLI is killed by CTRL_BREAK in the cancellation step
  (CPython turns only Ctrl-C into KeyboardInterrupt).
- `a080c7f` CI run 36206160098: Linux and Windows success (store ops +
  supervisor; before A/C merges).
- `468679d` local Debug 44/44; CI pending at handoff.
- `3962cd8` local Debug 44/44 (A reported ASan/UBSan clean on its session suites at `31cb3a7`); CI pending at handoff.
- Actual engine (compiled fixture results are separate): engine built from this
  branch (`/tmp/c2-runtime-engine/bin/Carnivores1_GL`, Linux SDL3/GL Release)
  answers the session contract. Against a disposable copy of the local Genesis
  Redux 1.1 install (revision = pinned `GENESIS_REVISION`) and a disposable
  store, the staged production binary performed register, import-copy,
  upgrade, **production-policy** `native-hunt plan` and `prepare` (real
  capability query), and a headless launch (`SDL_VIDEODRIVER=offscreen`, no
  display/audio): the engine loaded content, ran 30 s, was stopped by the
  owned-child timeout, exited 0, and the session was quarantined; preview
  refused it; authority stayed G0; the real install was never written.
  Evidence: `~/.local/state/c2-runtime-completion/genesis-smoke/`.
  **Not done (manual validation):** an interactive hunt that returns a clean
  candidate and its explicit acceptance with real Genesis gameplay.

## Closure session (2026-09-26)

Windows issues left by the previous coordinator, all resolved in the test
harness except B1:

| Issue | Finding | Fix |
| --- | --- | --- |
| `frontend-native-workflow-reference` | `2b2ea4c` did **not** work: run 36213683091 failed with `[WinError 87]`. The reference starts its child in the same console group, so CTRL_BREAK had already killed it. Once past that point, run 36216374114 failed in teardown: the killed supervisor's orphan survived because the reference has no kill-on-close job. | `b6ce3d2` and `c1d4b88`: reap the child if it is alive and prove it is gone, then assert the leftover lock and `process-ownership-lost` recovery |
| `frontend-native-sessions-prepare` | Same missing-directory condition (ENOENT / `ERROR_PATH_NOT_FOUND`) on the same path. Only the prose differed, and the old relaxation recognized only `[Errno N]`, accepting any non-empty text. | `6f3c908`: semantic comparison. The native text must contain the OS text for the reference code and the identical path, and the missing-work step pins the code, path and directory-iteration operation. No production change. |
| `frontend-native-sessions-run` cancellation | Reproduced twice with the same 243-byte difference, which is exactly one receipt line. A 150 ms timer, started before preflight, could stop the Python child before it printed. | `6f3c908`: the reference cancels once the receipt and stderr lines cross the drained pipes. Native `run-cancel-after-running` counts its delay from the running transition, and readiness is proven from the logs. `b44f0cc`: CRLF-aware markers. Logs compared exactly. |
| C++ standard | GCC 16 defaults to C++20, so the documented native-only configure failed to compile | `09766c6`: `CMAKE_CXX_STANDARD 17` unless overridden |
| R5 B1 | See the review record | `5270af6` |
| Parallel-test race | The capability-query leak check scanned the shared temp directory (a reference `mkdtemp` entry appeared mid-test in a local Release run) | `a0ea7cb`: private temp root, exact leak check, engine cwd proven |

## Final verification evidence (`a0ea7cb`)

Local logs are under `~/.local/state/c2-runtime-completion/`: `gates-a0ea7cb.txt`
and `*-a0ea7cb.log`.

| Gate | Result |
| --- | --- |
| Frontend CI run 36220220762, Linux (ubuntu-24.04) | success: ctest 44/44; native-only production build; staged Release workflow without Python OK |
| Frontend CI run 36220220762, Windows (windows-latest) | success: ctest 59/59, including workflow, workflow-reference and sessions prepare/run/reconcile; native-only production build; staged Release workflow without Python OK |
| Build and Test CI run 36220220752 | success |
| Local Debug (GCC 16.2.1, pinned CPython 3.12.14 oracles) | 44/44 |
| Local Release | 44/44, three consecutive parallel runs |
| Clang ASan+UBSan, focused (`halt_on_error`, leak detection) | 13/13, no sanitizer reports: probe-process, probe-deferred-reap, session-journal, sessions-prepare/run/reconcile, session-process, store-ops, acceptance, native-cli, native-workflow, workflow-reference, workflow-sandbox |
| Native-only production build | `BUILD_TESTING=OFF`, `CMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE`, Release, fresh configure with no explicit standard. Python is never looked up (no `Python3_EXECUTABLE`). `cmake --install` stages exactly `c2-frontend-native`, `c2-profile-probe` and `c2-frontend-synthetic-child`, linked only against libstdc++, libm, libgcc and libc. |
| Staged Python-free workflow | `test_native_workflow.py <stage>/bin` with Python shims passed. With `--sandbox`, it passed in a bubblewrap namespace with no interpreter, and the negative control confirmed that. |
| Native G0 -> G1 -> G2 | Covered by the staged and sandboxed workflow: G0 hunt, preview, accept to G1, continuation from G1, accept to G2, stale-generation refusal, and an idempotent G1 retry that never rewinds the head |
| Compatibility generators | `generate_{catalog_fixtures,compatibility,profile_unicode,schema_fixtures,schema_unicode}.py --check`: all exit 0 |

Earlier-SHA results in "Verification evidence" above remain history. They are
not attributed to the candidate.

**Actual Genesis engine smoke, rerun at `a0ea7cb`.** The engine is unchanged
since `9631d66` (no `Hunt`/`Shared` commits) and its binary matches the trusted
digest `43cb590c…c336`. The run used the staged production binary against the
disposable Genesis Redux 1.1 copy and store:

- The production-policy `native-hunt plan` validated the pinned revision
  (`huntdat-sha256-v1`, 344 files).
- `prepare` performed a real capability query and got contract v1.
- The headless launch (SDL offscreen) initialized OpenGL, loaded the session's
  trophy files and config, ran until the 30 s owned timeout, and exited 0.
- The journal went prepared, launching, running, returned, inspecting,
  quarantined.
- Preview refused the session ("candidate has unresolved failure/review
  conditions") and authority stayed at `caa43e6b…` (G0).
- The Genesis copy is byte-identical, the lock was released, and no
  query-directory leftovers remain. The real installation was never written.

Evidence: `genesis-smoke-a0ea7cb/`; the original `9631d66` run is in `genesis-smoke/`.

**Manual validation (not automated by design):** an interactive real-Genesis
hunt that returns a valid changed save, followed by explicit acceptance. Neither
gameplay nor production policy was altered to remove this step.

## Final verdict

**Production Python-to-C++ frontend/backend migration complete.** The frozen
candidate is `a0ea7cb`. `main` is not merged, and the Python reference
implementation is retained for differential testing.
