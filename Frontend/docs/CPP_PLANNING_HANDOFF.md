# C++ planning completion — unreviewed implementation handoff

2C.2 is implemented as private library operations. Local verification is complete;
final-head Windows CI and independent review remain gates. Nothing was merged,
auto-merged or self-approved. No production CLI, session preparation, runnable
workspace, engine launch, schema change or native-format change was added.

## Identity and review order

- Actual main baseline: `a86be96faec2aacb4594d253bb9385091a0c2739`.
- Inherited sprint base: `42eebc9b250caee8bbd3d4fea8d9b750635398ef`.
  Main was its ancestor; ahead/behind was 10/0. All ten commits are preserved.
- Branch: `frontend/cpp-planning-completion`.
- Worktree: `/home/willvdb/code/games/carnivores2-planning-completion`.
  Original main worktree and its two untracked log files were untouched.
- Final implementation/tested code SHA:
  `1f2dceedcc4b99548adb28f142a3ca1d6dd22da3`.
  This handoff follows it; its own SHA is intentionally not predicted here.

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

## Tests and CI

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
  at `1f2dcee`: Ubuntu passed; Windows is still in progress at handoff.
  Confirm the full Windows suite,
  particularly `frontend-native-probe-process` and `frontend-native-planning-store`.
  Compilation alone does not satisfy those runtime gates.

The compiled runner scenarios are not skipped on Windows. POSIX shebang tests,
closed-stdio fd tests and the Linux-specific lowered-limit test have explicit
platform skips there. macOS/other POSIX, Windows Release and Windows sanitizers
were not run. Windows planning runtime success is established at `b9965b5`; a **complete
final-head Windows suite pass is not yet claimed**.

Reproduce local gates from the worktree root:

```sh
python3 Frontend/tools/generate_compatibility.py --check
cmake -S Frontend -B /tmp/c2-planning-debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=17 -DPython3_EXECUTABLE=/tmp/c2-planning-pythons/cpython-3.12.14-linux-x86_64-gnu/bin/python3.12
cmake --build /tmp/c2-planning-debug --config Debug --parallel 2
ctest --test-dir /tmp/c2-planning-debug -C Debug --parallel 8 --output-on-failure --no-tests=error
gh run view 36087372781 --log-failed
```

## Differential evidence and compatibility review gates

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

Named gates still requiring independent disposition:

- **PATH:** Python profile inspection searches PATH for bare executable names,
  while Python executable evidence resolves a filesystem path. Native execution
  retains the explicit-path/CWD convention on both hosts. Pinned operations
  execute only the absolute path returned by evidence; no PATH/helper fallback
  can bypass a codec pin. Bare-name equivalence is not claimed.
- **Output bound:** native stdout and stderr each retain at most 16 MiB; Python
  is unbounded. Overflow throws and cleans up; truncated JSON is never parsed.
- **Descriptor isolation:** the Linux backend now requires successful
  `close_range` (Linux 5.9+ with the syscall permitted). Other POSIX backends or
  blocked/unavailable syscalls refuse before exec. A limit-based close loop is
  unsound when existing descriptors exceed a subsequently lowered hard limit.
  Additional POSIX support requires a separately tested complete-close backend.
- **Cleanup:** POSIX terminates the direct owned child, like Python; it does not
  supervise grandchildren. After 250 ms, a prestarted waiter may finish reaping
  asynchronously. Windows terminates its job. These lifetime/deadline conventions
  are explicit and need review; no journal PID is ever used for signalling.
- **Manifest consistency:** hunt adapter selection and pins use one read instead
  of Python's separate schema/pin reads. Transaction refresh uses the retained
  manifest consistently instead of `association_root` rereading it. Behavior
  matches quiescent/cooperatively locked stores; no adversarial-writer guarantee.
- Inherited non-object helper/error-kind conventions, tuple/list/non-finite typed
  API limits and 3A follow-ups remain as documented in the sprint handoff. No
  claim of full cumulative sprint review is made. Self-review covered the runner,
  new adapters/wrappers and the inherited discovery, generation/capture,
  transaction and inspection paths they exercise.

## Next bounded task

Finish/inspect the final-code Windows CI results, then independently review the
inherited dependencies and these six commits, resolving the named compatibility
gates. **Do not begin 3B session/workspace preparation as part of this handoff.**
The 2C.2 implementation exists; all verification/approval gates are not yet met.
