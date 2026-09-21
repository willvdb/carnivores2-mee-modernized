# C++ migration implementation sprint — handoff (UNREVIEWED DRAFT)

Everything on this branch is unreviewed draft work. Passing tests imply no
approval and no merge. Nothing was merged; `main` was not changed.

## Identity

- Start: `origin/main` `a86be96faec2aacb4594d253bb9385091a0c2739` (PR #27, 3A).
- Branch: `frontend/cpp-session-sprint`, worktree
  `~/code/games/carnivores2-session-sprint`. No draft PR was opened.
- Final code commit: `469c7fbef0485956a632cb6f13a4c4cefd697229`; this handoff is
  the commit after it. Last compiling and fully tested commit: `469c7fb`.
- Sprint date 2026-09-21, one Fable implementer in the main thread. No second
  implementer, no separate agent branch, nothing left unintegrated.
- Owned processes: none running. The isolated build directory was in the
  session scratchpad and is disposable.

## Commits in review order (each depends on those above it)

1. `01c72ef` Ledger current-state correction (docs only).
2. `7eacc18` `probe_process`: shell-free probe runner, `codec_inspect`,
   `executable_evidence`, `codec_evidence` (pulled-forward sliver of 4A).
3. `eea6e7e` `session_journal`: validate/encode/persist/read/transition (3B).
   Independent of 2; depends only on 3A.
4. `3e0a0e9` PR #26 follow-ups 1 and 3 (oracle `unexpected` assertion, ordinal
   guard before the launch mask shift).
5. `ccab5fb` Helper-backed `inspect_set` and `inspect_bytes` (needs 2).
6. `0cc8bf8` `planning_store::refresh_association` / `refresh_state` (needs 5, 3A).
7. `98d8b4b` `planning_store::launch_dry_run` (needs 6; PR #26 follow-up 2).
8. `469c7fb` PR #26 follow-up 4 (typed API limits, comment only).

## Implemented

- Probe execution on POSIX: explicit argv via `fork`/`execv`, CLOEXEC pipes,
  all three pipes serviced in one `poll` loop, deadline, SIGKILL-and-reap on
  every exit path including exceptions, thread-local SIGPIPE blocking, exec
  failure reported through a CLOEXEC error pipe as `filesystem_error`.
  Reference messages `codec helper timed out` and `codec helper failed (N)`
  with `-signal` return codes.
- Journal component for historical versions 1–4 in the reference check and
  message order; byte-identical encoding; 4 MiB bound; failed persistence
  leaves the caller's journal and the file unchanged.
- `refresh-state` and `launch-dry-run` library operations, differentially
  tested for identical results and identical `lodge.json` bytes.
- All four PR #26 follow-ups.

## Partially implemented

- **Windows probe runner is written but has never been compiled or run.**
  It uses `CreateProcessW` with `list2cmdline` quoting, a restricted inherited
  handle list, a kill-on-close job object and one thread per pipe. Treat it as
  untested code until Windows CI builds it.
- `refresh_association` refuses a `managed-state-history` association with
  `planning_store::NotImplemented` (explicit refusal, writes nothing).
  Generation resolution over the mutable manifest value is not wired.
- 2C.2 is partial: `launch-dry-run` and the observation write exist.

## Not started

- `snapshot_pins`, and therefore the `genesis-observer-plan` and
  `native-hunt plan` wrappers.
- 3B workspace preparation (`prepare_session`, native preparation and its
  trusted-engine capability gate), evidence pins, `execution_spec`.
- CLI exposure of anything added here. 4A beyond the probe runner.

## Tests run (Linux x86-64, GCC, `-DCMAKE_CXX_STANDARD=17`, CPython 3.12.14, Debug)

- `ctest -j8` at `469c7fb`: 31 of 32 passed. `frontend-native-catalog-projection`
  failed once in that parallel run and passed when rerun alone (17 s). The
  failure output was overwritten before it was read, so the cause is
  **undiagnosed**; this sprint did not touch catalog code. Check for a
  load-sensitive flake before trusting either result.
- New suites, all passing: `frontend-native-probe-process` (15 tests),
  `frontend-native-session-journal` (9 tests, more than 150 refused mutations
  compared with the reference), `frontend-native-planning-store` (4 tests).
- Not tested: Windows entirely; macOS; MSVC warnings; the POSIX non-Linux
  `sigwait` branch; helper script tests are skipped on Windows by design.
- The managed-copy refresh test accepts either authority outcome; which
  branch ran on this store schema was not recorded.

## Review-sensitive decisions and known defects

- The probe path is executed as given with `execv`, without PATH search. The
  reference `subprocess.run` would search PATH for a bare name.
- Native-only 16 MiB bound on helper stdout/stderr (`OutputLimit`); the
  reference is unbounded. Overflow kills the child and throws; never truncates.
- The runner kills only the direct child on POSIX, as the reference does. A
  helper's grandchildren are not tracked.
- A helper result that is not a JSON object is returned unchanged by
  `codec_inspect`; `inspect_set`/`inspect_bytes` then throw `invalid_argument`.
- `session_journal::read` reproduces the reference prefix
  `cannot read session journal: ` with a native detail text, and the
  duplicate-key refusal without the key name, matching the existing native
  manifest reader rather than Python's exact text.
- Non-finite journal floats map to `StoreError` (3A convention) where the
  reference raises a bare `ValueError`.
- `transition` throws `invalid_argument` for an unknown current state where
  the reference raises `KeyError`.
- `launch_dry_run` re-reads the manifest through `Store::read()` inside the
  transaction callback to obtain a typed `Manifest`. This is consistent only
  because the writer lock is held; review that assumption.
- `narrow_ascii` replaces non-ASCII kind/dialect code points with `?` before
  comparison. Validated manifests cannot contain such hints; confirm.

## Inherited gaps (unchanged by this sprint)

- The merged 3A head did not receive routed Fable re-review of its final
  Windows lock-link correction rounds.
- 3A follow-ups still open: Python-side Windows safety fixes, lone-surrogate
  path policy, the older copyable handle in `store_paths`, clustered-Windows
  hostname behaviour. No ownership defect from that handle was observed in
  the new paths, and none was looked for beyond the tests above.

## Next implementation task

Native `snapshot_pins` over `inspect_instance`, `catalog::project`, generation
resolution, `capture`, `codec_evidence` (with the `expected_codec` pin check
before any helper execution) and `inspect_bytes`; then the two remaining
2C.2 wrappers, then 3B preparation on top of `session_journal::persist` and
`store_write::write_blobs`.
