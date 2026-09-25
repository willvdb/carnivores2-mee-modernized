# Native frontend migration relay

## Historical foundation baseline and scope

Python remains the authoritative runtime/reference at main commit
`e742fb7ab85ba0c571d50e45ff0a410eb2a444dd`. Branch:
`frontend/cpp-migration-foundation`. This relay covers **0A + 0B only**:
compatibility fixtures, representation, exact encoders, SHA-256, standalone core,
CLI shell and native tests. No frontend backend operation is ported or exposed.
No engine, Menu, legacy disk format, schema version, or Python behavior changes.

## Immutable compatibility contract

* Read manifest versions 1/2, history and receipt version 1, journal versions
  1–4; never upgrade on read. Preserve unknown nested metadata and exact JSON
  value kinds. Exact-integer validation must reject booleans and floats even
  though Python numeric equality considers `True == 1 == 1.0`.
* Preserve exact authority and current head, with no fallback to earlier state.
  Retain native bytes and historical codec-helper pins; do not re-encode saves.
* Keep POSIX/NT path semantics distinct. Preserve Latin-1 projection and case
  ambiguity rules. Reject unsafe links, aliases, reparse points, hardlinks,
  special files and overlapping owned paths; keep capture bounded and stable.
* Source, baseline, working and returned evidence are independent. Signal only
  an owned live child, never a PID recovered from a journal. Preserve timeouts,
  cancellation, bounded retained logs and draining of child output.
* Distinguish unknown/null evidence from an observed empty list. A candidate
  is not quarantine and neither authorizes automatic adoption.
* Acceptance is explicit and freshly revalidated against the expected
  predecessor and candidate digest. Publish an independent immutable snapshot
  before the atomic head + receipt commit. A copy failure after that commit
  must not roll back authority. Reject stale and equal-byte competing sessions.
  An idempotent retry must never rewind G2 to G1.

### Exact serialization and hashes

`ContentFingerprintV1` is the byte encoding in `lodge.discovery.fingerprint`:
`json.dumps(sorted(entries), ensure_ascii=True, separators=(',', ':')).encode()`.
Each entry is `[relative-posix-path, integer-size, member-sha256]`. Hash those
bytes with SHA-256, lowercase hexadecimal; no newline, no spaces, no object or
algorithm wrapper. The native encoder receives already observed entries; it
does not scan or validate filesystem content.

`JournalEvidenceV1` is `lodge.session_io.encode(decoded_value)`:
`(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + '\n').encode()`.
`lodge.acceptance.candidate_digest` hashes **those encoded bytes**, not original
disk bytes. Object ordering, whitespace and numeric source spelling are not
retained on disk-byte evidence; decoded kinds and values are retained. This
encoder is not journal validation or acceptance.

Strings sort by Unicode code point, not UTF-8 bytes or UTF-16 code units.
ASCII output uses lowercase `\\u` escapes, surrogate pairs for supplementary
code points, short escapes for backspace/formfeed/newline/carriage-return/tab,
and escaped quote/backslash; slash is not escaped. Escaped unpaired surrogates
are valid retained Python strings. Raw UTF-8 must be well formed. Duplicate
object keys are rejected after escape decoding at every depth, including an
escaped key identical to a literal key. No Unicode normalization is performed.

Integers retain arbitrary decimal magnitude rather than narrowing to binary64
or int64; negative integer zero becomes `0`. Floats remain IEEE-754 binary64,
including signed zero: `900.0` remains `900.0`, `-0.0` remains `-0.0`. Use the
shortest round-tripping nearest decimal, fixed notation for decimal exponents
from -4 through 15, and signed exponent notation with at least two exponent
digits otherwise. Preserve `.0` for fixed integral floats. Nonfinite tokens
(`NaN`, `Infinity`, `-Infinity`) and overflow are accepted by Python decoding,
but rejected by `JournalEvidenceV1` exactly as `allow_nan=False` requires.
Underflow rounds to signed zero. Retaining nonfinite unknown metadata at parse
time is separate from permission to encode evidence.

The private representation has no public semantic equality operation. Future
validation must reproduce Python numeric equality where used by the reference,
and exact-type checks where used, rather than replace both with one DOM rule.
Python's configurable integer digit and recursion resource limits are not new
file-format semantics. Resource/stack limits and all accepted representation
differences must be resolved as migration gates before exposing read APIs;
they do not authorize tightening the Python contract.

### Oracle and verification

`python3 Frontend/tools/generate_compatibility.py --check` regenerates in memory
using unchanged Python code and compares the committed corpus byte for byte.
Run without `--check` only for a reviewed oracle update. The generator calls the
real journal encoder and candidate digest, and captures the real fingerprint
payload over tiny authored files. No game assets are used. Inputs include
unknown metadata, duplicates, type distinctions, numeric boundaries, fixed
binary64 samples and Unicode cases. Expected payloads are stored as hex so LF,
escaping and indentation are reviewable independently of checkout line endings.

## Current state

Main is `a86be96faec2aacb4594d253bb9385091a0c2739` (PR #27, slice 3A, head
`d0a8c9d`; 28/28 CI green including actual Windows store-write tests).
Review limitation: independent Fable review approved an earlier 3A revision;
the final Windows lock-link correction rounds were not routed for re-review,
so the merged 3A head is not independently approved by every reviewer.
Inherited 3A follow-ups: Python-side Windows safety fixes, lone-surrogate
path policy, the older copyable handle in `store_paths`, and
clustered-Windows hostname behaviour.
The inherited **unreviewed implementation sprint** at
`frontend/cpp-session-sprint` / `42eebc9` is preserved. Its bounded continuation,
`frontend/cpp-planning-completion`, implements 2C.2 library wrappers and helper
hardening; see `CPP_PLANNING_HANDOFF.md` for exact code SHAs, tests and pending
CI/review gates. Nothing from either branch is approved or merged.
Implemented: `snapshot_pins`, authoritative managed-history refresh,
`genesis-observer-plan`, `native-hunt plan`, and complete shared paths for
`launch-dry-run`. The latter retains the reference observation write. No
workspace/session preparation or engine launch was added. The inherited
session-journal component remains draft, outside this milestone's review.
The inherited sprint implements the 2C.1 follow-ups: unexpected-oracle-kind
assertions, canonical ordinal guards, documented typed API limits and stage-one
completion/same-instance binding. The continuation completes their store paths.
Convention since 2C.1: reference `TypeError` paths map to `std::invalid_argument`;
module errors carry only reference `FrontendError` messages. From 3A: write
primitives propagate reference `OSError` paths as
`std::filesystem::filesystem_error`, and the reference's bare `ValueError`
from `allow_nan=False` encoding maps to `StoreError`. From 2B.2:
`project`/`text_reference` also propagate `ContentError`, `StoreError` and
`filesystem_error`; a future CLI must catch `std::exception`.
Open completion gates: final-head Windows verification and independent review;
bare-name/PATH behavior, native helper output bounds, Linux `close_range`
availability and bounded deferred reaping remain explicit compatibility review
items. See the planning handoff rather than treating local passes as approval.
Independent review remains separate from implementation; none was performed
for this continuation. This ledger does not approve the cumulative sprint.

### Operation coverage (Python CLI to native)

| Python command | Native status |
| --- | --- |
| `status`, `host-settings` (read), `hunter list`, `expedition list`, `managed-state inspect` | CLI views (1A.2, 1B.1) |
| `profiles` | Library only (1B.2/1B.3); CLI pending 7 |
| `expedition discover` (read-only), `expedition refresh` observation | Library only (2A); refresh write pending 5A |
| `catalog` | Parser 2B.1; projection library 2B.2; CLI pending 7 |
| `launch-dry-run`, `genesis-observer-plan`, `native-hunt plan` | 2C.2 library wrappers implemented on `frontend/cpp-planning-completion`; local verification complete, final-head Windows/review gates pending; no CLI |
| `session prepare-synthetic/inspect/run/reconcile/recover`, `simulate-return` | Pending 3B-4C (synthetic sessions remain developer tooling) |
| `native-observer prepare/run`, `native-hunt prepare/run/inspect` | Pending 3B-4C |
| `hunter create/select/rename/archive`, `host-settings --json`, `associate`, `refresh-state` | `refresh-state` library implemented, including managed history; other mutations and CLI pending 5A |
| `expedition register/relocate`, `discover --register-managed`, `managed-state upgrade`, `recover-backup` | Pending 5B |
| `managed-state preview/accept/recover-acceptance` | Pending 6A/6B |

## Relay checklist

| Slice | Status |
| --- | --- |
| 0A compatibility contract and golden corpus | Reviewed and merged in PR #16 |
| 0B core, private representation, encoders, SHA, CLI/tests | Reviewed and merged in PR #16 |
| 1A.1 pure manifest/schema validation | Reviewed and merged in PR #17 |
| 1A.2 read-only filesystem/store/CLI integration | Reviewed and merged in PR #18; overall 1A complete |
| 1B.1 current-generation/capture read | Reviewed and merged in PR #19 |
| 1B.2 pure profile-byte codec inspection | Reviewed and merged in PR #20 |
| 1B.3 filesystem profile inventory/inspection | Reviewed and merged in PR #21; overall 1B read observations complete |
| 2A.1 reference resolution/content fingerprint | Reviewed and merged in PR #22 |
| 2A.2 coherent-root discovery/instance observations | Reviewed and merged in PR #23; overall 2A complete |
| 2B.1 pure catalog parser/scalars | Reviewed and merged in PR #24 |
| 2B.2 filesystem catalog projection | Reviewed and merged in PR #25 |
| 2C.1 pure planning policies | Reviewed and merged in PR #26 |
| 2C.2 planning wrappers (lock, pins, observation write) | Implemented, locally tested, unreviewed on `frontend/cpp-planning-completion`; final-head Windows verification pending |
| 3A safe paths/capture/atomic I/O | Merged in PR #27 (final correction rounds not re-reviewed) |
| 3B preparation/journal | Pending |
| 4A trust/process/capabilities | Pending |
| 4B runner/log/cancel | Pending |
| 4C reconcile/recovery | Pending |
| 5A manifest writes | Pending |
| 5B registration/import/upgrade | Pending |
| 6A acceptance preview | Pending |
| 6B atomic accept/recovery | Pending |
| 6C G0–G1–G2 failure matrix | Pending |
| 7 cutover/API | Pending |

## Evidence and remaining gates

The published prerequisite 0A commit is
`ac92f8a153313c25e1fd7cfc6fce66651ca74a0f` (tree
`a8e52dae0fb398e7ce3f47e3f9d550f0d70a3e59`). This following 0B commit contains
the implementation, expanded 325-case corpus, and these results. Its SHA must
be recorded by the coordinator in review/merge evidence, not guessed within
its own content. Both slices await independent review; none is self-approved.

The standalone `c2_frontend_core` is C++17. Its public header exposes only
version and in-memory SHA-256; the DOM and named encoders are private/test
interfaces. `c2-frontend-native` exposes only help/version and rejects backend
commands. The existing Python `c2-frontend` target and all existing assertions
remain intact. Dependencies are vendored, pinned and offline; see
`native/third_party/README.md` for exact provenance, hashes, license and review.
The existing Linux/Windows frontend workflow already builds all targets and
runs all CTests, so its gates did not need modification.

Local validation: GNU C++ 13.3.0, Python 3.12.14, CMake 4.4.3, Ninja 1.13.2,
Linux x86-64. Commands from repository root (build directories may be outside
the checkout):

```sh
python3 Frontend/tools/generate_compatibility.py --check
cmake -S Frontend -B build/frontend-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/frontend-debug --parallel 2
ctest --test-dir build/frontend-debug --output-on-failure --no-tests=error

cmake -S Frontend -B build/frontend-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/frontend-release --target c2-frontend-native-tests c2-frontend-native --parallel 2
ctest --test-dir build/frontend-release -R 'frontend-(native|compatibility)' --output-on-failure --no-tests=error

cmake -S Frontend -B build/frontend-sanitized -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build build/frontend-sanitized --target c2-frontend-native-tests c2-frontend-native --parallel 2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/frontend-sanitized -R 'frontend-(native|compatibility)' --output-on-failure --no-tests=error
```

Results: Debug 6/6 CTests passed, including the unchanged 154-test Python suite
(69.27 seconds total); the final expanded corpus then passed all 5 foundation
CTests again (0.75 seconds). Release 5/5 passed (0.40 seconds), ASan/UBSan 5/5
passed (1.82 seconds), oracle `--check` passed. The coordinator independently
ran the unchanged baseline Python suite: 154 tests passed. Initial local
verification had a transient missing build executable and a mistakenly
overlapping clean rebuild/test run; the stable rerun above supersedes those
infrastructure failures. LeakSanitizer fails under this environment's ptrace
and was disabled; address and undefined-behavior checks remained enabled.

Preliminary coordinator evidence (before final published-SHA review): 50,986
independent differential cases had zero mismatches against unchanged Python,
including 20,000 random binary64 values, 20,000 long decimal/exponent spellings,
1,000 nested Unicode/large-integer objects, and 10,000 malformed-byte mutations.
The coordinator also independently compared both vendored PicoSHA2 files
byte-for-byte against the pinned upstream commit: exact matches. Complete
published-commit code review and CI remain pending.

Native tests compare every oracle byte/digest, preserve nested unknown fields,
check duplicate keys and exact types, and exercise escaping, invalid UTF-8,
binary64 boundaries, large integers, deep containers, SHA known answers and
locale independence. Test-only differential drivers accept arbitrary UTF-8 on
stdin via `c2-frontend-native-tests --journal-stdin` or `--compact-stdin`.

Real gates: the JSON grammar/DOM implementation is original and requires
independent review; a finite corpus cannot prove equivalence. It uses decimal
strings and code points without integer/Unicode loss and C++17 `charconv` for
binary64. Parsing/encoding currently guards depth above 1000, covering the
baseline default CPython recursion budget; deeper input accepted under an
explicitly increased Python recursion limit remains a resource-policy gate
before any production read API/cutover. Native decimal storage does not impose
Python's configurable integer conversion digit limit. These are explicit
resource-policy differences, never permission to change accepted file data.
Domain validation, Python-compatible semantic equality, filesystem policy,
processes and every later relay slice remain unimplemented.

Windows MSVC CI is required before merge; local Linux validation cannot stand
in for it. No later slice should start until 0A/0B representation gates pass.

## Review correction: bounded native stack use

The coordinator inspected published head
`42c2e0880134438a771e8b26ffc601ad691328bf`: 0 commits behind / 2 ahead of the
baseline, the complete 14-file additive diff, and no weakened existing tests.
An isolated Linux Debug run passed 6/6 CTests in 71.95 seconds; the independent
50,986-case differential rerun again had zero mismatches. That evidence did
**not** clear the Windows gate: MSVC compiled successfully, but native
compatibility crashed with a segmentation fault in job `106048568361`, push
run `35499486506`. The other five CTests, including all 154 Python tests, passed.

The original recursive parser exhausted its native call stack before safely
rejecting the existing 1002-opening-bracket test. An isolated reproduction of
that exact input with the original source, ASan and a 512 KiB child-process
stack reported `stack-overflow` with repeated `Parser::value` frames. GCC with
a larger/noninstrumented stack could pass; that did not establish Windows
safety. With the correction, the same reproducer returns the intended
`JSON nesting resource limit` error without overflowing.

Parser and encoder traversal now use explicit heap frames. DOM copy traverses
an explicit work list, and moves transfer ownership without recursive copies.
Destruction walks ownership in postorder using temporary parent pointers;
it allocates nothing and only destroys already-empty child containers. Thus
ordinary destruction, replacement, partially built trees and exception unwind
do not consume call stack proportional to document depth. The private DOM's
value/copy semantics, original depth-above-1000 guard, all 325 golden cases,
Unicode/numeric behavior, evidence bytes and public API remain unchanged.

Regression coverage includes arrays and objects with a scalar at depth 1000,
the accepted empty-container boundary, deep copy/move/replacement/destruction,
near-limit incomplete documents, well-formed and malformed over-limit input,
and encoder rejection of privately constructed over-limit trees. The original
1002-bracket assertion remains. Native test stage messages locate future CI
failures. A new `frontend-native-stack` CTest runs owned children at 256 KiB on
POSIX and the unchanged executable default stack on Windows. No `/STACK`
override or reduction in nesting coverage is used. Rejections must return
the normal error exit and message; crashes cannot satisfy the test.

The stack regression also compares deep journal bytes to unchanged Python:
depth 900 is accepted with its default recursion budget; for depth 1000 the
test temporarily increases only the oracle process's recursion budget to
compare the native guard boundary's bytes. This does not change reference
runtime code or erase the documented configurable-resource gate. Windows
test-only stdin/stdout are explicitly binary so subprocess comparisons check
the actual LF evidence, without newline normalization.

Correction validation uses the build/test commands above, with a fresh Debug
build outside workspace binary synchronization and the added stack CTest.
The exact final full-suite command was:

```sh
cmake -S Frontend -B /tmp/c2-foundation-stackfix-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/c2-foundation-stackfix-debug --parallel 2
ctest --test-dir /tmp/c2-foundation-stackfix-debug --output-on-failure --no-tests=error
```

Debug passed all 7 CTests, including all unchanged 154 Python tests (68.87
seconds total). Release passed all 6 foundation CTests (1.54 seconds), and
ASan/UBSan passed all 6 (4.87 seconds), including the constrained-stack children.
The original 325-case oracle `--check` still passes. Earlier workspace builds
encountered executables being truncated during synchronization, independently
observed by the coordinator; the fresh `/tmp` build avoided that infrastructure
failure. Windows rerun and final correction review remain required before merge
or later slices; the correction commit's SHA belongs in subsequent evidence.

## Foundation merge and manifest split

Independent review cleared 0A/0B at correction commit
`a36f5e605645052db422963f2a76b0af316c8667`, after implementation commit
`42c2e0880134438a771e8b26ffc601ad691328bf` and fixture prerequisite
`ac92f8a153313c25e1fd7cfc6fce66651ca74a0f`. PR #16 merged as
`a08f1cc441375068fbe18c6cb9550708337a3bb4` (tree
`0ea17f181a6bcd4d0ed257fb00e3e6037ac5dd02`). Reviewer validation passed
7/7 CTests including all 154 Python tests in 70.19 seconds, 50,986 independent
JSON differential cases with zero mismatches, and pinned vendor byte checks.
All 28 GitHub push/PR checks passed, including MSVC Windows 7/7 CTests in
143.40 seconds. This supersedes the earlier pending foundation gates above.

Slice 1A is split to keep pure structural semantics reviewable independently
from filesystem policy: 1A.1 decodes and validates manifests, including nested
managed history and historical execution receipts, without consulting files
or executing recorded binaries. 1A.2 will add read-only repository/store/CLI
integration. Overall 1A is incomplete until both slices pass independent review.
Generation snapshot resolution and profile inspection remain 1B. No production
read API or backend CLI command is introduced by 1A.1.

## Slice 1A.1 implementation (not independently approved)

The private `manifest_schema` interface now decodes duplicate-key-rejecting
lossless JSON and validates manifest versions 1/2 in memory. It ports the pure
`store` identity/locator/engine-evidence/manifest checks and all nested
`managed_state` history, member, provenance, head, ancestry, sequence, receipt,
and historical execution checks. No recorded executable is queried and no
snapshot path is opened. Unknown fields and accepted nonfinite metadata are
retained; no schema upgrade or metadata projection replaces the decoded input.

`schema_compat` provides exact arbitrary-integer/binary64/bool equality,
iterative container equality including Python JSON NaN identity shortcuts,
truthiness, Unicode 15.0.0 whitespace/casefold/contextual lowercase, and tagged
pure POSIX/NT paths. Exact-type validation remains distinct from equality.
The native host is consulted only for the Python-reference `Path` member checks.
Unicode data, pinned generation instructions and its license are committed.
See `native/src/SCHEMA_COMPATIBILITY.md` for source mappings and resource gates.

The authored corpus invokes unchanged Python validators. It includes both host
path outcomes, v1/v2 managed and referenced associations, G0/G1/G2, 9,106 field,
type, deletion and targeted cases, plus 400 semantic-equality pairs and Unicode
case probes. Exact JSON inputs are losslessly represented as prefix/middle/suffix
deltas against four authored bases. The test-only binary stdin driver reports
valid/invalid/resource/unexpected-diagnostic separately; accepted values export
without dropping unknown data. The standalone production CLI remains help/version.

Local Debug validation passed all 10 CTests in 122.62 seconds, including the
unchanged 154 Python tests, 325 original golden cases, constrained stack
regressions and the new schema/Unicode tests. ASan/UBSan passed all 9 native,
compatibility and schema CTests in 117.31 seconds. Commands used fresh `/tmp`
build directories; `ASAN_OPTIONS=detect_leaks=0` disables only unavailable
LeakSanitizer under ptrace, with `UBSAN_OPTIONS=halt_on_error=1`. Full native
verification uses CPython 3.12.14 / Unicode 15.0.0; the Python runtime's 3.10+
contract is unchanged. The final expanded 9,106-case fixture corpus then passed the Debug oracle/native
checks (2/2, 49.37 seconds) and ASan/UBSan native check (1/1, 83.70 seconds). Final published-SHA review
and Windows CI remain required; this implementation does not self-approve.

Slice 1A.2 must still implement filesystem-backed read/store/CLI behavior; 1B
must resolve the authoritative current snapshot and inspect profile bytes. The
existing parser depth/resource-policy and configurable Python integer/recursion
differences remain pre-production-read gates, as does a change to the Unicode
baseline. Pure structural acceptance does not assert filesystem trust or safety.

## Manifest validation merge and read-only store checkpoint

PR #17 was independently reviewed at published head
`bfe93508e9a618606fc0b7e251040a55859c6f0d` and merged as
`32786ef3818353e41673c409b5eae7ad4de43f7d` (tree
`d95c5c0049ccfbe664e015a727c5d67bf8724b1f`). Its focused commits were
`3984b546f85c9a5b56ebdc47a97873c04f9fbc4c`,
`2ad86d0e8342bd714768e7c4a46c093ecb4993bb`, and the published head.
The reviewer inspected the complete published scope: Python runtime/tests and
public API were unchanged, with no unresolved code finding. Exact-SHA Linux
validation passed 10/10 CTests in 117.64 seconds, including 154 unchanged Python
tests in 65.041 seconds without skips. Independent differential validation ran
34,994 cases per POSIX/NT semantics (69,988 total), with zero mismatches; earlier
20,530 NT path spellings and 48,237 Unicode lowercase cases also matched. All
28 push/PR checks were green, including actual MSVC Windows 10/10 CTests in
215.81 seconds (job 106054301850). Full native ASan/UBSan and the final 9,106-case
schema rerun passed. Original 325 golden fixtures remained unchanged. This
supersedes historical pending 1A.1 review statements above.

Branch `frontend/cpp-store-read` starts exactly from that merge for 1A.2 only:
read-only store/repository, safe-path read prerequisites, and thin CLI reads.
Overall 1A stays incomplete pending independent 1A.2 review. Generation/profile
resolution, writes, locks, discovery, execution, and acceptance remain later
slices. Earlier foundation-only descriptions are chronological evidence, not
the current branch scope. Resource-policy resolution is a prerequisite of this
slice, not permission to change the persistent format.

## Slice 1A.2 read-only implementation (review pending)

The standalone public `store.hpp` adds `Store` and an immutable, opaque owned
`Manifest`. Native store paths use `std::filesystem::path`; typed hunter and
expedition summaries use `std::u32string`, preserving escaped surrogate code
points as well as supplementary characters. Unknown nested metadata remains
in the private owned representation. Schema version and optional active-hunter
presence are distinct queries. Domain-specific status/hunter/expedition/settings
exports retain insertion order, ASCII escapes and LF; the journal/evidence
encoder remains sorted and byte-identical. Reading does not require exporting,
so retained nonfinite metadata can fail one export while another succeeds.

The CLI exposes only `status`, `hunter list`, `expedition list`, and read-only
`host-settings`, plus help/version. Global `--store` and unused `--probe` accept
native paths; writes and later-slice commands fail before repository access.
Default paths follow LOCALAPPDATA or home/.local/share. Windows uses wide argv
and environment variables; POSIX retains native path bytes. Regular errors use
a JSON error object on stderr and exit 2; resource exhaustion uses a JSON error
and distinct exit 3. The reference's absent-active-hunter list indexing failure
is reported as a controlled native error; status never synthesizes that field.

Read policy is operational, not persistent-format validation: default maximum
nesting is 1000, with an explicitly raised API `ReadPolicy::max_depth` supported
by both parsing and presentation export. There is **no default manifest byte
or export-size ceiling**. An optional caller-selected input byte budget reports
`ResourceExhausted`; allocation/length exhaustion and nesting exhaustion are
also distinct from schema corruption. Arbitrary decimal integers retain their
magnitude and do not inherit Python's configurable conversion digit cap. The
existing private encoders/parser preserve their default resource behavior and
325 original golden fixtures. CPython 3.12 / Unicode 15.0 remains the pinned
semantic reference. These policy differences never cause fallback or writes.

The read adapter deliberately resolves the caller root once in the constructor,
then checks stored ancestors and the current manifest on each read. It rejects
aliases, symlinks/reparse points, special files and multiply-linked regular
files, including missing-current paths with unsafe ancestors. Windows ancestor
anchors follow PureWindowsPath rather than STL UNC/device decomposition, and
alias comparison uses pinned Unicode lowercase rather than OS uppercase tables.
Caller-supplied extended prefixes are retained. POSIX opens the final file with
O_NOFOLLOW/O_NONBLOCK; Windows opens its final handle with OPEN_REPARSE_POINT
and rechecks its type/link count. Reads stop at the observed regular-file size
and compare post-read size/link/mtime metadata. This is not a claim of protection
against hostile concurrent directory replacement or all concurrent writers.

Missing current data returns the reference empty manifest only when neither
backup sentinel exists under normal exists semantics. Corrupt current data
never falls back; valid current data ignores locks and backup contents. Reads
never mkdir, lock, migrate, rewrite, capture profiles, query stored executables,
or resolve recorded historical snapshots. This is only the read prerequisite
of future filesystem work; capture, atomic writes and transactions remain later.

### Published 1A.2 implementation and verification checkpoint

PR #18 initially published implementation head
`0f095b553a68338ea05e523a5a45c7b642075e18` (tree
`cbb59c53273601bc4e00c117369a68ae83d21393`) from base
`32786ef3818353e41673c409b5eae7ad4de43f7d`. The focused stack starts with
ledger `db4a90137ea827342a0b61deb3dfad3995ca59c8`, private resource/display seam
`bfbbbe669ef1d6f3fab6870b87e8092c39197989`, repository/platform
`92bff74621617829c79e64eb277eed522b353410`, and the CLI/test head above.
No original Python implementation, old tests, golden fixtures, engine, Menu,
or workflow files changed.

Linux Debug passed 11/11 CTests in 124.70 seconds, including unchanged Python
154 tests in 66.27 seconds, 325 golden cases, 9,106 schema cases, and constrained
stack tests. Targeted final path/argument checks then passed store/golden/stack
3/3 in 8.26 seconds. The complete ASan/UBSan native suite passed 10/10 in
149.49 seconds (`ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=halt_on_error=1`);
only unavailable LeakSanitizer was disabled. Fresh build directories were
`/tmp/c2-store-read-initial` and `/tmp/c2-store-read-asan`, with build and test
commands awaited sequentially.

Review follow-up compares NT aliases as parsed drive/root/components, including
UNC anchor trailing separators, rather than raw lowercased spellings. It adds
144 authoritative PureWindowsPath equality pairs and explicit positive/negative
Greek contextual-lowercase rename fixtures. That expanded store gate passed
Debug in 9.69 seconds and ASan/UBSan in 26.15 seconds. The authored store test
also performed 164 POSIX Store/CLI output-or-failure comparisons, immutable
snapshot lifetime/API checks, resource tests and no-write filesystem snapshots.
Windows-only checks run in the existing Windows CI job; live network-share
reads are not asserted by the pure UNC spelling tests. Initial-head Ubuntu CI
passed, and actual MSVC compiled successfully with its tests still pending at
this checkpoint. Published final-head review and Windows results remain gates;
none of these implementation results self-approve the slice.

The initial-head MSVC job `106057527703` subsequently completed 10/11 CTests:
all original tests passed, but the new store oracle caught mixed separators
in default-directory presentation (`.local/share` versus `.local\\share`).
Commit `8a3991ae48f438c552d1b6c44c740a954c8a7d67` already corrects this by
joining native components; its tree is
`dca154cb249b7f466b792899ea185882ee6b5904`. The failed run took 287.38 seconds,
including 217.06 seconds for unchanged Python tests. It does not establish the
later Windows safety fixtures, which remained after the failed assertion.
A further focused review correction normalizes an empty post-tilde-expansion
path to `.` (empty Windows USERPROFILE), with isolated constructor oracles for
`~` and the current username. Linux's store gate passed again in 8.59 seconds;
Windows reruns and independent final-head approval remain required.

## Store merge and generation-read checkpoint

PR #18 was independently reviewed at final head
`76a8341598db3134417e73a28330fe8d892f40ef` and merged as
`20239c17cbfbce6fbae74e13743c32c109845845`, tree
`b4cdd27a91c1ea5e11d129d4c995a6ecee636725`. Overall 1A is complete.
This supersedes historical pending 1A.2 statements without changing their record.
The reviewer inspected the full diff and resolved all findings. Original Python,
old tests/goldens, engine, Menu and workflows remained untouched. All 28 final
push/PR checks passed. Actual MSVC Windows job 106058685869, run 35503272487,
passed 11/11 CTests in 255.13 seconds (backend 171.45 seconds). Reviewer Linux
verification at preceding head `8a3991ae48f438c552d1b6c44c740a954c8a7d67`
passed 11/11 in 129.32 seconds, including all 154 Python tests in 68.207 seconds
without skips; final empty-home correction passed store/golden/stack 3/3 in
10.12 seconds. Independent 600 exact-output/no-write and 10 POSIX-resolution
cases had zero mismatches. Native ASan/UBSan passed 10/10 in 149.49 seconds;
expanded store sanitizer verification passed in 26.15 seconds. Live UNC share
access remains untested; pure UNC spelling/ancestry and actual Windows junction,
extended local path and Greek-case lifetime tests passed.

Branch `frontend/cpp-generation-read` starts exactly at that merge. Slice 1B is
split into 1B.1 current-generation resolution/history inspection and 1B.2 profile
inspection. Only 1B.1 is in progress. Its direct `resolve_generation` dependency
brings the exact read-only `session_io.capture` and safe-path prerequisites
forward from 3A. Capture writes, atomic publication, locks and all remaining 3A
work stay deferred. This dependency split does not authorize profile decoding,
discovery, execution, mutations, acceptance or GUI work. Capture will retain
complete entries and opaque bytes, compare them with Python equality, and never
fall back from the authoritative current generation. Independent review and
actual final-head Windows CI remain gates; implementation evidence is not approval.

## Slice 1B.1 implementation and review checkpoint

PR #19 publishes read-only generation resolution and history inspection. Initial
implementation head `87c6dacdbccc23a2f432a68ad54c3687c4a30050` has tree
`4f24bb978ad84530d5fa50c1c37d481abdb444d2`; its parent safe-read prerequisite is
`c7dc263a235d2a1aa2f8088ec266f7d43d8b0f68`, following ledger checkpoint
`45a70df0cf767d0aac9574be853a25f0894d7751`. Windows capture test expansion is
`fb8a91307a23eab28620d87829053b1f087fb189`, tree
`90a6097bee230bfa039de693183efd7b39093d66`. Independent review and final-head
Windows CI remain pending; none of these implementation results approve 1B.1.

`Manifest::resolve_generation` binds resolution to that owned validated manifest
and the Store directory retained at its read. Current means that observation's
head, without a hidden reread or a claim of later acceptance freshness. The CLI
reads fresh for its only added command, `managed-state inspect ASSOCIATION`.
`GenerationObservation` owns immutable metadata, root, complete typed entries
and opaque byte blobs. Explicit prior-generation resolution remains available;
its history export rejects unless it is the manifest head, so an intact old
snapshot cannot bypass a missing/corrupt head inspection. Generation and history
presentation retain unknown metadata. Only the reference's three excluded keys
are omitted from import provenance. Recorded executable/helper evidence is never
probed, hashed or executed. Snapshot paths are constructed from validated IDs
and kind, never arbitrary locators; only the selected snapshot is captured.

Private capture preserves sorted depth-first directory entries, unsafe/link/
oversized evidence, 128-entry and 16/32 MiB bounds, and complete bytes. Native
POSIX names decode UTF-8 with surrogateescape; Windows UTF16 retains unpaired
units. Host Path ordering uses Unicode code points and pinned lowercase on NT,
with stable ties. Two full observations compare entries and blobs. Regular reads
use the owned bounded reader, compare original signature and post-read identity,
and additionally check EOF: zero-size virtual files that return bytes cannot be
certified empty. Manifest read limits remain unchanged. These checks detect
ordinary changes, not hostile concurrent directory replacement guarantees.

Linux Debug passed 12/12 CTests in 171.41 seconds (backend 74.06 seconds; all 154
unchanged Python tests in 73.780 seconds), including the 325 unchanged goldens,
9,106 schema cases, store and stack tests. The expanded generation test passed
again in 37.18 seconds. Focused ASan/UBSan generation/store/compatibility/stack
passed 4/4 in 125.16 seconds, generation 91.46 seconds; only unavailable leak
checking was disabled (`ASAN_OPTIONS=detect_leaks=0`,
`UBSAN_OPTIONS=halt_on_error=1`). Builds live in `/tmp/c2-generation-debug` and
`/tmp/c2-generation-asan`; full/focused logs are
`/tmp/c2-generation-debug-full.log`, `/tmp/c2-generation-debug-expanded.log`,
and `/tmp/c2-generation-asan-focused.log`.

The new filesystem gate performs 101 Linux oracle comparisons using unchanged
Python capture/resolve/inspect and exact CLI bytes, plus public observation
lifetime and no-write snapshots. Cases include G0/G1/G2 and explicit predecessors,
missing/corrupt head with intact old generations, metadata/member extras,
opaque bytes, nested directories, missing versus empty, entry/file/total bounds,
Unicode and invalid POSIX bytes, hardlinks, links, FIFO, virtual-file EOF, and
sustained writers. Successful concurrent observations have checked raw framing,
size and hash consistency; rejection is allowed. Windows cases add junctions,
extended paths, file/dangling symlinks and unpaired UTF16 names; privilege/filesystem
limitations print explicitly, so their actual CI coverage must be inspected.

No Python source/old tests/goldens, engine, Menu, or workflow changed. Profile
inspection (1B.2), capture writes and all remaining 3A work, other later slices,
and independent completion/merge remain deferred.

### Review follow-up: observable Windows capability coverage

The coordinator reviewed published implementation `87c6dacdbccc23a2f432a68ad54c3687c4a30050`
and verified production sources remained byte-identical through ledger head
`b282790e9ee78580816b1d4d38a8add92fafa2b9`. Independent Linux verification
passed 12/12 CTests in 169.90 seconds and all unchanged 154 Python tests; 244
capture trees/boundaries and 360 history-output/error comparisons had zero
mismatches. This is review evidence, not final approval or Windows coverage.

The existing workflow suppresses successful CTest stdout, hiding conditional
Windows capability messages. Three narrowly named Windows CTests now separately
exercise actual file-link, dangling-link and unpaired-UTF16-name creation and
Python/native capture comparison. Unavailable creation returns explicit CTest
skip code 77, so ordinary CI logs distinguish Passed from Skipped. The original
main oracle and its assertions remain intact, and no workflow changes are needed.
Non-Windows mode dispatch explicitly returned 77 for all three; the complete
101-case Linux generation gate passed again in 33.92 seconds after rebuilding
(`/tmp/c2-generation-debug-capabilities.log`). Final published-head Windows
results, including these individual capability outcomes, remain required.


## Generation merge and pure profile codec boundary

PR #19 was independently reviewed at head
`68fa21f3421791574e7938b43a9517ca78f2cc31` (six ahead / zero behind
`20239c17cbfbce6fbae74e13743c32c109845845`) and merged as
`e222b5542a12ab2675aba2116a189bd8e2985f58`, tree
`28a9f136d56ffabf812e194371130c50006d278c`. All 28 final checks passed.
Actual MSVC Windows run 35504678102 / job 106062373964 passed 15/15
CTests in 328.56 seconds; separate file-link, dangling-link and unpaired-UTF16
checks all passed without skips. Reviewer exact-production Linux passed 12/12
in 169.90 seconds, including 154 unchanged Python tests in 68.774 seconds
without skips. Independent 244 capture and 360 history comparisons had zero
mismatches. Local Debug passed 12/12 in 171.41 seconds; focused ASan/UBSan
passed 4/4 in 125.16 seconds, with only unavailable LSan disabled under ptrace.
Review resolved the history-head guard, virtual-file EOF, Windows capability
visibility and successful-raced-capture framing. This supersedes earlier
pending 1B.1 status; the merge does not complete overall 1B.

Branch `frontend/cpp-profile-codec` begins at that exact merge for **1B.2 only**.
The remaining profile work is split into 1B.2 pure byte inspection and 1B.3
filesystem inventory/stable-read/set inspection. The new C++17 API explicitly
selects unavailable or in-process native decoding, defaulting conservatively
to unavailable. It takes bytes, kind and dialect, never a helper path. It does
not consult environment variables, access files or run subprocesses. Python
and CLI defaults remain unchanged. The existing Shared codec and original
profile helper remain the authoritative, unmodified baseline.

Unavailable, unknown and unsupported outcomes contain no decoded values.
Exact dialect precedence and case-sensitive kind selection match Python;
non-`sav` kinds use the room length branch. Native candidates require full
input decode/reencode equality before returning any projection. This temporary
memory encoding never modifies stored bytes. Save options remain omitted from
the projection but participate in full-byte verification. Every name byte is
retained, and display alone stops at the first NUL with exact Latin-1 code
points. Raw IEEE fields remain unsigned integer bits; all signed words and
reserved item words retain their values. Immutable owned typed projections
expose no Shared codec, JSON implementation, runtime or process types.
Presentation uses the existing insertion-ordered ASCII encoder, indent 2 and
LF, without changing evidence/hash encoders or historical journal evidence.

A codec candidate proves only byte roundtrip, not semantic validity or working
gameplay. In-process selection is not external-helper availability or trust;
no helper path/SHA evidence is invented. External-helper providers remain 4A.
No inventory, stable read, set association, CLI profile command, discovery,
mutation, normalization, persisted reencoding, runner, acceptance, GUI or
cutover is included. Overall 1B requires the later 1B.3 review. Independent
review of this implementation and final-head MSVC CI remain required.

### Slice 1B.2 implementation and validation checkpoint

The implementation commit object is
`a352af1d2793560d6baefc6b3ac5958b9ad8c752`, tree
`ac82fee010de8154b466d0b5457c44fc1553312a`, following ledger commit
`12d4b76c28ef41ff96dadc60aedc861fce3a8ee8` on branch
`frontend/cpp-profile-codec`. This checkpoint adds only evidence; the final
published head must be recorded by the independent coordinator after review.

Linux Debug passed all 13 CTests in 188.99 seconds, including all unchanged
154 Python tests, 325 golden cases and 9,106 schema cases. The new profile
CTest passed in 15.39 seconds: 865 exact JSON byte comparisons against unchanged
`profiles.codec_inspect` with the original compiled helper or with
`C2_PROFILE_PROBE` unset, plus typed API and ownership assertions. Cases cover
all layout/availability/precedence branches, case-sensitive and non-save kinds,
near/exact lengths, random/zero/FF/pattern payloads, raw signed extremes and
IEEE NaN/infinity/negative-zero bit patterns, every item and reserved-word
position, all Latin-1 bytes, every NUL position and retained trailing bytes.
Direct API checks also cover embedded NULs in kind/dialect and input mutation,
destruction and result copying. Native child tests run with an unusable helper
environment, empty PATH and empty temporary cwd; the directory remains empty.

Focused ASan/UBSan passed profile/compatibility/stack 3/3 in 73.28 seconds
(profile 68.94 seconds), using `ASAN_OPTIONS=detect_leaks=0` only for the ptrace
LSan limitation and `UBSAN_OPTIONS=halt_on_error=1`. Builds/logs are
`/tmp/c2-profile-debug`, `/tmp/c2-profile-debug-final.log`,
`/tmp/c2-profile-asan` and `/tmp/c2-profile-asan-focused.log`.
An initial new test fixture accidentally excluded its intended trailing NUL
by specifying length 15; correcting it to 16 resolved that test-only failure.
The initial run's other 12 CTests passed; the complete successful rerun above
supersedes it. No existing assertions were modified or weakened. Shared codec,
original helper, Python implementation/tests/goldens, engine, Menu, workflows
and persistent formats remain unchanged. These results are not independent
approval; final published-head review and actual Windows CI remain gates.


## Profile codec merge and source-filesystem boundary

PR #20 was independently reviewed at head
`b0863b778fd79e7e77cf415251976e3c7f1aef03`, tree
`dfa99b9a1c06354b247ce0c5fa09b58982ff71e2`, three ahead / zero behind
`e222b5542a12ab2675aba2116a189bd8e2985f58`, and merged as
`7fe929a5b93c6ecff912e36ce66a86619cde74c8` with that same tree. The stack was
`12d4b76c28ef41ff96dadc60aedc861fce3a8ee8`,
`a352af1d2793560d6baefc6b3ac5958b9ad8c752`, and the reviewed head above.
The coordinator inspected the actual GitHub diff, public API, tests and scope;
no findings remained. All 28 final checks passed. Actual MSVC Windows run
35505907698 / job 106065572533 passed 16/16 CTests in 350.28 seconds
(profile 13.08 seconds), with all three filesystem capability tests passed,
none skipped. Isolated reviewer Linux passed 13/13 CTests in 186.12 seconds,
including all 154 Python tests in 68.800 seconds without skips. Independent
580 struct-offset/Python/native exact-byte comparisons had zero mismatches.
Agent Debug passed 13/13 in 188.99 seconds (154 Python tests in 71.179 seconds),
with 865 profile oracle comparisons; ASan/UBSan passed 3/3 in 73.28 seconds,
only unavailable LSan disabled under ptrace. Shared codec, original helper,
Python, old tests/goldens, engine, Menu and workflows were untouched. This
supersedes earlier pending 1B.2 statements without altering historical evidence.

Branch `frontend/cpp-profile-files` starts at that exact merge for **1B.3 only**:
read-only source-profile inventory, read_set, stable_read and inspect_set.
Only their direct `discovery.walk_files` prerequisite moves forward from 2A.
No recognition, reference resolution, fingerprinting, catalog, Genesis, profile
association/refresh writes, store publication, locks, process execution, trust,
acceptance, GUI or cutover is authorized by this dependency. External-helper
execution remains 4A; a profile CLI bridge requiring explicit probe semantics
is deferred with it. The explicit unavailable/native byte codec remains the
only decoding choice, without environment lookup or fabricated helper evidence.

Public `profile_files.hpp` observations own immutable state metadata and bind
the original native root to the initial cwd without collapsing POSIX link/..
components or expanding literal tilde. State objects cannot be constructed from
caller-supplied trusted metadata. The bound root is an observation location,
not physical-root or acceptance authority. Read resolves it afresh following
Python Path.resolve semantics. Missing/non-directory/NUL roots yield empty
inventory; other status errors retain the reference distinction from ignored
scandir errors. Native POSIX surrogateescape and Windows unpaired UTF16 names
remain code points, never locale conversions. Filenames and directories sort
by Python code-point order, including on Windows. Unicode 15.0 decimal digits
use a separately generated/verified narrow table; filename slots retain
arbitrary decimal magnitude. Long-s regex equivalence does not normalize the
captured extension's lowercased kind. Group keys, companion/file ordering,
case ambiguity and diagnostics follow the reference literally.

Source reading has its own bounded actual-byte reader. It allows ordinary
hardlinks, ignores unrelated special files in inventory, rejects changed
members that become special/symlink/escaping files, and reads at most 16MiB+1
actual bytes even for virtual files whose stat extent is zero. It does not
import managed capture's 128-entry/32MiB aggregate/alias-hardlink restrictions.
Windows junction directories are traversed by inventory as in os.walk;
canonical containment is checked at read. Nonsymlink regular reparse files
are followed by the source reader; there is no blanket reparse rejection.
Cloud-provider hydration behavior is not directly exercised by the authored
fixtures. Existing Store and Capture policy is unchanged.

Stable reads compare full member paths and companion metadata, then complete
bytes; old inventory state-file sizes are not a pinned baseline. Inspection
recomputes size/hash/decoded projections and retains raw bytes. All presentation
uses insertion order, ASCII escapes, indent 2 and LF. These observations never
certify an externally atomic SAV/SAB pair, semantic gameplay compatibility,
ownership, fresh acceptance, or safety against hostile concurrent directory
replacement. Ordinary detected races fail closed; no bytes are normalized or
rewritten. Independent review and actual final-head Windows CI remain gates.

A narrow operational cycle guard rejects an entire source inventory when a
resolved directory repeats in the current ancestry. Unlike the reference's
potential repeated junction traversal (which may eventually hit OS path limits),
it reports `profile inventory directory cycle`; it never returns a partial
success. Finite sibling junction aliases remain separate observations. The
guard runs only after successful scandir, retaining ignored permission errors.
Named Windows tests distinguish junction, cycle, hardlink, file-link,
dangling-link and unpaired-UTF16 coverage from capability skips. The authored
POSIX permission test uses a restricted identity when run as root; local
identity dropping is denied with EPERM and is reported as skip 77, not permission
coverage. Actual Linux CI permission coverage remains a merge gate.

### Slice 1B.3 implementation and validation checkpoint

The published implementation object is
`d21bc2f9f1d2a687c396fe468a42eb6e5c84e8e8`, tree
`04ff2f3a1d7499d931766e9e06b3ffeacf9d3de2`, following ledger
`2686647c82f7d1cc5d3b885a1b305690889b2771` and prerequisite
`9841e322311d7192d511b156ba3da7fc8c44748b` from base
`7fe929a5b93c6ecff912e36ce66a86619cde74c8`. Authenticated GitHub object
creation verified every blob and complete tree against the local commits.
The tested local implementation `189751ffb1c3948f106fc6c537ae62d1db972260`
has that exact implementation tree; author/committer metadata accounts for
its different commit ID. This following checkpoint changes only documentation;
its final published SHA belongs in subsequent independent review evidence.

Final sequential Linux Debug completed all 16 registered tests in 233.76
seconds: 15 passed, and the named permission capability explicitly skipped
because changing to the restricted identity returned EPERM. All unchanged
154 Python tests passed in 77.529 seconds, without Python skips. The 325
golden cases, 9,106 schema cases and 865 pure-profile oracle comparisons remain
unchanged and passed. The new source-profile test passed in 32.89 seconds,
with 204 authored cases comparing unchanged Python inventory/stable-read/
inspect-set, original compiled helper projections, exact presentation bytes,
and complete raw bytes. No-write snapshots and owned observation lifetime/
cwd binding assertions passed. Cases cover Unicode decimal blocks, arbitrary
slots, long-s and case collisions, raw names, nested/unknown companions,
symlinks and hardlinks, special-file replacement, actual virtual-file bytes,
16MiB boundaries, more than 128 entries and more than 32MiB source trees,
pre-read membership/companion/byte changes, and sustained active writers.
Successful raced observations retain verified JSON/raw size/hash framing;
rejection is permitted and a detected active change is required. None of this
claims an externally atomic pair or protection against hostile replacement.

Final ASan/UBSan passed all five focused gates in 218.22 seconds: source files
145.22 seconds, pure profile 68.32 seconds, compatibility 2.15 seconds, stack
2.13 seconds, plus the decimal oracle. Only unavailable LeakSanitizer was
disabled under ptrace (`ASAN_OPTIONS=detect_leaks=0`,
`UBSAN_OPTIONS=halt_on_error=1`). Final builds/logs are
`/tmp/c2-profile-files-debug`, `/tmp/c2-profile-files-debug-final.log`,
`/tmp/c2-profile-files-asan`, and `/tmp/c2-profile-files-asan-final.log`.
An earlier full Debug run overlapped a rebuild and encountered executable
startup permission failures in schema/compatibility/stack tests; that invalid
verification run is superseded by the fully awaited, sequential final run.
No assertion was removed or weakened.

The coordinator independently archived the exact local implementation above,
reviewed the actual code/API/tests, and ran 679 differential comparisons over
64 randomized filesystem trees plus native-root edge cases, with zero
mismatches and unchanged source bytes. Its isolated full run completed 15
passed plus the same explicit permission skip out of 16 in 236.21 seconds;
all 154 Python tests passed in 75.588 seconds without Python skips (backend
76.03 seconds), and the new source-files gate passed in 33.77 seconds.
This preliminary evidence does not replace actual published-GitHub review
or platform gates. Actual Linux CI must pass the named permission test;
actual final-head MSVC outcomes for each named filesystem capability must be
inspected. Shared codec, original helper, Python implementation/old tests,
goldens, engine, Menu and workflows remain unchanged. Overall 1B and the
profile CLI/helper cutover are not self-approved by this checkpoint.

### Review correction: extended Windows member separators

Actual PR #21 head `703fee61379005436e72cbd33ade0cb448403ab5` passed
Linux job 106069721343, run 35507514266, with all 16 CTests in 98.93 seconds.
The named POSIX permission test passed in 0.09 seconds without a skip, closing
that host-capability gap. Actual Windows job 106069721393 from that run passed
23/24 tests in 435.57 seconds. All six new profile and three existing capture
capability tests passed, but the main profile-files test failed its existing
extended-root regression (78.72 seconds): nested `case0/trophy00.sab` could
not be opened. Push job 106069673421 exhibited the same failure. These runs
do not establish Windows approval for that head.

The source reader joined a native extended-length root to the observation's
POSIX-style relative member path, then passed retained forward slashes directly
to CreateFileW. Extended-length Windows syntax disables the separator translation
that ordinary paths receive. Correction implementation
`9eff2bd47a166885cdc8c4fe654304a0b3ef7d49`, tree
`1b764245d83834bf254bbde93b0db60e5f6f8fce`, applies Windows-only
`make_preferred()` to the joined local syscall path before all pre-read, open
and post-read checks. It changes separator spelling only: retained domain paths,
bound root identity, extended drive/UNC prefixes, Unicode code units and path
components are unchanged; it performs no lexical normalization. POSIX code,
Store/Capture, Python/reference helper, codecs and persistent bytes are untouched.

The original failing regression remains intact. Added direct reader comparisons
retain literal forward-slash nested member input under an extended root; the
named unpaired-UTF16 capability now also compares its complete extended-root
inventory/read/inspection against Python. Live UNC shares remain untested.
Sequential targeted Linux Debug passed decimal/source-profile tests 2/2 in
30.11 seconds (source-profile 29.79 seconds, all 204 cases). Log:
`/tmp/c2-profile-files-debug-extended.log`. The correction's behavior is
Windows-only, so this local regression run does not validate the Windows fix;
actual fresh final-head MSVC and all final checks remain required. This
checkpoint is bundled before a single branch update, with no weakened assertions.


## Profile filesystem merge and reference/fingerprint boundary

PR #21 was independently reviewed at head
`da2f61c131df25f0f0f59af5459e967dd4da2b59`, six ahead / zero behind
`7fe929a5b93c6ecff912e36ce66a86619cde74c8`, and merged as
`ff0bb74f0481628384672a1271aafc25a5468540`. The exact final tree
`8f54f62fd38fd2b8c4f0b50cbc5367660e42498d` was verified after merge.
The coordinator reviewed the actual GitHub branch, API and complete diff;
679 independent exact JSON/raw comparisons over 64 random trees and root
edges had zero mismatches and unchanged source bytes. Isolated Linux review
completed 15 passed plus one explicit local permission EPERM skip out of 16
in 236.21 seconds, including all 154 Python tests in 75.588 seconds without
Python skips. Agent ASan/UBSan passed 5/5 in 218.22 seconds.

Windows review found the extended-prefix forward-separator read failure
described above; the same implementation agent fixed only the local syscall
path with make_preferred(), retaining persistent/root/code-unit/lexical
semantics. Final PR run 35508095029 passed Linux job 106071182333, 16/16
in 109.57 seconds including the named permission test, and Windows job
106071182389, 24/24 in 320.03 seconds. All six profile and three capture
named capabilities passed without skips; all 28 checks were green. This
supersedes pending 1B.3 gates. Overall 1B read observations are complete;
historical/external-helper selection, execution and the CLI trust bridge
remain explicitly deferred to 4A, without native reinterpretation of pins.

Branch `frontend/cpp-reference-fingerprint` starts at that exact merge for
**2A.1 only**: native_path, resolve_reference, resolved_path, hash_file,
walk_files, content_inventory and fingerprint. The separate later **2A.2**
slice owns recognize, discover, engine_evidence, inspect_instance and
move_candidates. No registration, refresh, relocation, import/upgrade,
manifest writes, catalog, Genesis, CLI discovery, process/trust, GUI, engine
or Menu changes are included.

Reference root resolution must remain separate from native_path foreign-path
rejection and tilde expansion. Every casefold-equal sibling participates in
ambiguity. Content inventory validates unsorted native dirs+files, including
mutable/ignored subtrees, before a separate sorted regular-file traversal.
Source hardlinks remain allowed. Fingerprints stream 1MiB chunks without
managed-capture or profile-size ceilings, compare metadata and the complete
inventory, and hash the unchanged exact ContentFingerprintV1 bytes.
Independent review and actual final-head MSVC CI are required; this boundary
record does not approve the implementation or begin later slices.

### Slice 2A.1 implementation and validation checkpoint

The additive C++17 `content.hpp` API owns reference observations and immutable
content fingerprints. It exposes native locator resolution, reference status
and original spelling/relative POSIX path, resolved paths, and fingerprint
algorithm/hash/counts. No JSON implementation or Python/runtime types cross the
public boundary. Byte totals are arbitrary-magnitude canonical decimal strings.
Inventory metadata stays private and retains signed nanosecond timestamps and
full Windows 128-bit file IDs as decimal integers; compare-only device identity
is not substituted for the Python-visible inode. Existing native path resolution,
Unicode 15.0 casefold/lower and ContentFingerprintV1 encoders are reused only
where their reference semantics match. No Store/Capture/profile authority or
size limit is applied to general content.

Reference observation resolves roots without expanding literal tilde or rejecting
native backslash spelling. Locator native_path retains its distinct rejection
and expansion policy. NUL roots fail before native C-string APIs; NUL reference
components participate in sibling comparison and normally report missing.
PurePath construction drops dot/repeated separators while preserving parent
components and their link-dependent meaning. Found does not imply regular file
or coherent-root shape: a regular-file HUNTDAT yields an empty inventory/hash,
matching the reference; later recognition owns its separate directory checks.

The first inventory pass preserves native dirs+files enumeration and validates
all children, including mutable suffixes and ignored subtrees. A second pass
sorts by Python code point, skips child symlinks, follows Windows junctions,
filters only the reference suffixes/parents and observes regular-file metadata.
Ordinary hardlinks and finite sibling junction aliases remain distinct entries.
A narrow per-ancestry directory-cycle guard fails the entire observation with
`content inventory directory cycle`. This operational divergence from potentially
unbounded Python junction traversal is checked only after successful scandir,
so ignored permission errors remain omitted; there is no global deduplication.

Member hashing streams owned regular handles in 1MiB chunks through the existing
vendored SHA-256, with no arbitrary file-size/entry-count/aggregate ceiling.
Nonblocking POSIX open plus regular-handle checks prevents FIFO substitution
from hanging. Handle and path stability checks fail closed on detected changes;
reference member metadata and complete final inventory are compared separately.
Actual bytes are hashed even when a virtual regular file reports extent zero.
The payload remains sorted [relative,size,sha] arrays, exact compact ensure_ascii
bytes without LF, hashed to lowercase SHA-256. These observations do not establish
an externally atomic tree, hostile-race security, managed ownership or authority.

The test-only line-framed driver accepts native_path, resolve_reference,
resolved_path, hash_file, walk_files, content_inventory, fingerprint and
fingerprint_payload operations. A narrow private phase callback additionally
lets an owned test child pause after initial inventory, after member hashing
before restat, and before final inventory. Production fingerprint supplies no
callback; there is no environment switch, production CLI bridge or caller-supplied
trusted metadata. Tests bound readiness/continuation, kill/wait and close owned
children on failure. Deterministic mutations prove member addition/removal,
posthash metadata change, and POSIX special-file substitution are rejected.
Sustained atomic replacements still require every successful raced fingerprint
to equal a complete valid expected identity, never merely a zero exit status.

The original added membership race assertion could finish at equal inventories
and therefore legitimately produce only valid successes. Both agent and reviewer
saw that test-only scheduling failure. The private phase barriers above replace
that probabilistic requirement; no compatibility assertion was weakened, and
all original 325 goldens, 9,106 schema cases, 865 pure-profile cases and 154 Python
tests remain unchanged. Root preliminary exact-tree review passed the corrected
282-case content gate in 35.07 seconds and 6,890 independent comparisons across
64 random trees and native root edges, with zero mismatches and unchanged source
bytes. That evidence is not final published-head or Windows approval.

Validation commands use fresh external build directories, and each build/test
session is fully awaited before rebuilding its directory:

```sh
PATH=/root/.local/bin:$PATH cmake -S Frontend -B /tmp/c2-reference-fingerprint-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
PATH=/root/.local/bin:$PATH cmake --build /tmp/c2-reference-fingerprint-debug --parallel 2
PATH=/root/.local/bin:$PATH ctest --test-dir /tmp/c2-reference-fingerprint-debug --output-on-failure --no-tests=error
PATH=/root/.local/bin:$PATH cmake -S Frontend -B /tmp/c2-reference-fingerprint-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
PATH=/root/.local/bin:$PATH cmake --build /tmp/c2-reference-fingerprint-asan --target c2-frontend-content-tests c2-frontend-native-tests --parallel 2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 PATH=/root/.local/bin:$PATH \
  ctest --test-dir /tmp/c2-reference-fingerprint-asan -R 'frontend-native-(content|compatibility|stack)$' --output-on-failure --no-tests=error
```

Focused ASan/UBSan passed 3/3 in 86.08 seconds (content 81.33, compatibility 2.43,
stack 2.31), with 282 authored oracle cases, exact observations/payloads, read-only
snapshots, a 48MiB-plus streamed file and 130 source entries, raw names, mutable
exclusions, aliases/collisions, signed timestamps, virtual bytes, and race gates.
Only unavailable LeakSanitizer was disabled under ptrace. Log:
`/tmp/c2-reference-fingerprint-asan-final.log`.

Named Windows file-link, dangling-link, unpaired-UTF16, junction, hardlink and
cycle capabilities distinguish Passed from unsupported skip 77. Linux permission
coverage also has its own named test; local uid/gid dropping returns EPERM and
is explicitly skipped, so actual Linux CI must supply that gate. Actual final-head
MSVC execution remains required; Linux is not a substitute. Live UNC shares and
cloud-provider reparse/hydration behavior remain unexercised. This slice does not
change Python, original tests/goldens, Shared codec/helper, engine, Menu, workflow,
persistent formats, or CLI behavior. Slice 2A.2 and all later operations remain
deferred until independent review and merge.

Final sequential Linux Debug completed all 18 registered CTests in 242.59
seconds: 16 passed and two named permission capabilities explicitly skipped
for local identity-drop EPERM (content and existing profiles). The corrected
content gate passed in 33.75 seconds with all 282 cases; all unchanged 154 Python
tests passed in 68.628 seconds, without Python skips (backend 68.93 seconds).
The original goldens/schema/profile gates also passed. Log:
`/tmp/c2-reference-fingerprint-debug-final.log`. This complete rerun supersedes
the earlier membership-test scheduling failure.

Authenticated GitHub object publication checked every created blob and complete
tree against the local commits. Ledger local `69cf860` maps to remote
`c1630e1eac2c18272527b2f4f5b0d3c25fc5f530`; implementation local
`4324bc8d4b00090e36c650cb57bdb9cf33b5bd07` maps to remote
`f448731daecb002af5df67e8c393f953192f50fa` with identical tree
`166e83affb39a9610fa50588a0d135912120eca6`; deterministic race correction
local `7e68166b5bd734fadab98b63c924e99f9af27781` maps to remote
`88244c08e8425a89d2326e137dd65eacd10e1cc2` with identical tree
`f72d333e38e9e5f75a487820456b351d5765e9d9`. Author/committer metadata explains
commit-ID differences. This following evidence commit is bundled with that
implementation in the initial branch update; its final SHA and actual CI
outcomes belong in subsequent independent review evidence. No merge or
self-approval is performed by this implementation checkpoint.

### Windows active-writer diagnostic checkpoint

Actual head `98f7ab7ab77474097d376c20da9938cc6682a1e0` passed Linux PR
job 106074871578, run 35509519139: 18/18 in 99.67 seconds, with both named
permission tests passed. Windows push job 106074824777, run 35509500990,
passed 30/31 in 456.65 seconds; Windows PR job 106074871661, run 35509519139,
passed 30/31 in 561.82 seconds. The content test failed the same combined
writer-thread-alive/exception assertion in both (91.45 and 123.45 seconds).
All six content, six profile and three capture named Windows capabilities
passed without skips; all other tests passed. These heads are not approved.

Those failure logs omitted the exception and alive flag, so they do not
establish whether the writer errored or failed to stop. This diagnostic-only
checkpoint preserves production bytes and every existing race assertion while
reporting phase, completed replacement counts per observation, rejection count,
thread alive state, bounded join duration, and exception type/repr/errno/winerror/
traceback (plus a live writer stack when available). No failure code is ignored,
no retry introduced, and no timeout increased. Explicit native read/stat handles
still use FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE and RAII close;
the observed Windows cause must be established before choosing a correction.

The local affected oracle gate passed all 282 cases after diagnostic additions
(`/tmp/c2-reference-fingerprint-writer-diagnostics.log`); the exact final writer
diagnostics additionally passed an isolated active-writer rerun and Python syntax
compilation. Production, native driver, CMake, original tests and workflows are
unchanged. Actual Windows diagnosis is pending this narrowly instrumented head;
this is not a claimed Windows fix or a reason to bypass the platform gate.

### Windows writer correction: bounded replacement retry

Diagnostic head `65209441aaa5589a7d3b21642caf997f0f6792bf` established the
actual cause in Windows PR job 106076473458, run 35510132713: the writer raised
`PermissionError`, errno 13, winerror 5 (`ERROR_ACCESS_DENIED`) at
`os.replace(replacement, HUNTDAT/a)`, after two completed replacements. It had
already exited (`alive=False`, join 0.0 seconds); every observation after the
first saw no new replacements and the run had zero detected rejections. This
was a stopped mutation workload, not evidence of a hung writer. The content
test failed in 70.89 seconds; 30/31 CTests passed in 307.23 seconds, including
all fifteen named Windows content/profile/capture capabilities without skips.
No successful fingerprint mismatch was reported. The diagnostic checkpoint
correctly retained the failing assertion and did not approve that head.

Correction local `b786aa75623c500c613035097f56b0c2d265198a`, remote object
`cca648ab5e0895ba1c16244d8a8496807f5757a0`, tree
`c8fa2272b537e9b8f8fae364d6f3ad9639437740`, changes only the authored test
writer. The exact observed Windows error 5 may be retried only from os.replace,
using the same completed replacement file, a two-second per-replacement deadline
and a stop-cancellable two-millisecond wait. Persistent denial remains fatal;
all other error codes, non-Windows errors and write_bytes failures remain fatal.
No reader handle/share flag, metadata/stability check, hash, format, timeout,
Windows skip, or production behavior changes. The precise lower-level source of
the transient Windows denial is not established by that exception alone.

The harness now additionally requires multiple completed replacements, a measured
replacement-count increase during an observation, and an actual hash/inventory
change rejection. Every successful race result must still equal the complete
expected A or B fingerprint. Original alive/error/rejection assertions and all
diagnostics remain. Small synthetic checks on every host prove retry/success,
non-Windows error 5 rejection, Windows error 32/2/no-code rejection, deadline
failure and cancellation. These are harness-policy checks, never claimed as
actual Win32 evidence. Actual corrected Windows execution remains required.

The affected Linux Debug gate passed all unchanged 282 authored content cases
plus those harness checks. Its sustained writer completed 4,873 replacements,
advanced during all sixteen observations and produced sixteen actual detected
change rejections, no writer errors, and bounded clean termination. Log:
`/tmp/c2-reference-fingerprint-writer-fix-debug.log`. Production/native-driver/
CMake bytes remain exactly those already reviewed and fully tested; the original
Python implementation/tests/goldens, engine, Shared codecs, Menu and workflows
are untouched. This correction and its verification ledger are bundled into one
non-force branch update; final-head actual MSVC and independent review remain
gates, with no merge or self-approval.

The same affected 282-case gate also passed against the existing ASan/UBSan
production build with only unavailable LSan disabled. Its writer completed
8,863 replacements with overlap in all sixteen observations and sixteen detected
change rejections, no writer errors and bounded clean termination. Log:
`/tmp/c2-reference-fingerprint-writer-fix-asan.log`. Both correction verification
sessions were fully awaited before this bundled checkpoint.

### Windows race coordination: owned observation lifetime

Head `5c086b4326f2a1794f2536532ba7cb42a1444680` had divergent actual Windows
outcomes: push job 106078369422, run 35510845424, passed 31/31 in 535.47 seconds
(content 130.91 seconds); PR job 106078374864, run 35510847646, failed 30/31 in
528.22 seconds (content 123.24 seconds). All fifteen named capabilities passed.
The red run exhausted the two-second replacement deadline after six completed
replacements and 314 WinError 5 retries; the writer exited, and later observations
had no new mutations. It reported zero unconstrained change rejections. The green
CTest log suppresses successful writer output, so its retry counts and individual
observation durations are unknown. These results do not identify which OS handle
caused the denial, and a larger arbitrary timeout would not establish correctness.

Correction local `4c58ea143d5b5bd5bfa6097f0e4f57d5ed186924`, remote object
`ed247767584c193d7de1f7ab90b9813ae8b48132`, tree
`0c85231cad455c8c23b543187a078383c6c4fa56`, changes only authored test
coordination. A small locked lifecycle clock subtracts both accumulated completed
reader intervals and the current interval from monotonic time. The two-second
idle-denial budget therefore excludes an owned observation even if the writer
is descheduled across its entire active-to-idle transition. Synthetic regressions
exercise that 200-second interval without waiting, alongside all previous fatal
error/deadline/cancellation checks; they do not claim Windows execution coverage.

Each of the sixteen public native fingerprint calls still runs with the sustained
writer and must return an exact complete A/B fingerprint on success. Each owned
child retains the existing 90-second timeout and is fully awaited before closing
its lifecycle interval. Before the next child starts, the harness requires a new
successful replacement while idle; persistent denial cannot be hidden by starting
another reader. The existing bounded stop/join and fatal writer-error assertions
remain. Retry remains restricted to Windows os.replace and actual error 5; all
other errors remain fatal. The test records individual observation durations,
active/idle retry counts, mutation counts and every idle progress handoff.

Mandatory detected-change proof now comes from two deterministic atomic replacements
on the same 4MiB member at both existing private phases: initial_inventory and
member_hashed before restat. Each requires a successfully completed replacement
and a specific changed-while-hashing rejection. Those paused mutations receive
no lifecycle exemption, so their short bound prevents a paused-child/retry deadlock.
The exact required phase pair is asserted directly. Unconstrained rejection and
overlap counts remain visible diagnostics rather than assumptions that Windows
must permit rename while a reader is open. No valid-success, no-error, bounded
cleanup, original oracle, or actual replacement assertion is removed. There is
no new production callback, API, environment switch, format or safety policy.

Affected Linux Debug and ASan/UBSan gates passed all original 282 authored cases
plus the two atomic-replacement cases (284 total). Debug completed 5,066 writer
replacements, all sixteen idle handoffs, sixteen unconstrained detected changes
and both forced replacements. ASan/UBSan completed 9,077 replacements with the
same handoff/detection outcomes. Both writers terminated cleanly without errors.
The coordinator requested a final assertion-clarity edit to name the exact two
required phases directly; it preserves the checked outcomes, and syntax compilation
passed. Logs: `/tmp/c2-reference-fingerprint-lifecycle-final-debug.log` and
`/tmp/c2-reference-fingerprint-lifecycle-final-asan.log`. Only unavailable LSan
was disabled, as before; both verification sessions were fully awaited.

Production, public/private C++ implementation, differential driver, CMake, original
Python/tests/goldens, Shared codecs, engine, Menu and workflows are unchanged.
This scoped correction and evidence are bundled into one non-force branch update.
Actual fresh Windows CI and final independent review remain required; the green
push on the preceding head does not approve its red PR outcome or this correction.


## Reference/fingerprint merge and discovery observation boundary

PR #22 on `frontend/cpp-reference-fingerprint` was independently reviewed at
`58b0493cc042ae0fcefe096bde1aafd28b7666c5`, nine ahead / zero behind
`ff0bb74f0481628384672a1271aafc25a5468540`, and merged as
`a4b84d9d5532ae57004d51f26558a9fdf0486109`. The tested and merged tree is
`2b5acb831720ce8b51a6b0909f65584c98ca4d0e`. Root reviewed the actual base,
stack, full code, tests and API; 6,890 exact comparisons across 64 random trees
and root edges had zero mismatches and unchanged source bytes. Final independent
284-case verification (`/tmp/c2-reference-review-final/lifecycle-root.log`)
completed 5,049 writer replacements, sixteen idle handoffs, sixteen local
unconstrained change rejections and both forced atomic-replacement rejections.
Agent Debug and ASan/UBSan 284-case outcomes remain recorded above.

All 28 final checks were green at 13:03:07 UTC. Actual Linux PR run 35511761736,
job 106080770957, passed 18/18 in 124.41 seconds (content 15.96 seconds), both
named permission capabilities Passed without skips. Actual Windows PR job
106080771065 passed 31/31 in 539.24 seconds (content 130.02 seconds); Windows
push run 35511760493, job 106080767877, passed 31/31 in 442.92 seconds (content
94.99 seconds). Both Windows jobs passed all fifteen named content/profile/
capture capabilities without skips. The earlier Windows writer false failures,
diagnosis and lifecycle corrections above remain historical evidence; this final
review supersedes pending approval with no unresolved current correctness finding.
The per-ancestry junction-cycle operational distinction and untested live UNC /
cloud-provider behavior remain caveats.

Branch `frontend/cpp-discovery-observations` starts at that exact main merge for
**2A.2 only**: read-only recognize, discover, engine_evidence, get_instance,
inspect_instance and move_candidates over the filesystem and immutable validated
Manifest. Recognition is content evidence, never execution or trust certification;
engine evidence remains independent from content identity. Full retained baseline
values and Python semantic equality are required, including unknown metadata.
Only minimal private prerequisite seams may move forward. No CLI bridge,
register/refresh/relocate or manifest mutation (5B), catalog (2B), Genesis,
process trust/execution (4A), acceptance, new persistent format, UI, engine/Menu,
generalized architecture/performance work or broad refactor belongs here.
Historical/external codec-helper selection/execution and the profile CLI bridge
remain explicitly deferred to 4A without reinterpretation of historical pins.
Independent review and actual final-head MSVC CI remain mandatory gates.

### Slice 2A.2 implementation and compatibility checkpoint

The additive `discovery.hpp` API exposes recognize, discover, engine_evidence,
get_instance, inspect_instance and move_candidates. Owned immutable observations
retain insertion order, diagnostic order, unknown metadata and exact value kinds;
only standard-library types cross the public boundary. Executable observations
are an optional owned vector: a foreign instance's unobserved field stays absent,
while a missing/native root's observed empty list stays present. Typed path and
change/review queries likewise preserve absence. Instance observations outlive
both Store and Manifest without rereading disk or projecting the baseline.

A narrow private ManifestAccess seam reads retained validated instance values and
read policy. It neither exports/reparses the manifest nor changes authority. Full
current revision and engine baselines use the existing Python semantic equality;
unknown fields participate, integral float/bool values admitted by revision-history
membership remain intact, and unrelated nonfinite metadata does not block
inspection merely because presentation cannot encode it. The private raw-baseline
test seam exercises absent-engine default behavior unreachable through today's
validated Manifest; the public API always obtains instances from that Manifest.

Recognition constructs native Path spelling, expands user and resolves without
native_path's foreign/drive-relative rejection. Missing-root shape returns early.
Reference diagnostics and map pairs preserve exact order; MAP enumeration follows
regular aliases and permits colliding MAP spellings when the RSC reference is
unique, whereas executable symlinks are excluded. Executable Path sorting uses
pinned NT lowercase or POSIX code points; map strings sort by code points on every
host. Content recognition is computed before the missing-engine advisory, never
certifies execution, and never substitutes for fingerprint policy.

Resource-script evidence reads at most 8MiB+1 actual bytes from an owned regular
handle, independent of reported extent. Its ASCII byte-regex equivalent uses the
exact word/whitespace/case rules without UTF-8 decoding, locale classes or catalog
parsing. Ordinary aliases, hardlinks and nonsymlink regular reparse sources are
allowed. POSIX nonblocking open rejects a substituted FIFO instead of blocking;
this is an explicit operational fail-closed distinction, not managed ownership.
No capture/profile member, entry or aggregate ceiling is imported. Existing
fingerprint bytes, hashes and primitives remain unchanged.

Discovery follows sorted os.walk directory traversal, excludes child symlinks
before recognizing parents and prunes only HUNTDAT children. Siblings, nested
games, finite junction aliases and permission omissions remain observable. The
per-ancestry cycle guard runs after successful enumeration and fails the whole
observation with `discovery directory cycle`; it never deduplicates aliases or
returns silent partial success. As in prior slices, this is an operational
difference from potentially unbounded Windows junction traversal. Live UNC shares
and cloud-provider hydration/reparse behavior are not asserted by local fixtures.

Inspection separates fresh filesystem evidence from the full retained baseline.
Foreign shape contains no fabricated path, capabilities or map pairs. Pending
engine relocation review remains required after bytes return to the baseline.
Move candidates fingerprint directly, preserve manifest insertion order, use
Path.exists semantics for dangling old locations and never require coherent-root
recognition. No registration, refresh, relocation, manifest write, execution,
trust certification or production CLI bridge is added.

The authored test-only line-framed adapter drives public APIs and exact presentation
bytes against unchanged Python. Its 634 local oracle cases cover all 256 byte
values at resource-regex boundary and whitespace positions, exact/over 8MiB reads,
zero-extent virtual bytes, missing/incomplete/foreign shapes, content-only roots,
Unicode and raw native names, MAP/RSC ambiguity, trophy exclusion, nested pruning,
source aliases, independent mutable/content/engine changes, unknown/nonfinite and
numeric baselines, pending review and candidate order. Typed assertions test
absence versus observed-empty and copies after Store/Manifest destruction. An
owned bounded child retains a baseline while the fixture rewrites the disk
manifest and engine, then proves inspection compares against the old baseline.
Read-only source/manifest snapshots remain unchanged by every observation.
Additional native-only FIFO rejection and more-than-128-entry / 36MiB-engine
fixtures demonstrate the operational boundary and absence of managed limits.

Named Windows file-link, dangling-link, unpaired-UTF16, junction, hardlink and
cycle CTests require real fixture creation and distinguish Passed from unsupported
skip 77. Extended local roots are exercised by each supported noncycle capability.
The named POSIX permission test distinguishes denied-scandir omission from
unreadable-script rejection; local identity dropping is denied with EPERM and
reported as an explicit skip, not permission coverage. Actual Linux CI and actual
final-head MSVC outcomes remain gates for independent review.

Local implementation `63673b651fab66302e3dabf8f3da5ad43d1d8eb8` and remote immutable
object `fa15bf816a4259f711fd68f0d1120eff8a7ad505` have the identical tree
`868218d3c49aa51a6c9321389a984cd6b511d090`. Its private prerequisite local
`8b5a0d6fa89363c09fd6efe56053f6671738ee09` maps to remote
`b802663cd174baa83c6ef12c6fd2fa69f61b18d9`, tree
`f1c735dcaebb53b59e524651055063a10af8fa96`; initial ledger local
`352ae0131e7c4038394b2a8c630e526b550cb155` maps to remote
`1232b78a4c1b5ef0e795ca6b12ac66d42fa0487c`, tree
`85d9b935f4fdfbdafbdc2f6e5a2b2b52c3b8083a`. Every blob and complete tree was
checked against local Git objects. Author/committer metadata accounts for commit
ID differences. This following validation ledger is bundled before initial branch
publication; its final SHA belongs in independent review evidence.

Preliminary root review independently compiled a frozen matching production/header/
driver snapshot in `/tmp/c2-discovery-review-initial/build` and ran 253 separate
exact JSON/value comparisons across 55 randomized roots plus targeted raw-byte,
8MiB, alias/collision, discovery, retained/nonfinite/numeric baseline, foreign,
pending-review and candidate-order cases. There were zero mismatches and source/
manifest bytes and metadata were unchanged. Log:
`/tmp/c2-discovery-review/independent-result.log`. Root reviewed the full new code,
API and tests; its requested typed executable-presence correction is included.
This is preliminary evidence, not published-head or Windows approval.

Verification uses the immutable source archive `/tmp/c2-discovery-astra-final-source`
and separate `/tmp/c2-discovery-astra-final-debug` and
`/tmp/c2-discovery-astra-final-asan` build directories. Each owned process is fully
awaited; no binary directory is rebuilt while its tests run. The sanitizer command
uses `-fsanitize=address,undefined -fno-omit-frame-pointer`, with
`ASAN_OPTIONS=detect_leaks=0` only for unavailable LSan under ptrace and
`UBSAN_OPTIONS=halt_on_error=1`. Targeted discovery/store/compatibility/stack
ASan/UBSan passed 4/4 in 152.42 seconds (117.03, 30.94, 2.28 and 2.15 seconds).
Log: `/tmp/c2-discovery-astra-final-asan.log`.

Final full Debug completed all twenty registered CTests in 287.76 seconds:
seventeen Passed and three named POSIX permission capabilities explicitly Skipped
for local identity-drop EPERM (discovery, content and profiles). The new 634-case
discovery gate passed in 20.65 seconds. All unchanged 154 Python tests passed in
67.746 seconds without Python skips (backend 68.08 seconds), using the actual
compiled profile, launch-argument and native-session probes. Existing content,
profile, schema, original 325 goldens and constrained-stack gates passed unchanged.
Log: `/tmp/c2-discovery-astra-final-debug.log`; complete child output is in its
build directory's `Testing/Temporary/LastTest.log`. Both final verification
sessions were fully awaited before publication. Python implementation/old tests,
original goldens, Shared codecs/helper, engine, Menu and workflows are unchanged.
No later slice is started and this checkpoint does not self-approve 2A.2.


## Discovery merge and pure catalog boundary

PR #23 was independently reviewed at head
`f9bf8a7e6a74e761d98a4a86941556924cf4fedd` and merged as
`6f48c387ec05bbb021753182d975989b5e501bb6`, matching tree
`032904d38e49de2c05d7a9cdd9fe721f11c7c135`. Its four-commit stack is
`1232b78a4c1b5ef0e795ca6b12ac66d42fa0487c`,
`b802663cd174baa83c6ef12c6fd2fa69f61b18d9`,
`fa15bf816a4259f711fd68f0d1120eff8a7ad505`, and the reviewed head.
Root inspected the actual GitHub base, stack, full diff, public API, new tests
and unchanged old tests. Independently compiled matching source passed 253
exact-value/display-byte comparisons over 55 random roots and targeted cases,
plus all 634 authored cases, with source/manifest bytes and metadata unchanged.
The typed executables absent-versus-observed-empty finding was corrected; no
correctness finding remains. Local Debug completed 17 Passed plus three explicit
permission EPERM skips out of 20 in 287.76 seconds; all unchanged 154 Python
tests passed in 67.746 seconds without skips using actual compiled probes.
Actual focused ASan/UBSan passed 4/4 in 152.42 seconds; only LSan was unavailable.

All 28 final-head CI checks were green. Linux PR run 35513474502 / job
106085347354 passed 20/20 in 130.75 seconds, all three permission capabilities
Passed without skips. Windows PR job 106085347421 from that run passed 38/38
in 593.39 seconds (discovery 37.41 seconds). Windows push run 35513444472 /
job 106085268209 passed 38/38 in 557.95 seconds (discovery 29.88 seconds).
Both Windows jobs passed all 21 named capabilities without skips. Live UNC and
cloud-provider behavior remain unexercised; cycles and substituted FIFOs retain
the documented fail-closed operational distinctions. This supersedes pending
2A.2 review statements above; overall 2A observations are complete.

Branch `frontend/cpp-catalog-parser` starts at that exact main merge for
**2B.1 only**: pure byte/token/tree parsing and scalar/attribute/block queries.
Filesystem projection is separately bounded as 2B.2. Source is opaque Unicode
metadata, never a validated filesystem path. Raw bytes, tree insertion order,
source/line metadata, arbitrary decimal values and diagnostic order are retained.
The separate scalar conversion follows the pinned CPython 3.12 default 4300-digit
integer limit; it does not change private JSON storage or raw-tree parsing.
No text_reference/project, filesystem policy, CLI bridge, Genesis/planning,
rank/economy/equipment semantics, mutation, execution/trust/acceptance, UI,
engine/Menu, format changes or broad refactor is included. Independent review
and actual final-head MSVC CI remain gates; this checkpoint does not approve 2B.1.

### Slice 2B.1 implementation and compatibility checkpoint

The additive standard-C++17 `catalog.hpp` exposes only pure parse_script,
scalar, attribute and blocks operations in `c2::frontend::catalog`. Script and
Node are immutable owned handles with typed source/name/line/raw/attribute/
diagnostic queries and complete ordered presentation exports. Child handles and
copies retain the underlying full tree after every original Script dies. Script
also owns every original input byte, including text after a root-level dot;
SHA-256 always covers the complete input. Source is opaque Unicode metadata and
may contain NULs, supplementary code points or unpaired surrogates. A direct
native assertion separately verifies adjacent surrogate code points without the
JSON test transport combining their escaped spelling into a supplementary point.

The scanner reproduces the ordered Python regex finditer over Latin-1: comments
start only at a new match, quotes must close before LF, unmatched quotes are
skipped, and generic tokens may already have consumed embedded double slashes.
Latin-1 Unicode whitespace includes NEL, NBSP and U+001C–U+001F; only LF increments
line. Pending tokens cross LF unless an equals token is present. The first equals
alone divides assignments, including empty key/value; braces and root-only dot
flush in reference order, and diagnostics retain source/line/order exactly.
Actual bytes are bounded at 8 MiB and the 128-frame nesting guard includes root.
No normalization, scalar coercion or interpreted precedence alters the raw tree.

Scalar first strips matching outer quotes, then recognizes ASCII optional signs
and pinned Unicode 15.0 Nd digits. Accepted integers own canonical arbitrary
magnitude decimal strings, including leading-zero and negative-zero normalization;
there is no binary64/int64 narrowing. The full decimal match precedes conversion:
4301 digits followed by a nondigit remain a string. A full decimal match above
4300 digits throws a distinct ScalarConversionError with the reference error;
leading zeros count and a sign does not. This fixed CPython 3.12 default conversion
baseline is separate from unchanged unlimited private JSON decimal storage.
Explicitly reconfigured Python int limits are not claimed by this API. Raw parsing
of longer digit strings remains successful within the byte/nesting bounds.
Attribute folds only stored keys and returns absent for zero/multiple matches;
blocks folds only stored top-level names, retaining all matches in order. Neither
query folds the requested spelling or interprets catalog rank/economy semantics.

The original Python implementation, all old native/Python assertions and goldens,
Shared codecs/helper, engine/Menu, workflows, CLI and persistent formats are
unchanged. New compact authored goldens are regenerated by the unchanged Python
catalog. The line-oriented test adapter accepts parse/typed/attribute/blocks with
`bytes_hex` and independent Unicode `source`; attribute adds `node_path` child
indices and `key`, blocks adds `name`, scalar takes `raw`. Success includes exact
Python-equivalent `value` and ASCII indent-2/LF `json`; errors distinguish catalog,
scalar-conversion and unexpected failures. It is a test executable only.

The 2,276 authored oracle cases cover all Latin-1 bytes in five token contexts,
comments/quotes/CRLF/NUL/high bytes, multiline pending/equality/braces/dot,
diagnostic ordering, duplicates and casefold request asymmetry, every Unicode
Nd block, arbitrary signed decimal values, 4299/4300/4301 conversion boundaries,
leading zeros, quote/nondigit bypasses, depths 126–129, runtime-generated exact/
over 8 MiB input and ignored tails, long raw values, complete typed metadata and
owned lifetimes. The initial authored run exposed only a test transport ambiguity:
adjacent Python surrogate code points were combined by JSON decoding before native
input, while its expected value was computed before that transport. The corrected
fixture separates those points and retains the direct native adjacent-source
assertion; no production change or existing assertion was weakened.

Root independently compiled the matching production source and passed 14,718
separate exact value/display-byte/failure comparisons in 23.36 seconds: 10,705
parse, 70 blocks, 214 attribute and 3,729 scalar cases. These include each Latin-1
byte in six contexts, 4,096 token triples, 5,000 randomized byte/grammar cases,
depths/bounds, all 680 Unicode Nd digits, duplicate/case queries and conversion
ordering. Script/log: `/tmp/c2-catalog-review/independent_catalog.py` and
`/tmp/c2-catalog-review/independent-result.log`. This preliminary independent
source evidence does not replace actual published-head review or Windows CI.

Implementation local `5590f8233f1fb780acca0a15d644d3b2b43669e2` maps to remote
immutable object `03d7b5eab2bca7cc17ab9b634efa401fe65b9b37`, both with tree
`d856ab2a84dfefd1d324e6819bb78d02e07c971e`. Preceding ledger local
`3fe0e859ec4b375e37fc705d6af0be2f0fe014cf` maps to remote
`d1222d5121db65682c3fb6a7020059efebcd9fac`, tree
`e2397e9415b08ff8b05ebebef1fa422701e0c359`. Every blob and complete tree was
verified against local Git objects. Commit metadata explains different commit
IDs. This following verification ledger is bundled before branch publication;
its final head belongs in subsequent independent review/merge evidence.

Verification uses the immutable archive `/tmp/c2-catalog-astra-final-source`
and separate `/tmp/c2-catalog-astra-final-debug` and
`/tmp/c2-catalog-astra-final-asan` build directories. Each owned process is fully
awaited and no binary directory is rebuilt while tests use it. Sanitizer flags
are `-fsanitize=address,undefined -fno-omit-frame-pointer`, with
`ASAN_OPTIONS=detect_leaks=0` solely for unavailable LSan under ptrace and
`UBSAN_OPTIONS=halt_on_error=1`. Actual focused ASan/UBSan passed 5/5 in 63.87
seconds: all 2,276 catalog cases in 59.18 seconds, compatibility in 2.12,
constrained stack in 2.09, plus catalog and reused Unicode decimal oracles.
Log: `/tmp/c2-catalog-astra-final-asan.log`.

Final full Debug completed all 22 registered CTests in 289.09 seconds: nineteen
Passed and three named POSIX permission capabilities explicitly Skipped for local
identity-drop EPERM (discovery, content and profiles). The new catalog gate passed
all 2,276 authored cases in 25.20 seconds. All unchanged 154 Python tests passed
in 67.169 seconds without Python skips (backend 67.49 seconds), using the actual
compiled profile, launch-argument and native-session probes. The original 325
goldens, schema/profile/content/discovery and constrained-stack gates passed
unchanged. Log: `/tmp/c2-catalog-astra-final-debug.log`; full child output is in
its build directory's `Testing/Temporary/LastTest.log`. This complete rerun
supersedes the initial test-only surrogate-transport failure. Every owned local
verification process was fully awaited before publication. Actual final-head
MSVC/Linux CI, independent published-head review and merge remain gates; no
later slice is begun and this implementation does not self-approve 2B.1.

### Windows oracle checkout correction

Original PR #24 head `7e1a6ad2a55f78fdc157aa33d862f0de2683afd9` passed
Linux PR run 35515092033 / job 106089575530, all 22 CTests in 144.29 seconds,
including all three permission capabilities without skips. Actual Windows push
run 35515090640 / job 106089571884 completed 39/40 in 618.77 seconds; Windows
PR job 106089575396 completed 39/40 in 657.81 seconds. Both failed only the
new catalog golden regeneration check, `Catalog golden differs from unchanged
Python`. Native catalog passed all 2,276 cases in 33.82 seconds (push) and
35.06 seconds (PR); all old tests and all 21 named Windows capabilities passed
without skips. Those results do not approve the failed oracle head.

Root independently identified the checkout-byte cause: the new golden lacked
an LF attribute. With core.autocrlf=true, Git's actual checkout filter produced
35,020 bytes containing 979 CRLF sequences instead of the authoritative 34,041
LF bytes. Replacing those CRLF sequences with LF exactly recovered the unchanged
Python generator output. The focused correction adds only
`tests/catalog/golden.json text eol=lf` to the existing Frontend/.gitattributes.
It does not broaden attributes across the repository, change expected fixture
data, normalize input in the oracle or relax its read_bytes comparison.

After the correction, the actual command
`git -c core.autocrlf=true cat-file --filters --path=Frontend/tests/catalog/golden.json HEAD:Frontend/tests/catalog/golden.json`
returns exactly the unchanged committed 34,041 bytes, with 979 LF and zero CR.
Golden SHA-256 remains
`e3da4e8210f3fc3e3134d21647fac544ddab6caae614bfbf946486216c0339e2`.
The unchanged generator's --check passes. A separate temporary fixture tree with
the unchanged generator/reference accepts those filtered LF bytes, rejects an
intentional CRLF conversion, and rejects a deliberate content mutation with the
original error. Temporary corruptions are removed and the working golden is
unchanged. Verification log: `/tmp/c2-catalog-lf-fix-verification.log`.

Production/parser/API, private JSON, workflows, all golden expected data and old
tests are unchanged. Existing compiled-source, native differential and sanitizer
evidence remains applicable; no redundant full local rerun is claimed. This
attribute plus ledger correction is published as a focused non-force follow-up.
Actual final-head Windows/Linux CI and independent review remain required; no
merge or later slice is authorized by this implementation checkpoint.

### Slice 2B.2 implementation checkpoint

The additive `catalog.hpp` API adds `text_reference` and `project` with owned
immutable `TextObservation`, `Entry` and `Projection` handles, typed queries
and exact Python-order `export_json`. A private `catalog_internal.hpp` seam
shares the parser's handle representation with the sibling projection TU so
parsed scripts embed without re-parsing exported JSON; no foundation helper
changed. Root resolution, reference resolution, walking, prefix reads and
SHA-256 reuse the existing native pieces. Script size is checked by stat
before a read bounded just past 8 MiB; text references are gated by regular
file, strictly greater than 1 MiB is `too-large`, otherwise actual bytes are
split with Python `str.splitlines` semantics over the Latin-1 projection.
The unusual-label regex is reproduced over the Latin-1 domain with the
Unicode word table and ASCII-only case folding that Python exhibits there;
`\b` over labels beyond Latin-1 is unreachable from parsed scripts and throws.
Arbitrary-magnitude AI values keep decimal arithmetic for `DINO{ai-9}` and
numeric duplicate-AI ordering; conversion errors propagate as
`ScalarConversionError` exactly where the reference raises `ValueError`.
Substituted FIFOs and directories at found script/text references fail closed
where Python would block or raise, as in prior slices. No CLI, Genesis,
mutation, Python or golden change is included.

The new `frontend-native-catalog-projection` CTest authors temporary trees
only and compares exact display bytes, compact values, error kinds and
messages, typed handles retained after every projection dies, and unchanged
source snapshots: 257 cases including MENU/RES/both, missing/ambiguous/
unsafe scripts, exact and over-limit scripts, multiple blocks, AI edge
cases, every Latin-1 byte around instruction words, int/str/blank labels,
declared reference kinds, surplus and non-integer prices, slot-six
combinations, 4/5/6 accessories with conflicts, 1 MiB boundaries, every
separator, high bytes in text and names, `.map`/`.c2map`/`'.map'` files,
explicit areas, conversion limits, eight dialect hints and forty random roots.

Verification used separate external Debug and ASan/UBSan build directories
with a pinned CPython 3.12.14 (Unicode 15.0.0) oracle, each session fully
awaited. Full Debug passed all 23 CTests in 109.00 seconds (projection 17.66
seconds; all unchanged 154 Python tests in 19.118 seconds; the three POSIX
permission capabilities Passed under the local identity). Focused ASan/UBSan
with `ASAN_OPTIONS=detect_leaks=0` (LSan unavailable under ptrace) passed 4/4
in 97.39 seconds (projection 65.15, catalog 30.76). Actual Windows/Linux CI,
independent review and merge remain gates; this checkpoint approves nothing.

### Slice 2C.1 implementation checkpoint

The additive `planning.hpp` API ports `genesis.observer_policy`,
`genesis_hunt.hunt_policy` and the evaluation core of `launch.prepare` as
pure functions over supplied observations: no filesystem reads, store lock,
pin snapshot/capture, codec-helper or engine execution, id/time generation
or manifest write. `Revision` (from `revision_of(InstanceObservation)`),
`Selection` (typed constructors reproduce the plan_observer and native-hunt
CLI key orders), `catalog::Integer` slot and optional-integer score (nullopt
is every non-exact-integer kind, which the reference refuses identically)
yield an immutable `GenesisPlan`; `begin_launch(Manifest, association,
DiscoveryObservation, LaunchSelection, id, created_at)` returns a
`LaunchRequest` that is final for an unrecognized installation and otherwise
completed by `evaluate_launch(request, Projection, StateObservation)`.
`StateObservation` is consumed only; its native producer (the
refresh_association wrapper and its `last_observation` write) is 2C.2. Every
reference check is evaluated over the retained values through the private
seams of the preceding commit, including the hunt contiguous-identity,
non-slot-six stem and `ai < 10` checks that a fresh native projection cannot
fail; Python dict equality (numeric equality, exact key set) pins the
revision, `str.lower`/`strip`/whitespace and `in` semantics follow the
schema helpers, cost sums and bounds use canonical decimals, and the
`1 << ordinal` masks are computed only behind the reference's ten-entry gate.
Errors are `planning::Error` with the reference messages; reference
TypeError paths (unhashable or non-iterable selections, unorderable native
scores) throw `std::invalid_argument`. No CLI, wrapper, Python, golden,
engine/Menu or format change is included.

The new `frontend-native-planning` CTest drives authored trees and stores
only: pinned and near-miss revisions (each field, extra/missing keys,
int/float/bool/text kinds, manifest-sourced), dialect hints and script
families, every expected count off by one, modifier and ambiguity
diagnostics, slots and scores at every boundary, every area/license/weapon
ordinal, slot-six candidates, labels including Latin-1 NBSP, unresolved,
negative, int32-boundary and arbitrary-magnitude prices, selection shapes
and kinds in reference check order, and for the launch core an unknown
association, unrecognized/foreign/missing installations, every diagnostic
branch alone and combined, duplicate/unknown selections, ordinals at and
above ten, supplied state kinds and shapes, manifest provenance edits and
engine evidence. 602 cases compare exact display bytes, values, error
kinds and messages, typed handles retained after every input dies, and the
typed API against the supplied-value seam.

Verification used separate external Debug and ASan/UBSan build directories
with the pinned CPython 3.12.14 oracle, each session fully awaited. Full
Debug passed all 24 CTests in 128.11 seconds
(planning 22.05 seconds; all unchanged 154 Python tests in
18.649 seconds; the three POSIX permission capabilities Passed under the local identity). Focused ASan/UBSan with
`ASAN_OPTIONS=detect_leaks=0` (LSan unavailable under ptrace) passed
2/2 in 126.81 seconds (planning 62.80, projection 64.00) with no sanitizer report. Actual Windows/Linux CI, independent review and merge
remain gates; this checkpoint approves nothing.

### Slice 3A implementation checkpoint

Private `native/src/store_write.hpp` (no public header, no CLI, no Python
change) ports `atomic_write`, `Store.lock` (`WriterLock`),
`Store.transaction` (`transaction` over an authored mutation callback),
`now`/`new_id`, `session_root` and `write_blobs`, reusing `store_paths`
safe paths and the reviewed reader, the 1A validator, the 1B.1 capture and
the private encoders. Foundation extension in its own commit:
`compat::dumps` reproduces `json.dumps(value, sort_keys=...)` with default
separators and `allow_nan=True` (the transaction change comparison and the
lock text), and `schema::valid_id` is shared. The capture was confirmed
against `session_io.capture` for the write side (two passes, 128 entries,
16/32 MiB, link/alias/unsafe/oversized typing, name checks) and reused
without extension.

Semantics: the temporary is `.pending-` plus eight characters from the
mkstemp alphabet, created exclusively (0600 and `O_NOFOLLOW` on POSIX,
`CREATE_NEW` on Windows), written, fsynced, then `rename` or
`MoveFileExW(MOVEFILE_REPLACE_EXISTING)` with a POSIX parent-directory
fsync; the temporary is removed on every failure and there is no retry
(the 2A.1 retry convention was test-harness only). The lock mkdirs parents,
creates `lodge.lock` with `O_CREAT|O_EXCL`/`CREATE_NEW` and 0600, refuses
with the exact reference message, writes `{"pid": .., "host": ..,
"created_at": ..}` in default-separator form, fsyncs, and unlinks only a
lock it created; a content failure unlinks and rethrows. The transaction
copies the retained manifest, edits, revalidates, compares sorted dumps,
reads current bytes through the reviewed safe reader into `lodge.json.bak`
and then writes `display` bytes plus LF, keeping the reference order so a
nonfinite encoding failure leaves the backup exactly as Python does. Blob
names pass the reference absolute/`..`/backslash/colon checks through the
PurePath normalizer; POSIX fsyncs the reference's directory set deepest
first. A narrow test-only hook names each durability phase with the acted-on
path; production passes none.

Deviations and ambiguities recorded for review: OS failures are
`filesystem_error`; the `WriterLock` destructor cannot report an unlink
failure during unwinding (Python's `finally` would replace the original
exception), while `release()` on the success path reports it; `new_id` and
temporary names draw from the OS CSPRNG rather than Python's seeded
`random.Random`; `now()` truncates to microseconds where Python rounds. The
reference joins a rooted drive-less NT blob name (`/x`) below the drive
root because `PureWindowsPath('/x').is_absolute()` is false; the native
port reproduces that join rather than adding a check, and the harness never
uses such a name. Capture cannot produce it.

Initial head `13b70f6071adfdab3a8441e41041a312ee298af4` passed locally
(Debug 27/27, focused ASan/UBSan 7/7) but **failed actual CI on both
platforms** (PR #27 merge-ref run 35635139112: Linux 24/27, Windows 42/44;
all new write tests failed). The local pass depended on the compiler
eliding a copy; see the correction below. Actual Windows CI, independent
review and merge remain gates; this checkpoint approves nothing.

#### Slice 3A correction after CI failure

R1 (production, data safety): both OS `Handle` owners in `store_write.cpp`
closed a raw descriptor/HANDLE in their destructor yet were implicitly
copyable; `create_temporary` returned a loop-local handle, so without
named-return-value optimization the copy's destructor closed the value and
`atomic_write` then wrote and fsynced a closed, possibly reused descriptor
(CI: `cannot fsync: Bad file descriptor`, `cannot write: The handle is
invalid` on the `.pending-` temporary). The owners are now move-only with
noexcept, source-invalidating moves and static_asserts. Regression: the
driver is rebuilt from the production core sources with
`-fno-elide-constructors` (`/Zc:nrvo-` on MSVC) as
`frontend-store-write-no-elision` and runs the full default gate; the
pre-fix source fails that gate with the CI symptom, the fixed source
passes. Unsupported compilers register a named explicit skip 77.

R4 (safety beyond parity, reviewed decision): `write_blobs` fails closed.
Every member is validated lexically (rooted, drive-qualified or UNC, `..`,
backslash, colon, NUL, empty or dot names) and its destination must be
strictly beneath the target directory, all before any mkdir or write; the
reference's rooted drive-less NT join below the drive root is no longer
reproduced. A Python-side fix is deferred; no Python changed. Native-only
sandboxed lexical cases assert the exact message and that nothing was
created inside or outside the sandbox; capture-produced names retain exact
parity including the round trip.

R2: `isoformat_utc` no longer uses the C library `gmtime` (MSVC range is
narrower); it computes the proleptic Gregorian civil date directly and
defines the supported range as datetime's years 1-9999, rejecting outside
it with `StoreError`. Timestamp/identity checks moved to a separate
`frontend-store-write-utilities` gate whose oracle uses timedelta
arithmetic (the previous `datetime.fromtimestamp(253402300799)` raised
OSError on Windows and aborted the suite). R3: the driver's stderr is
binary on Windows and the Python lock holder writes explicit LF bytes, so
exact LF assertions hold; every owned child is reaped on failure.
Persisted bytes remain compared exactly.

Verification of the corrected source used the same pinned oracle and
external Debug/ASan trees, each session awaited: full Debug 29/29 in
127.84 seconds (unchanged Python suite within `frontend-backend`, 17.75
seconds; store-write 144 comparisons, no-elision 144, utilities 25,
file-link 10, posix-durability 11); focused ASan/UBSan
(`ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=halt_on_error=1`) 9/9 in
53.27 seconds with no sanitizer report. Logs:
`/home/willvdb/code/games/c2-build/r-final-debug-full.log`,
`r-final-asan-focused.log`, `r1-proof-old-code.log`. Windows is proven
only by actual CI.

Round 2 (harness only, after the independent review of `a09315b` judged
R1-R4 correct and Windows run 35648432320 failed 43/46): the containment
case label `nul` had been used as a directory component (a DOS device on
Windows, failing `safe_path` before member validation and aborting the
default and no-elision gates), and the snapshot helper compared raw
`os.readlink` targets, which Windows reports with the `\\?\` prefix, so
`file-link` failed at its first case. Containment directories are now
index based, extended-prefix targets are normalized before the relative
comparison, and every tree comparison prints the differing keys with both
values so a remaining mismatch diagnoses itself. No production change;
Linux gates unchanged (144/144/10/11/25; Debug 29/29). Windows is proven
only by actual CI.

Round 3 (Windows run 35650728265, file-link at `dangling-lock`): on
Windows, `CreateFileW(CREATE_NEW)` and Python's `os.open(O_CREAT|O_EXCL)`
follow a symlink or junction at `lodge.lock`, so both writers created the
link target and wrote lock content through it (the trees differed only in
pid/created_at, but the target could lie outside the store); POSIX O_EXCL
refuses. Reviewed Windows safety correction beyond Python-on-Windows
parity: the lock create adds `FILE_FLAG_OPEN_REPARSE_POINT` (same
CREATE_NEW and share modes), so any existing entry yields the standard
refusal and is never followed, written through or removed; the
`.pending-` temporary create gains the same flag for POSIX-equivalent
semantics although its random name makes a planted link impractical. No
other create disposition exists in `store_write.cpp`. Python is unchanged;
its fix is deferred. The harness now checks link-at-lock cases natively on
every platform (exact refusal, link untouched, target absent) with parity
additionally on POSIX, compares lock content written by independent
processes structurally, and runs every independent case to completion,
reporting all failures at the end while still exiting nonzero. Linux:
default 144, no-elision 144, file-link 11, posix 11, utilities 25; Debug
29/29. Windows is proven only by actual CI.

Round 4 (Windows run 35653430598, file-link at `dangling-directory-lock`):
with `CREATE_NEW|FILE_FLAG_OPEN_REPARSE_POINT`, an existing directory,
directory link or junction at `lodge.lock` reports `ERROR_ACCESS_DENIED`
rather than `ERROR_FILE_EXISTS`, so native failed closed with the OS
error instead of the standard refusal. The lock create now confirms
existence without following (`GetFileAttributesW` reports the link
itself) and raises the standard refusal for any existing entry, while a
genuine error on an absent path still propagates; the `.pending-`
temporary applies the same rule (existing entry retries, genuine error
propagates). The harness asserts the exact refusal for a plain directory
on every platform and adds directory-link and Windows junction lock cases.
Windows is proven only by actual CI.


### Slice 2C.2 bounded planning completion

This continuation preserves all ten inherited sprint commits and adds runner
hardening, pin snapshots, current-generation refresh and the remaining library
wrappers. `ManifestAccess::snapshot` validates/copies the retained transaction
value; it neither rereads disk nor serializes/reparses unknown metadata.
Generation resolution uses the existing immutable manifest/capture component;
invalid current authority fails without fallback. Codec evidence is compared
before the resolved helper path is executed. No native bytes are rewritten.

The Windows runner now has one nonblocking named-pipe owner loop, a restricted
handle list and a kill-on-close job, with no pipe worker threads or joins.
Linux closes unrelated descriptors with `close_range`, moves all pipe fds above
stdio before `dup2`, and polls the exec handshake under the I/O deadline.
Cleanup has a bounded 250 ms reap grace then transfers an owned PID to a waiter
started before fork. Missing complete descriptor isolation refuses execution;
older Linux and other POSIX backends are an explicit compatibility gate.

Differential coverage uses unchanged Python functions and the existing
asset-free policy-double convention for successful observer/hunt wrapper
composition; production Genesis gates remain unchanged and separately tested.
The compiled helper exercises real Windows/POSIX pipe execution and pin-refusal
invocation markers. The catalog-projection flake reproduced: relative-path
immutability checks had scanned CTest's changing build directory. A separate
commit binds those checks to the fixture's actual working directory without
weakening assertions or serializing the suite.

Exact revisions, configuration results, preserved logs, CI links, exceptions
and the next bounded review task are in `CPP_PLANNING_HANDOFF.md`. This is
implementation evidence, not independent approval, runtime game certification
or a merge. Session preparation remains unimplemented.
