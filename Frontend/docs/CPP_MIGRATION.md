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
| 0B core, private representation, encoders, SHA, CLI/tests | Pending implementation |
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

0A generation and `--check` pass with the baseline Python sources unchanged.
0B build, native differential tests and complete Python suite are pending.
Implementation commit evidence will be recorded by the following commit;
review and merge evidence belongs to the coordinator, not a self-approval.
Windows MSVC CI is required before merge; local Linux validation cannot stand
in for it. No later slice should start until 0A/0B representation gates pass.
