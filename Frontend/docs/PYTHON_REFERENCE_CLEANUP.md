# Python reference source isolation

## Baseline and scope

Baseline: `47ec204f1a0f2ca071700ae18b6070a5a40a110c`, the current head of
open documentation prerequisite [PR #30](https://github.com/willvdb/carnivores2-mee-modernized/pull/30).
`origin/main` was `37762ccab4afbdcc6f7f6b93d5f228b901ff6f69`.
Cleanup branch: `cleanup/isolate-python-reference`; chosen PR base:
`cleanup/native-frontend-entrypoint-docs`. No merge or branch deletion is part
of this work. The existing main worktree and its untracked logs were untouched.

Implementation commits: `ddf9d72` (shared fixture extraction), `8430b2c`
(reference relocation). The validation below tests code SHA
`8430b2cbe3210ddf014ce6b9ae7f5c0795a0683d`; subsequent handoff changes are docs only.

## Classification and path map

Paths below are relative to `Frontend/`.

| Class | Previous location | Final location / disposition |
| --- | --- | --- |
| A: production C++ | `native/{include,src}/`, `profile_probe.cpp` | Unchanged, including all four production target names. |
| B: reference implementation | `frontend.py`, `lodge/` | `reference/python/frontend.py`, `reference/python/lodge/`; byte-identical moves. |
| C: reference tests | `tests/test_*.py`, `run_tests.py` | `reference/python/tests/test_*.py`, `reference/python/run_tests.py`. |
| D: shared fixture builders | `tests/support.py`, definitions in `test_profiles.py`, `test_launch.py`, `test_genesis_hunt.py` | `tests/c2_test_support/__init__.py`. |
| D: shared session setup | `test_sessions.py` setup/helpers | `tests/c2_test_support/session_fixture.py`. |
| D: golden data / compiled helpers | `tests/{catalog,compatibility}/`, `tests/*.cpp` | Unchanged; these remain shared verification resources. |
| E: harnesses / generators | `native/tests/`, `tools/` | Retained, reconnected through `tools/c2_reference_paths.py`. |
| F: proven dead code | None identified | No implementation, regression test, generator or golden file discarded. |

`tests/support.py` is the only non-rename file removal: its entire `game`
builder moved verbatim into the distinctly named shared support package and
all consumers were updated. It was active code, not dead code. No wrapper,
duplicate source, symlink or old-location compatibility alias remains.

Extracted definitions retain their construction behavior: `game`, `save_bytes`,
`room_bytes`, `SCRIPT`, `selection`, `capture_native`, and the session fixture's
`setUp`, `prepare`, `assert_sources_untouched`. Reference tests and native CLI,
planning-store, store-ops, sessions, spawn and acceptance harnesses consume them.
`SessionFixture` retains unittest cleanup/assertion support but contains no test
methods. Native harnesses no longer import reference test modules.

The optional help target changed from **`c2-frontend` to
`c2-frontend-reference`**. It still only shows reference CLI help.
`BUILD_TESTING` / `C2_FRONTEND_REFERENCE_TOOLS` defaults and all test
registrations are preserved. The CTest name `frontend-backend` is intentionally
retained for the reference suite. Its explicit runner now sets its own import
paths; the restart child takes its reference path explicitly, rather than
requiring inherited `PYTHONPATH`. The store-write lock-holder child likewise
receives its explicit reference path; its original CWD and binary control
channel are unchanged.

Active README, CMake, CI and adapter instructions use the new paths. Historical
examples in `PYTHON_REFERENCE.md`, `docs/CPP_MIGRATION.md`,
`docs/CPP_RUNTIME_COMPLETION.md` and `docs/HANDOFF.md` retain their original
paths/target names with relocation notices. There are no active old-path
callers. The root-level Genesis play-loop plan's source pointer was updated.

## Local validation

Host: CachyOS Linux, kernel 7.2.6-1-cachyos, x86_64; GCC 16.2.1 (20260810),
CMake 4.4.3, CPython **3.12.14**, Unicode 15.0.0. No substitute Python version
or third-party Python dependency was used. Raw logs stay outside the repository.

Use fresh directories and an absolute CPython 3.12 interpreter path:

```sh
cmake -S Frontend -B "$DEBUG_BUILD" -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DPython3_EXECUTABLE="$PYTHON312"
cmake --build "$DEBUG_BUILD" --parallel 2
ctest --test-dir "$DEBUG_BUILD" --show-only=json-v1
ctest --test-dir "$DEBUG_BUILD" -C Debug -V -j 2 --no-tests=error
# Repeat in another fresh directory with Release for full Release verification.

cmake -S Frontend -B "$NATIVE_BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DC2_FRONTEND_REFERENCE_TOOLS=OFF \
  -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE
cmake --build "$NATIVE_BUILD" --config Release --parallel 2
cmake --install "$NATIVE_BUILD" --config Release --prefix "$STAGE"
"$STAGE/bin/c2-frontend-native" --help
"$PYTHON312" Frontend/native/tests/test_native_workflow.py "$STAGE/bin" \
  "$DEBUG_BUILD/c2-frontend-native-fixture" "$DEBUG_BUILD/c2-native-session-fixture"
# Repeat the workflow with --sandbox on Linux with working bubblewrap.
```

Baseline Debug: **45/45 CTests passed**, no pre-existing failures. The first
post-relocation Debug/Release runs exposed a new import failure in both
store-write variants: the lock-holder child still relied on `cwd=Frontend`.
It was fixed in the relocation commit, both affected tests passed, and full
Debug/Release suites were rerun. Original failure logs were retained separately.
Fixture
extraction: all nine affected CTests passed. Final Debug and Release:
**45/45 each**, identical CTest names/order and unittest counts. No registrations,
assertions, test methods, expected outputs or capability guards were removed.

Reported unittest counts, identical before/after (other CTests use standalone
assertion harnesses):

| Suite | Cases |
| --- | ---: |
| Reference `frontend-backend` | 154 |
| Probe process / deferred reap | 25 / 1 |
| Session journal / planning store | 9 / 18 |
| Native CLI | 7 |
| Native workflow / reference workflow / sandbox workflow | 2 / 2 / 2 |
| Sessions prepare / run / reconcile | 12 / 12 / 11 |
| Store operations / session process / acceptance / spawn | 34 / 19 / 7 / 1 |

No local CTest capability skip occurred. Both ordinary workflow variants skip
one unittest, `test_sandbox_contains_no_python` (reason: `sandbox negative
control`); that test runs and passes in the sandbox variant. These are the same
baseline skips. Local permissions and bubblewrap capabilities were available.

All five `tools/generate_*.py --check` commands passed. Golden JSON, Unicode
`.inc` files, production C++ and compiled helper sources are unchanged.
The reference CLI/help, renamed CMake help target, corpus inspection tool and
direct reference-suite entry point all passed. A separate archive of the tested
commit under a source path containing spaces ran the generator checks, both
reference CLI entry points and all 154 reference tests from an unrelated CWD,
with `PYTHONPATH` unset. Import checks located every `lodge` module and the
synthetic child inside that source copy, and loaded no reference test modules
through the shared bootstrap. Build/install paths also contained spaces.

The native-only cache has no `Python3_EXECUTABLE`; staging contains exactly
`c2-frontend-native`, `c2-profile-probe`, `c2-frontend-synthetic-child` in `bin/`.
The staged workflow passed with failing Python shims and with an interpreter-free
bubblewrap namespace, including the negative control. Python drives these tests;
it is not a dependency of the product being tested. No engine/gameplay test or
real game assets were needed for this source-only cleanup.

## Review and cross-platform handoff

A fresh read-only reviewer inspected the actual baseline-to-branch diff. Its
initial review missed the lock-holder child import found by the full test gate.
After that fix, it rechecked all five inline child scripts importing `lodge`
and all script-based launch sites, reporting **no remaining actionable findings**.
It independently checked
fixture equivalence, 154 distinct discovered test IDs, native execution paths,
imports, child paths, generator destinations and the production boundary.
Windows was statically reviewed locally, not executed locally.

The Linux/Windows CI matrix is preserved. Baseline Frontend run `36373849348`
passed 45 Linux registrations (sandbox CTest skipped on that runner) and 60
Windows registrations. Published-head results belong to the cleanup PR's checks,
not those baseline results; the PR handoff records their exact tested SHA and
any remaining pending/skipped checks. Review and merge the documentation
prerequisite before merging the cleanup. No GUI work or production behavior
change is included.
