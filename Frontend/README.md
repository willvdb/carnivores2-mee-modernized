# Carnivores native frontend backend

`c2-frontend-native` is the production C++ frontend CLI. Its backend lives in
`native/` and builds independently of the game executable and the legacy Win32
`Menu/`. The lodge and Expedition Console GUI are still future presentation
layers; this directory does not yet provide that GUI.

The Python implementation is retained for reference, differential tests and
developer tools, **not as a production runtime dependency**. The former
Python-first README is preserved verbatim in [PYTHON_REFERENCE.md](PYTHON_REFERENCE.md).
That walkthrough describes the reference prototype and historical validation
gates; it is not the current native build or release guide.

## Build the native frontend without Python

From the repository root, with CMake 3.20+ and a C++17 compiler:

```sh
cmake -S Frontend -B build/frontend-native-only \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE
cmake --build build/frontend-native-only --config Release --parallel 2
cmake --install build/frontend-native-only --config Release --prefix build/frontend-stage
build/frontend-stage/bin/c2-frontend-native --help
```

On Windows, use a suitable C++ build environment and append `.exe` to executable
names. The configure command is shown with POSIX-shell line continuations; use
one line or your shell's continuation syntax elsewhere.

The install stages three native executables together in `bin/`:

- `c2-frontend-native`: the frontend CLI.
- `c2-profile-probe`: the native profile-codec helper.
- `c2-frontend-synthetic-child`: the compiled child for controlled developer
  synthetic sessions.

Python source and test drivers are not installed. Keep the helpers beside the
frontend; the synthetic-session path locates its child there. For commands that
require profile inspection, provide the probe explicitly, for example:

```sh
build/frontend-stage/bin/c2-frontend-native \
  --store /path/to/disposable-lodge \
  --probe build/frontend-stage/bin/c2-profile-probe status
```

Native command availability does not certify arbitrary game content for launch.
Existing executable-trust, content-policy, ownership, reconciliation and explicit
acceptance gates still apply. No original game assets are included.

## Build and run the verification suite

The complete verification suite uses CPython 3.12 / Unicode 15.0.0, including the
checked-in Unicode-table oracles. The Python reference CLI itself supports
Python 3.10+, but that is not the full-suite verification baseline. No third-party
Python packages are required.

Use a separate build directory so the test configuration cannot inherit the
Python-disabled production cache:

```sh
cmake -S Frontend -B build/frontend-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/frontend-tests --config Debug --parallel 2
ctest --test-dir build/frontend-tests -C Debug --output-on-failure --no-tests=error
```

The [frontend CI workflow](../.github/workflows/frontend.yml) exercises both the
Python-disabled Release install and the Debug verification suite on Linux and
Windows. It also runs the staged native workflow with failing Python shims on
its child-process PATH. Python remains the *test driver*, not a dependency of the
installed frontend.

`C2_FRONTEND_REFERENCE_TOOLS` controls the optional `c2-frontend` CMake helper
target, which only displays the Python reference CLI's help. It defaults to the
value of `BUILD_TESTING`. It is not the native executable; the native target is
`c2-frontend-native`.

## Source and reference boundaries

| Location | Role |
| --- | --- |
| `native/include/`, `native/src/` | C++ backend and CLI; `c2_frontend_core` is the backend library target. |
| `native/tests/` | Native test drivers, Python harnesses and differential comparisons. |
| `frontend.py`, `lodge/` | Retained Python reference CLI and implementation. |
| `tests/`, `run_tests.py` | Reference regression suite, shared fixtures and native test helpers. |
| `tools/` | Fixture/Unicode generators and developer corpus inspection. |

Do not remove the Python implementation merely because the runtime migration is
complete. In particular:

- `native/tests/reference_cli.py` imports `frontend.py`, and the CLI/workflow
  suites compare native behavior against that reference.
- Native tests import `lodge` modules and fixtures from `tests/`, including
  `support.py`, `test_launch.py` and `test_profiles.py`.
- CTest runs `run_tests.py` and the `tools/generate_*.py --check` oracles; compiled
  native test helpers also live under `tests/`.

A future relocation into a clearly named reference directory must update the
imports, subprocess paths, CMake registrations, generators and documentation
together, while preserving the independent expected results and test coverage.
Shared fixtures must not be deleted with an old test directory. Keep production
C++/GUI development on the native boundary rather than extending the reference
implementation as product code.

## Contracts and implementation records

- [Runtime completion](docs/CPP_RUNTIME_COMPLETION.md): native command coverage,
  recorded validation evidence and accepted differences. Its branch/merge status
  and SHAs are a historical sprint record, not a live report of `main`.
- [Play-loop boundary](docs/PLAY_LOOP_HANDOFF.md): operation and presentation
  handoff context.
- [State model](docs/STATE_MODEL.md), [session model](docs/SESSION_MODEL.md) and
  [managed state](docs/MANAGED_STATE.md): ownership and persistence contracts.
- [Engine session seam](docs/ENGINE_SESSION_SEAM.md): engine/frontend process
  boundary and its design history.
- [Python reference walkthrough](PYTHON_REFERENCE.md): retained prototype usage
  and historical notes, separate from the native instructions above.
