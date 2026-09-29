# Carnivores native frontend backend

`c2-frontend-native` is the production C++ frontend CLI. Its backend lives in
`native/` and builds independently of the game executable and the legacy Win32
`Menu/`. An opt-in RmlUi evaluation GUI (`c2-frontend-gui`, `C2_FRONTEND_GUI=ON`)
lives under `gui/`; it now connects the Expedition Console to the backend play loop.
See [docs/HUNT_LOOP_GUI.md](docs/HUNT_LOOP_GUI.md) for write opt-in, trust gates,
asset-free demo limits and verification evidence.
The Console supports multiple licenses/weapons and the four pinned equipment
entries; map/time remain single-select. See [Genesis policy and session compatibility](docs/GENESIS_HUNT.md).
See [docs/RMLUI_EVALUATION.md](docs/RMLUI_EVALUATION.md).

The Python implementation is retained for reference, differential tests and
developer tools, **not as a production runtime dependency**. The historical
Python-first walkthrough is retained in [PYTHON_REFERENCE.md](PYTHON_REFERENCE.md).
That walkthrough describes the reference prototype and historical validation
gates; it is not the current native build or release guide.

## Build the native frontend without Python

From the repository root, with CMake 3.20+ and a C++17 compiler:

```sh
cmake -S Frontend -B build/frontend-native-only \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DC2_FRONTEND_REFERENCE_TOOLS=OFF \
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
cmake -S Frontend -B build/frontend-tests -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DPython3_EXECUTABLE=/absolute/path/to/python3.12
cmake --build build/frontend-tests --config Debug --parallel 2
ctest --test-dir build/frontend-tests -C Debug --output-on-failure --no-tests=error
```

The [frontend CI workflow](../.github/workflows/frontend.yml) exercises both the
Python-disabled Release install and the Debug verification suite on Linux and
Windows. It also runs the staged native workflow with failing Python shims on
its child-process PATH. Python remains the *test driver*, not a dependency of the
installed frontend.

`C2_FRONTEND_REFERENCE_TOOLS` controls the optional `c2-frontend-reference` CMake helper
target, which only displays the Python reference CLI's help. It defaults to the
value of `BUILD_TESTING`. It is not the native executable; the native target is
`c2-frontend-native`.

## Source and reference boundaries

| Location | Role |
| --- | --- |
| `native/include/`, `native/src/` | C++ backend and CLI; `c2_frontend_core` is the backend library target. |
| `native/tests/` | Native test drivers, Python harnesses and differential comparisons. |
| `reference/python/frontend.py`, `reference/python/lodge/` | Independent Python reference CLI and backend; not installed. |
| `reference/python/tests/`, `reference/python/run_tests.py` | Reference regression suite and its entry point. |
| `tests/c2_test_support/` | Shared authored fixture builders and disposable session setup. |
| `tests/catalog/`, `tests/compatibility/`, `tests/*.cpp` | Golden data and compiled helpers used by verification. |
| `tools/` | Fixture/Unicode generators, shared import bootstrap and developer corpus inspection. |

The [Python reference guide](reference/python/README.md) documents its supported
entry points. Native comparisons import the reference implementation and shared
fixtures, never reference test modules. `tools/c2_reference_paths.py` selects the
reference and shared support from this checkout, without global installation or
an externally configured `PYTHONPATH`. The full CTest suite runs the reference
regressions and all five generator `--check` oracles by default.

The optional help target was renamed from `c2-frontend` to
`c2-frontend-reference`; no old-target alias is retained. Production target names
are unchanged. Future GUI and backend features belong on the native boundary.
See the [cleanup record](docs/PYTHON_REFERENCE_CLEANUP.md) for the path map and
validation evidence.

## Optional Expedition Console GUI

`gui/` holds a headless presentation model (always built; tests run in the
ordinary configuration above) and, behind `C2_FRONTEND_GUI=ON`, the RmlUi +
SDL3 + OpenGL executable `c2-frontend-gui`. With the option off nothing
graphical is fetched or linked. The current play-loop flow and verification are
in [docs/HUNT_LOOP_GUI.md](docs/HUNT_LOOP_GUI.md); historical toolkit evaluation
is in [docs/RMLUI_EVALUATION.md](docs/RMLUI_EVALUATION.md). The artwork contract is
[docs/ASSET_BRIEF.md](docs/ASSET_BRIEF.md).

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
