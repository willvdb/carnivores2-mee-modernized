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

## Relay checklist

| Slice | Status |
| --- | --- |
| 0A compatibility contract and golden corpus | Reviewed and merged in PR #16 |
| 0B core, private representation, encoders, SHA, CLI/tests | Reviewed and merged in PR #16 |
| 1A.1 pure manifest/schema validation | Reviewed and merged in PR #17 |
| 1A.2 read-only filesystem/store/CLI integration | Reviewed and merged in PR #18; overall 1A complete |
| 1B.1 current-generation/capture read | In progress; independent review pending |
| 1B.2 profile inspection | Pending |
| 2A discovery/reference/fingerprint observation | Pending |
| 2B catalog | Pending |
| 2C Genesis planning | Pending |
| 3A safe paths/capture/atomic I/O | Pending |
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
