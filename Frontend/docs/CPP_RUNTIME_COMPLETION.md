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

Status columns: Impl = native implementation, Wired = native CLI, Diff =
differential test against unchanged Python, E2E = native-only workflow test.

| Python command | Python entry | Native implementation | Impl | Wired | Diff | E2E |
| --- | --- | --- | --- | --- | --- | --- |
| `status` | `Store.read` | `Store::read`, `export_json(status)` | yes | yes | yes | pending |
| `hunter list` | `Store.read` | `export_json(hunters)` | yes | yes | yes | pending |
| `expedition list` | `Store.read` | `export_json(expeditions)` | yes | yes | yes | pending |
| `host-settings` (read) | `Store.read` | `export_json(host_settings)` | yes | yes | yes | pending |
| `managed-state inspect` | `inspect_history` | `resolve_generation().export_history_json()` | yes | yes | yes | pending |
| `hunter create/select/rename/archive` | `store.hunter` | `store_ops::hunter` | B | pending | pending | pending |
| `host-settings --json` | inline | `store_ops::update_host_settings` | B | pending | pending | pending |
| `recover-backup` | `Store.restore_backup` | `store_ops::restore_backup` | B | pending | pending | pending |
| `expedition discover` | `discover`,`move_candidates` | `store_ops::discover_view` | B | pending | pending | pending |
| `expedition discover --register-managed` | `register` | `store_ops::discover_register` | B | pending | pending | pending |
| `expedition register/relocate/refresh` | `discovery.*` | `store_ops::*` | B | pending | pending | pending |
| `profiles` | `inspect_set`,`inventory` | `probe_process::inspect_set` | yes | pending | yes (lib) | pending |
| `associate [--import-copy]` | `profiles.associate` | `store_ops::associate` | B | pending | pending | pending |
| `refresh-state` | `refresh_association` | `planning_store::refresh_state` | yes | pending | yes (lib) | pending |
| `catalog --instance/--path` | `catalog.project` | `catalog::project` | yes | pending | yes (lib) | pending |
| `launch-dry-run` | `launch.prepare` | `planning_store::launch_dry_run` | yes | pending | yes (lib) | pending |
| `simulate-return` | `launch.simulated_return` | coordinator | pending | pending | pending | pending |
| `genesis-observer-plan` | `genesis.plan_observer` | `planning_store::plan_observer` | yes | pending | yes (lib) | pending |
| `native-hunt plan` | `native_hunt.plan_hunt` | `planning_store::plan_hunt` | yes | pending | yes (lib) | pending |
| `session prepare-synthetic` | `sessions.prepare_session` | `sessions::prepare_session` | A | pending | pending | pending |
| `session inspect` / `native-hunt inspect` | `read_journal` | `session_journal::read` | yes | pending | yes (lib) | pending |
| `session run` (+auto reconcile) | `run_session` | `session_runner::run_session` | A | pending | pending | pending |
| `session reconcile` | `reconcile_session` | `reconciliation::reconcile_session` | A | pending | pending | pending |
| `session recover` | `recover_session` | `session_runner::recover_session` | A | pending | pending | pending |
| `native-observer prepare/run` | `native_observer.*` | `native_session::prepare`, `run_native` | A | pending | pending | pending |
| `native-hunt prepare/run` | `native_hunt.*` | `native_session::prepare`, `run_native` | A | pending | pending | pending |
| `managed-state upgrade` | `upgrade_store` | `store_ops::upgrade_store` | B | pending | pending | pending |
| `managed-state preview/accept/recover-acceptance` | `acceptance.*` | `acceptance::*` | C | pending | pending | pending |

## Open findings and decisions

- CLI argument parsing reproduces the argparse surface (options in any
  position after the subcommand, `--opt=value`, repeated `append` options,
  choices, int/float types, required options, unique long-option prefixes).
  Argparse usage errors are reported as the JSON error envelope with exit 2
  instead of argparse's usage text (documented difference).

## Next actionable task

Workers A/B/C implement their streams; coordinator wires the existing
library-complete commands into the native dispatcher.
