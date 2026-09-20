# Private manifest compatibility implementation

`manifest_schema.cpp` maps `store.validate`, `valid_id`, `validate_locator`,
`validate_engine_evidence`, and `managed_state.validate_history`,
`_validate_history`, `_members_valid`, `_execution_valid`. Historical execution
uses the unchanged `native_session.CAPABILITY`/`CONFIG` and
`genesis_hunt.SCORE_MODIFIERS` constants. It never queries executables or files.

`schema_compat.cpp` supplies iterative Python semantic equality, truthiness,
Unicode whitespace/case operations, and pure POSIX/NT path decomposition.
Integer/float comparison converts an integral binary64 exactly to decimal;
integers never round through double. Container equality follows Python's
identity shortcut for the JSON decoder's shared NaN constant, while scalar NaN
is unequal. Objects compare all keys independent of insertion order. Validators
use exact kinds separately from numeric equality.

Path tags are interpreted independently of the build host. Only state-member
`Path` checks follow the native host, as in the Python reference. Duplicate
installation locators and engine-evidence filenames use raw string equality;
managed containment uses normalized PurePath components and Windows Unicode
lowercase. State member ambiguity uses Unicode casefold. PurePath normalization
removes empty/dot components; it does not resolve parent traversal. This module
makes no filesystem safety assertion.

The Unicode table is generated from CPython 3.12.14 / Unicode 15.0.0, with the
full `str.lower`, `str.casefold`, and `str.isspace` behavior. Context-sensitive
Greek final sigma requires Cased and Case_Ignorable context, not scalar casing.
The generator recovers those predicates using Python final-sigma probes, and
records compact ranges. Unicode source provenance is the Unicode Character
Database 15.0.0, https://www.unicode.org/Public/15.0.0/ucd/; the generating
CPython implementation is https://github.com/python/cpython/tree/v3.12.14.
The Unicode License V3 text is retained in `UNICODE-LICENSE.txt` (retrieved
2026-09-20 from https://www.unicode.org/license.txt; SHA-256
`e7a93b009565cfce55919a381437ac4db883e9da2126fa28b91d12732bc53d96`).
`generate_schema_unicode.py --check` requires that pinned Unicode baseline;
it is an explicit regeneration check, not a build-time Python dependency.
A different Unicode database version is a compatibility gate before production
reads, not permission to silently substitute locale or ASCII casing.

`generate_schema_fixtures.py --check` invokes unchanged Python validators with
only their pure `Path` symbol parameterized as PurePosixPath/PureWindowsPath.
Both host outcomes are committed, so a Linux-generated fixture cannot silently
impose Linux rooted-path rules on Windows. Exact input bytes use documented
prefix/middle/suffix deltas against four authored base manifests. The test
reconstructs these bytes without normalization and checks retained exports.

The test-only driver accepts arbitrary byte stdin with `--manifest-stdin`:
valid + retained compact JSON (exit 0), invalid + diagnostic (exit 1), unexpected
diagnostic (exit 2), or explicit resource exhaustion (exit 3). `--batch` accepts
one JSON document per line and emits one status line, including compact JSON
for valid documents. stdin/stdout are binary on Windows. Additional private
modes test equality and Unicode casing. These are not production CLI commands.

The existing parser depth-above-1000 guard is retained. Python configurable
recursion/integer-resource differences remain pre-production-read gates;
unknown nonfinite metadata is permitted where validation permits it. Evidence
encoding restrictions do not tighten manifest validation. No read-time upgrade,
unknown-field filtering, snapshot inspection, trust check, or write is added.
