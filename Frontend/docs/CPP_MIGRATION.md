# Native frontend migration relay

## Baseline and scope

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
| 0A compatibility contract and golden corpus | Implemented, awaiting review |
| 0B core, private representation, encoders, SHA, CLI/tests | Implemented, awaiting review |
| 1A manifest read/validation | Pending |
| 1B history/current-generation + profiles | Pending |
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
