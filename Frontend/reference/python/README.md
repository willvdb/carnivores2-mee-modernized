# Python reference implementation

This is the independent reference backend and CLI retained for differential
verification of the production C++ frontend. It is not the product, is never
installed, and is not the place to implement future GUI or backend features.
See the [native frontend guide](../../README.md) for production development.
No third-party Python packages or global `lodge` installation are needed.

From the repository root (or use absolute script paths from any directory):

```sh
python3.12 Frontend/reference/python/frontend.py --help
cmake --build build/frontend-tests --target c2-frontend-reference
ctest --test-dir build/frontend-tests -C Debug -R '^frontend-backend$' --output-on-failure
```

Configure and build the verification targets first as described in the native
guide. CTest supplies all three compiled helpers and runs every reference test.
For direct execution on a single-configuration build:

```sh
C2_PROFILE_PROBE="$PWD/build/frontend-tests/c2-profile-probe" \
C2_LAUNCH_ARGUMENT_PROBE="$PWD/build/frontend-tests/c2-launch-argument-probe" \
C2_NATIVE_TEST_ENGINE="$PWD/build/frontend-tests/c2-native-session-fixture" \
python3.12 Frontend/reference/python/run_tests.py
```

On Windows use environment-variable syntax for your shell and absolute helper
paths under the selected configuration (e.g. `Debug/`, with `.exe`). The runner
requires the real codec probe and rejects codec/probe skips. Use CPython 3.12
for the full verification suite's pinned Unicode 15.0.0 behavior; the CLI alone
supports Python 3.10+. Test discovery is owned by `run_tests.py`; native harnesses
use shared fixtures in `../../tests/c2_test_support/`, not these test modules.

The [historical walkthrough](../../PYTHON_REFERENCE.md) records prototype usage
and earlier validation limits. In its older command examples, substitute
`Frontend/reference/python/frontend.py` for `Frontend/frontend.py`.
