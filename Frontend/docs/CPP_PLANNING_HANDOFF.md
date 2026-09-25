# C++ planning completion — review-closure handoff

2C.2 is implemented as private library operations. The source/test-code/CI review
requested this bounded closure: declare threading, prove deferred reaping, and
record compatibility decisions and exact-revision verification. This is not a
full independent audit or approval of every inherited sprint commit. Nothing was
merged, auto-merged or self-approved. No production CLI, session preparation, runnable
workspace, engine launch, schema change or native-format change was added.

## Identity and review order

- Actual main baseline: `a86be96faec2aacb4594d253bb9385091a0c2739`.
- Inherited sprint base: `42eebc9b250caee8bbd3d4fea8d9b750635398ef`.
  Main was its ancestor; ahead/behind was 10/0. All ten commits are preserved.
- Branch: `frontend/cpp-planning-completion`.
- Worktree: `/home/willvdb/code/games/carnivores2-planning-completion`.
  Original main worktree and its two untracked log files were untouched.
- Review-closure implementation/tested code SHA:
  `06673e0ab86716b7f2a6b4b099ea7c6385d5441d`.
- Previously reviewed head: `1e2a398424f2c114ada7f8bfc3c34b0b9b720956`;
  original implementation SHA: `1f2dceedcc4b99548adb28f142a3ca1d6dd22da3`.
  This handoff follows the tested code; its own SHA is not predicted here.
  Fetch confirmed both live branch refs at the reviewed head and a clean planning
  worktree before editing; no inherited commits were reset, rebased or replaced.

Review the inherited dependency stack first, in the order in
[CPP_SPRINT_HANDOFF.md](CPP_SPRINT_HANDOFF.md). It remains unreviewed draft,
including the separate journal component. Then review these additions:

1. `338d19ab8e2a87b480d91ad1f254a35f2b4bc7fd` — **A**, helper runner
   ownership/deadlines, descriptor isolation and compiled cross-platform tests.
2. `378828c5c168c476a4a06ed73ba4622e6f347a51` — **B**, snapshot pins,
   authoritative managed-generation refresh and same-snapshot manifest adapter.
3. `4fbe3f2f7201fb508cbe2cb64cc790c512f8d352` — **C**, observer/hunt wrappers,
   differential composition and failure/side-effect tests.
4. `e9a9bd3f4cc00dd26bbc9a331d8a788b9cb53704` — isolated catalog-projection
   fixture working-directory correction after reproducing the inherited flake.
5. `b9965b54a60e6cd944cdb22532bfe421d443c571` — planning test-driver binary
   I/O, correcting Windows CRLF protocol failures exposed by actual CI.
6. `1f2dceedcc4b99548adb28f142a3ca1d6dd22da3` — **A follow-up**, refuse
   incomplete descriptor isolation; test an inherited fd above a lowered hard
   limit. Review alongside commit 1; its production delta is POSIX-only.

## Review-closure diff

After the preserved implementation stack and `1e2a398` handoff:

1. `e9253c4` — CMake discovers `Threads` with `THREADS_PREFER_PTHREAD_FLAG`
   and links `Threads::Threads` privately to `c2_frontend_core` and
   `c2-frontend-store-write-no-elision-tests`. These are the only two targets
   independently compiling `probe_process.cpp`; the latter remains test-guarded.
   This corrects a source-identified portability dependency; no older-toolchain
   failure was reproduced or attributed to the reviewer.
2. `06673e0` — private POSIX observer/seam and deterministic regression, plus
   a bounded Linux CTest and the test driver's own threading dependency.
   A FIFO confirms the compiled `hold` helper executed. The seam reports
   synchronous reap attempts as pending through the real 250 ms grace, without
   replacing SIGKILL, the atomic PID handoff or the prestarted waiter's waitpid.
   A condition-variable gate holds that waiter after acquisition. The test
   observes failure return with the runner stack gone and the waiter blocked,
   one signal/transfer/acquisition, and no later synchronous wait or signal.
   `waitid(WNOWAIT)` verifies the actual child is still available to its owner;
   releasing the gate allows real `waitpid` to reap it, with the expected SIGKILL
   status and a completion notification. A following `waitpid` yields `ECHILD`.
   Another compiled helper succeeds in the same caller process. Shared ownership
   keeps test coordination alive independently of the runner stack; an RAII guard
   releases the gate on assertion/exception paths. Python bounds the driver at
   15 s and kills its owned process group on failure; the standalone CTest has a
   20 s outer timeout. No sleeps establish the test assertions. Existing Windows
   compiled scenarios remain active; this additional case requires Linux's
   supported descriptor-isolation backend.
   **No runtime defect was demonstrated**; production changes are limited to the
   inert-by-default private seam. No public option or environment switch exists.
3. Documentation-only closure evidence and compatibility dispositions follow
   that tested code revision; this document does not predict its own commit SHA.

## Completed scope

**A:** Windows pipe readers/writer were replaced by one synchronous nonblocking
byte-pipe loop (`PIPE_NOWAIT`), removing data races, partial worker startup and
thread-exception/join hazards. Child handles remain explicitly restricted;
argument quoting is retained. Suspended child/job assignment, resume, waits,
I/O and exit-code failures are checked. Job cleanup does not rely on direct-child
termination releasing descendant pipe handles. Timeout and OS failures remain
different exceptions. Linux protects closed stdio descriptor allocation, closes
unrelated descriptors, polls the exec-error handshake alongside all streams,
bounds drain batches and retains thread-local SIGPIPE handling. A prestarted
cleanup waiter owns any reap delayed beyond a bounded 250 ms cleanup grace;
the operation never waits on an unbounded join. Fail-closed output limits remain.

**B:** `snapshot_pins` follows Python's validation order, ownership restrictions,
content/catalog observations, canonical-slot checks, legacy/schema-2 split,
bounded stable capture, expected-codec comparison, resolved helper execution,
inspection and provenance filtering. Pins and blobs are independent owned values;
unknown metadata and exact native bytes are retained. Current head is authoritative;
corrupt/missing heads never select G0 or an older generation. Explicit historical
resolution remains available only when explicitly requested. Managed-history
refresh compares the current generation members rather than original-import
metadata. `launch_dry_run` no longer rereads disk inside its transaction: typed
and mutable views derive from the same retained value via a private adapter.

**C:** `plan_observer` holds the reference writer lock, uses pins and the selected
instance's projection, evaluates the existing policy and only then hashes an
optional selected engine. `plan_hunt` selects the legacy/current-generation path
from one immutable manifest observation, requires the complete pair and evaluates
the existing hunt policy. Like Python it does not take a writer lock or persist.
`launch_dry_run` retains early completion, same-instance stage binding and the
legitimate `last_observation` transaction write. No new native-byte mutation.

## Review-closure verification

At exact code SHA `06673e0ab86716b7f2a6b4b099ea7c6385d5441d`:

- Compatibility generator `python3 Frontend/tools/generate_compatibility.py --check`:
  passed; expected fixtures were not regenerated.
- Clean Debug reconfiguration (`cmake --fresh`) found `Threads: TRUE` and
  `CMAKE_HAVE_LIBC_PTHREAD` on this host. Both independent production-source
  targets built at the threading commit, then rebuilt at the tested closure SHA.
  No extra pthread link flag is needed by this integrated-libc
  environment; no flags were hard-coded.
- Reused GCC Debug and Release builds: **33/33 CTests each**, complete parallel
  suites including runner, planning-store, journal and pure-policy regressions.
- New standalone deferred-reap CTest: **20/20 consecutive repetitions**, also
  exercised by both full suites and by the full runner test (25 Linux cases).
- Reused Clang Debug ASan/UBSan build: **4/4 actually executed and passed**:
  `frontend-native-probe-process`, `frontend-native-probe-deferred-reap`,
  `frontend-native-planning-store`, `frontend-native-session-journal`.
  `ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=halt_on_error=1`; no sanitizer
  reports. LeakSanitizer was not run.
- New GitHub Frontend run
  [36100210586](https://github.com/willvdb/carnivores2-mee-modernized/actions/runs/36100210586)
  at this exact code SHA: **both jobs completed successfully**, verified from
  GitHub metadata and completed logs. Ubuntu job `107960895813`: **33/33 passed**,
  including deferred-reap (0.31 s), runner (4.29 s), journal (0.67 s),
  planning-store (1.99 s) and pure-policy (18.88 s). Windows job `107960896004`:
  **49/49 passed**, including actual compiled runner (7.05 s), journal (4.82 s),
  planning-store (24.26 s) and pure-policy (219.66 s). Both configurations found
  `Threads: TRUE`. Windows retains all existing compiled scenarios; the new
  Linux-only deferred case is explicitly skipped within its runner harness and
  is not registered as a separate Windows CTest. No CTests were skipped.
  Earlier green runs are not substituted for this exact-SHA verification.
  The final documentation-only commit follows this tested code checkpoint;
  these CI results are not attributed to that documentation commit's SHA.

Logs for this closure are preserved as `closure-*` and
`review-closure-prior-ci.log` under
`/home/willvdb/.local/state/c2-planning-completion/`. Builds, C++17 selection and
existing Python 3.12.14 interpreter were reused; no toolchains were installed.
No local CTests or focused sanitizer cases were skipped. macOS/other POSIX execution, Windows Release and
Windows sanitizers were not run.

## Original implementation tests and CI (retained evidence)

At exact code SHA `1f2dceedcc4b99548adb28f142a3ca1d6dd22da3`, Linux x86-64:

- Compatibility generator `python3 Frontend/tools/generate_compatibility.py --check`:
  passed (also covered by the full suite with Python 3.12.14).
- GCC Debug, C++17, Python 3.12.14: **32/32, three complete parallel `-j8`
  runs**, each with `--output-on-failure --no-tests=error` and separate logs.
- GCC Release: **32/32**, complete parallel suite.
- Clang Debug ASan/UBSan: **2/2** focused runner/planning-store CTests,
  `ASAN_OPTIONS=detect_leaks=0`, no sanitizer reports. LeakSanitizer was not run.
- Runner suite: 24 cases on Linux; planning-store suite: 18 cases with multiple
  differential fixtures each. Full suite includes all 154 unchanged Python tests
  and the existing pure-policy, generation/capture and write-primitive oracles.
- Initial full run at `4fbe3f2` was **31/32**: catalog-projection flake reproduced.
  The failure log was preserved. Its relative-path snapshots scanned the build
  directory while other tests changed CTest files. The correction checks the
  actual fixture tree. Subsequent complete parallel runs passed; assertions
  were not weakened and no test was serialized/removed.

Builds used `/tmp/c2-planning-debug`, `/tmp/c2-planning-release` and
`/tmp/c2-planning-asan`. CMake explicitly used `-DCMAKE_CXX_STANDARD=17` because
this host's GCC 16 default selects newer `char8_t` filesystem return types that
an inherited test driver does not support. A disposable Python 3.12.14 interpreter
was installed under `/tmp/c2-planning-pythons`; the installer rewrote a pre-existing user symlink, which was restored to
its exact prior (removed sprint scratchpad) target. No persistent machine
configuration change is retained.

Logs are preserved outside the repository in
`/home/willvdb/.local/state/c2-planning-completion/`, including each initial/final
parallel run, build logs, Release/sanitizer results and available CI logs.
The `c2-planning-complete-*` logs correspond to the exact tested code SHA above.

Actual CI evidence (not a claim of full Windows approval):

- [A run 36086312763](https://github.com/willvdb/carnivores2-mee-modernized/actions/runs/36086312763),
  at `338d19a`: Ubuntu **32/32 passed**. Windows built and **ran/passed the native
  runner CTest** (5.65 s), including compiled helpers through CreateProcess and
  real pipes. Windows planning-store ran and exposed two inherited CRLF error
  protocol failures. `b9965b5` fixes the driver rather than normalizing errors.
  The superseded Windows job was cancelled later; its full suite did not finish.
- [Intermediate run 36087015262](https://github.com/willvdb/carnivores2-mee-modernized/actions/runs/36087015262)
  at `e9a9bd3` was cancelled after supersession by the binary-protocol fix.
- [Protocol-fix run 36087135014](https://github.com/willvdb/carnivores2-mee-modernized/actions/runs/36087135014)
  at `b9965b5`: Ubuntu passed. Windows **ran/passed both the compiled runner
  CTest (5.81 s) and the complete new planning-store CTest (25.09 s)**, plus
  the real pure-policy CTest (211.58 s). This confirms actual Windows execution
  of pins, managed history, the wrappers, refusal markers and write/lock cases.
  The superseded run was then cancelled to retrieve its logs; the final-code
  run below is retained. The only later production change is POSIX isolation.
- [Final-code run 36087372781](https://github.com/willvdb/carnivores2-mee-modernized/actions/runs/36087372781)
  at exact SHA `1f2dceedcc4b99548adb28f142a3ca1d6dd22da3`: **both jobs completed
  successfully**, verified from GitHub metadata and completed logs in this
  closure. Ubuntu job `107922162665`: **32/32 passed**. Windows job
  `107922162825`: **49/49 passed**, including actual compiled runner (5.74 s),
  planning-store (18.34 s), journal (4.05 s) and pure-policy (209.89 s) execution.
  This supersedes the original handoff's pending-Windows statement. It covers
  `1f2dcee`, not the later closure code.

The compiled runner scenarios are not skipped on Windows. POSIX shebang tests,
closed-stdio fd tests and the Linux-specific lowered-limit test have explicit
platform skips there. macOS/other POSIX, Windows Release and Windows sanitizers
were not run. The complete original implementation Windows suite is now verified
at `1f2dcee`; the closure code has separate exact-SHA evidence above.

Reproduce local gates from the worktree root:

```sh
python3 Frontend/tools/generate_compatibility.py --check
cmake -S Frontend -B /tmp/c2-planning-debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=17 -DPython3_EXECUTABLE=/tmp/c2-planning-pythons/cpython-3.12.14-linux-x86_64-gnu/bin/python3.12
cmake --build /tmp/c2-planning-debug --config Debug --parallel 2
ctest --test-dir /tmp/c2-planning-debug -C Debug --parallel 8 --output-on-failure --no-tests=error
gh run view 36087372781 --log-failed
```

## Differential evidence and private-milestone compatibility decisions

Tests call unchanged `lodge.sessions.snapshot_pins`, profile refresh,
`genesis.plan_observer`, `native_hunt.plan_hunt` and `launch.prepare`.
Results compare insertion order and exact JSON kinds; persisted manifest/backup
bytes are compared without normalization. Observer/launch IDs and timestamps
are the only volatile result fields omitted, with format checks where applicable.
Authored G0/G1/G2 have different save scores, proving exact head/root selection.
Missing/corrupt heads, referenced/legacy managed copies, foreign/missing/drifted
installations, unknown/archived identities, malformed/opaque state, incomplete
pairs, capture bounds, locks and injected write failure have deterministic
fixtures. Per-file before/after comparisons include original saves and all
managed generations. A compiled helper invocation marker proves mismatched or
missing codec evidence never executes the helper; matching evidence invokes it.

Successful observer/hunt wrapper composition uses a clearly labelled asset-free
policy double, following the existing Python test convention. It echoes real
revision, projection, slot, selection and inspected score. Production policies
are unchanged; real-policy refusals and the full existing pure-policy suite run
separately. No real Genesis assets were used or committed, so a successful
production-policy plan against a real Genesis installation is not certified.

The following are explicit accepted restrictions/decisions for this **private
library milestone**, not a claim that the entire Python runtime is behaviorally
identical. Broader equivalence remains a future public API/CLI-cutover decision:

- **PATH:** Python profile inspection searches PATH for bare executable names,
  while Python executable evidence resolves a filesystem path. Native execution
  retains the explicit-path/CWD convention on both hosts. Pinned operations
  execute only the absolute path returned by evidence; no PATH/helper fallback
  can bypass a codec pin. Bare-name equivalence is not claimed; no PATH-resolution
  redesign is included in this closure.
- **Output bound:** native stdout and stderr each retain at most 16 MiB; Python
  is unbounded. The 16 MiB per-stream bound is an intentional defensive
  difference. Overflow throws explicitly and cleans up; truncated output is
  never parsed as a valid result. Exact unbounded Python equivalence is not claimed.
- **Descriptor isolation:** the Linux backend now requires successful
  `close_range` (Linux 5.9+ with the syscall permitted). Supported helper execution
  is restricted to Windows and Linux environments where that required operation
  is available and permitted. Other POSIX, older Linux and blocked/unavailable
  syscalls currently refuse before helper exec; they are not supported.
  A limit-based close loop is
  unsound when existing descriptors exceed a subsequently lowered hard limit.
  Fail-closed behavior remains; there is no approximate descriptor-closing
  fallback. Additional POSIX support requires a separately tested complete-close
  backend.
- **Cleanup:** POSIX terminates the direct owned child, like Python; it does not
  supervise grandchildren. After 250 ms, a prestarted waiter may finish reaping
  asynchronously. Bounded return to the caller does **not** guarantee kernel-level
  termination or reaping has already completed at that instant. Windows retains
  job-based cleanup. The deterministic regression and exact tested revision are
  recorded above; no journal PID is ever used for signalling.
Other inherited caveats retained (not resolved or independently approved by this
closure):

- **Manifest consistency:** hunt adapter selection and pins use one read instead
  of Python's separate schema/pin reads. Transaction refresh uses the retained
  manifest consistently instead of `association_root` rereading it. Behavior
  matches quiescent/cooperatively locked stores; no adversarial-writer guarantee.
- Inherited non-object helper/error-kind conventions, tuple/list/non-finite typed
  API limits and 3A follow-ups remain as documented in the sprint handoff. No
  claim of full cumulative sprint review is made. Original implementation
  self-review covered the runner,
  new adapters/wrappers and the inherited discovery, generation/capture,
  transaction and inspection paths they exercise.

## Stopping point

Focused self-review checked the threading target coverage/test guards, inert
observer defaults, shared deferred lifetimes, ownership-event assertions, bounded
failure cleanup, Windows scenario retention and the seven-file scope. It found
no additional defect. Main remains `a86be96`; the Python implementation and all
inherited commits remain unchanged.

This closure is limited to the requested build dependency, deterministic cleanup
regression and verification/compatibility documentation. All requested local and
code-checkpoint CI checks passed; no technical verification blocker remains. Reviewer acceptance of
these closure revisions and any separate inherited-stack review remain distinct
from the test results; this pass does not approve the cumulative sprint or mark
anything merged. No Python implementation changed. No public CLI, session
preparation, workspace creation or engine launch was started. **Stop here; do not
begin another migration slice as part of this handoff.**
