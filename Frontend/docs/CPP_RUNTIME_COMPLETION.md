# C++ runtime completion (Python-free production frontend)

Maintained, resumable record for the runtime-completion sprint. Terms are kept
distinct: **implemented** (native library code exists), **wired** (reachable
through `c2-frontend-native`), **tested** (differential and/or end-to-end
tests pass at a recorded SHA), **reviewed** (separate reviewer context
inspected it), **merged** (never, in this sprint; `main` is not touched).

## Identity (live state at handoff, 2026-09-25)

- Main: `a86be96faec2aacb4594d253bb9385091a0c2739` — untouched, nothing merged.
- Base: `frontend/cpp-planning-completion` `38b398a` (its tested code SHA
  `75eff4b`; its CI evidence is not re-attributed here).
- Integration branch `frontend/cpp-runtime-completion`, worktree
  `~/code/games/carnivores2-runtime-completion`. Last code checkpoint:
  **`3962cd8`** (merge of A's R4 fixes; this documentation commit follows it).
- Worker branches (worktrees `~/code/games/carnivores2-runtime-{a-sessions,b-store,c-acceptance}`):
  - B `frontend/cpp-runtime-b-store` `c34678a` — fully integrated.
  - C `frontend/cpp-runtime-c-acceptance` `fcb121f` — fully integrated (merge `468679d`).
  - A `frontend/cpp-runtime-a-sessions` `31cb3a7` — fully integrated (merge `3962cd8`).
- `runtime_pending.cpp` is **deleted** (commit after merge of A); no stubs remain.

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
| R4 (separate context) | workstream A lifecycle at `9631d66` | B1 capability query ran in caller cwd (reference: fresh temp dir), B2 CLI Ctrl-C before launch still launched | fixed `31cb3a7` (fresh `c2-contract-*` dir; `Interrupted`, CLI exit 130), integrated `3962cd8`; fixes not re-reviewed |

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

## Next actionable task

Frontend CI at `7071068` (run 36208984040): Linux green; Windows 56/59 with
three failures. `2b2ea4c` fixes the first; the other two are still open.

1. **Fixed in `2b2ea4c` (harness only):** `frontend-native-workflow-reference`
   now accepts CPython's CTRL_BREAK exit 0xC000013A on Windows, then recovers
   the session. The native workflow still asserts cancellation on every platform.
2. **Open, fails the same way every run:** `frontend-native-sessions-prepare`
   `test_workspace_findings_match_reference`. On Windows, the native finding for
   a missing directory says `directory_iterator::directory_iterator: ...`, but the
   reference says `[WinError 3] The system cannot find the ...`. This is a
   difference in diagnostic text. Decide whether it is a production parity
   defect (format native OS errors like the reference) or an accepted
   platform-text difference (compare code/path only on Windows).
3. **Open, seen once (run 36208984040):**
   `frontend-native-sessions-run` `test_synthetic_cancellation_stops_the_owned_child`.
   The log `total_bytes` differ by 243 against a tolerance of 4. The likely
   cause is the 150 ms cancel racing child startup on Windows. Confirm by
   rerunning, then fix the harness timing without weakening the
   cancellation assertions.
4. Freeze a code SHA. At that exact SHA, get green Linux and Windows CI, a
   Python-disabled build, the staged Python-free workflow, a Release build and
   local ASan/UBSan, and the native G0->G1->G2 E2E.
5. Run one final separate review of the integrated path: CLI -> prepare ->
   trust/capability -> owned run/cancel -> reconcile -> candidate ->
   accept/recover -> G0/G1/G2 -> Python-free cutover. Include the R4 fixes: the
   capability query uses a fresh temporary cwd, and a pre-launch interrupt
   exits 130 without launching.
6. Record the verdict. An interactive real-Genesis hunt plus acceptance
   remains manual validation.
