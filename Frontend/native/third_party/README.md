# Offline dependency provenance

## PicoSHA2

* Upstream: https://github.com/okdshin/PicoSHA2
* Exact commit: `161cb3fc4170fa7a3eca9e582cebd27cc4d1fe29`
* Files copied verbatim: `picosha2.h`, `LICENSE` (MIT, copyright okdshin).
* SHA-256 of header:
  `b13c180161ffac8d0adc81e033e493c409457c4d1258ab9781ac80579ba3bdd8`
* SHA-256 of license:
  `6c30eb1f37554ec4199cb82a8c86d9e7a852da78a757832bebb03ac9ddc44ff1`
* No local changes, configure-time fetch, system OpenSSL, subprocess hashing or
  engine dependency. Only the private `core.cpp` includes this implementation.

Integration review: the implementation masks its `unsigned long` arithmetic to
32 bits, explicitly assembles big-endian words and pads the 64-bit bit length.
Its incremental API converts each input chunk length to `word_t`, whose width
differs on LP64/LLP64. The wrapper feeds at most 64 KiB per call, avoiding that
narrowing and bounding the vendor's temporary vector. Hex encoding is produced
with a fixed alphabet rather than the vendor's locale-sensitive stream helper.
Review is source/integration review, not an independent cryptographic audit.

Native tests check empty, `abc`, the standard 56-byte two-block-padding vector,
one million `a` bytes (including wrapper chunk boundaries), binary signed-byte
input, and Python-oracle hashes. SHA implementation is not newly authored.

## JSON representation

There is no vendored JSON dependency. `src/json_compat.*` is a small original,
private compatibility implementation, explicitly subject to review. It uses
the platform C++17 `from_chars`/`to_chars` binary64 converters and its own Python
formatting policy. Decimal-string integers avoid bounded-library narrowing;
code-point strings preserve Python's escaped unpaired surrogates. Duplicate-key
detection, raw UTF-8 checking and JSON grammar are tested against the unchanged
Python oracle. This choice increases parser-review responsibility; passing a
finite corpus is not proof of equivalence. No production JSON read API is
exposed in this slice. MSVC's converters must pass the same corpus before merge.
