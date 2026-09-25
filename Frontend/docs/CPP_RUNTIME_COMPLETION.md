# C++ runtime completion (Python-free production frontend)

Maintained, resumable record for the runtime-completion sprint. Terms are kept
distinct: **implemented** (native library code exists), **wired** (reachable
through `c2-frontend-native`), **tested** (differential and/or end-to-end
tests pass at a recorded SHA), **reviewed** (separate reviewer context
inspected it), **merged** (never, in this sprint; `main` is not touched).

## Identity

- Main baseline: `a86be96faec2aacb4594d253bb9385091a0c2739` (untouched).
- Base: `frontend/cpp-planning-completion` at
  `38b398a04f7a139b94138c7ae0e58d661ebd19d4` (22 commits ahead of main; its
  tested code checkpoint is `75eff4b`, whose CI evidence is not re-attributed).
- Integration branch: `frontend/cpp-runtime-completion`, worktree
  `~/code/games/carnivores2-runtime-completion`.
- Baseline local Debug at `38b398a` (GCC 16.2.1, `-DCMAKE_CXX_STANDARD=17`,
  CPython 3.12.14): **33/33 CTests passed**.

## Canonical native executable

`c2-frontend-native` (unchanged target name) is the production frontend. It is
staged with `c2-profile-probe` (codec helper) and `c2-frontend-synthetic-child`
(compiled developer synthetic fixture). The former CMake custom target
`c2-frontend`, which only ran `python frontend.py --help`, is retained as
optional reference tooling behind `C2_FRONTEND_REFERENCE_TOOLS` (default: the
value of `BUILD_TESTING`). `python3 Frontend/frontend.py` remains the reference
CLI for differential testing, not the product.

## Workstreams

| Stream | Owner | Scope | Branch |
| --- | --- | --- | --- |
| A session lifecycle | worker A | 3B preparation, native-session adapters, run/recover state machine, 4C reconciliation, compiled synthetic child | `frontend/cpp-runtime-a-sessions` |
| B store operations | worker B | 5A/5B: hunters, host settings, register/relocate/refresh/discover-register, associate/import-copy, upgrade, recover-backup | `frontend/cpp-runtime-b-store` |
| C process + acceptance | worker C | 4A/4B owned child supervisor, then 6A–6C acceptance/recovery/failure matrix | `frontend/cpp-runtime-c-acceptance` |
| Coordinator | coordinator | shared interfaces, CMake/CI, CLI dispatcher, combined workflow tests, this record | integration branch |

Shared interfaces (settled before delegation): `session_policy.hpp`,
`sessions.hpp`, `session_process.hpp`, `session_runner.hpp`, `acceptance.hpp`,
`store_ops.hpp`, `store_write::sync_directory`. All values are the reference
dictionaries as `compat::Value`; no second JSON representation.

## Command / operation matrix

Columns: **Impl** native implementation (A/B/C = in progress in that
workstream); **Wired** dispatched by `c2-frontend-native` (`native/src/cli.cpp`;
"stub" = dispatcher wired to an explicit refusing stub in
`runtime_pending.cpp` until the workstream lands); **CLI diff** compared
against `frontend.py` on twin stores by `test_cli.py` (stdout, exit status,
error text, every store byte); **E2E** exercised by the native-only
`test_native_workflow.py` (expectations validated against Python with
`--reference`).

| Python command | Python entry | Native implementation | Impl | Wired | CLI diff | E2E |
| --- | --- | --- | --- | --- | --- | --- |
| `status`, `hunter list`, `expedition list`, `host-settings` | `Store.read` | `Manifest::export_json` | yes | yes | yes | pending |
| `managed-state inspect` | `inspect_history` | `resolve_generation().export_history_json()` | yes | yes | yes (refusal) | pending |
| `profiles` | `inventory`,`inspect_set` | `inventory_profiles`, `probe_process::inspect_set` | yes | yes | yes | pending |
| `catalog --instance/--path` | `catalog.project` | `catalog::project` | yes | yes | yes | pending |
| `refresh-state` | `refresh_association` | `planning_store::refresh_state` | yes | yes | yes | pending |
| `simulate-return` | `launch.simulated_return` | dispatcher + `refresh_association` | yes | yes | yes | — |
| `launch-dry-run` | `launch.prepare` | `planning_store::launch_dry_run` | yes | yes | yes | pending |
| `genesis-observer-plan` | `genesis.plan_observer` | `planning_store::plan_observer` | yes | yes | yes (real refusal + double) | — |
| `native-hunt plan` | `native_hunt.plan_hunt` | `planning_store::plan_hunt` | yes | yes | yes (real refusal + double) | pending |
| `session inspect`, `native-hunt inspect` | `read_journal` | `session_journal::read` | yes | yes | pending | pending |
| `hunter create/select/rename/archive` | `store.hunter` | `store_ops::hunter` | B | stub | pending | pending |
| `host-settings --json` | inline | `store_ops::update_host_settings` | B | stub | pending | pending |
| `recover-backup` | `Store.restore_backup` | `store_ops::restore_backup` | B | stub | pending | — |
| `expedition discover [--register-managed]` | `discover`,`register` | `store_ops::discover_view/discover_register` | B | stub | pending | pending |
| `expedition register/relocate/refresh` | `discovery.*` | `store_ops::*` | B | stub | pending | pending |
| `associate [--import-copy]` | `profiles.associate` | `store_ops::associate` | B | stub | pending | pending |
| `managed-state upgrade` | `upgrade_store` | `store_ops::upgrade_store` | B | stub | pending | pending |
| `session prepare-synthetic` | `sessions.prepare_session` | `sessions::prepare_session` | A | stub | pending | pending |
| `session run/reconcile/recover` | `session_runner`, `reconciliation` | `session_runner::*`, `reconciliation::*` | A (+C process) | stub | pending | pending |
| `native-observer prepare/run` | `native_observer.*` | `native_session::prepare`, `run_native` | A | stub | pending | pending |
| `native-hunt prepare/run` | `native_hunt.*` | `native_session::prepare`, `run_native` | A | stub | pending | pending |
| `managed-state preview/accept/recover-acceptance` | `acceptance.*` | `acceptance::*` | C | stub | pending | pending |

Global options: `--store` (default `$LOCALAPPDATA` or `~/.local/share`, then
`carnivores-lodge`, as the reference) and `--probe` (explicit codec helper;
otherwise `C2_PROFILE_PROBE`; never auto-discovered, as the reference).

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

## Next actionable task

Workers A/B/C implement their streams; coordinator wires the existing
library-complete commands into the native dispatcher.
