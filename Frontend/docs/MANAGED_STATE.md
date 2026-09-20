# Explicit managed-state history and acceptance

This contract extends [the original state model](STATE_MODEL.md). Manifest schema
**2** changes authority explicitly; schema 1 retains immutable-import semantics.
Old frontends reject schema 2 before launching instead of selecting the original
import. Reading/discovery never migrates. Native SAV/SAB formats are unchanged.

## Upgrade and provenance

`managed-state upgrade` holds the existing exclusive store writer lock, validates
schema 1 and the exact independent managed-import membership/hashes, writes
`lodge.schema-1.backup.json`, reads it back and verifies its bytes and schema, then
atomically replaces `lodge.json` with schema 2. Failed/previous differing upgrade
backups are retained and block retry pending review. Unknown metadata is preserved;
collisions with newly authoritative reserved fields block instead of interpreting
unknown data. Missing/corrupt/unsupported versions and unsafe snapshots fail closed.
No native files are converted, re-encoded, renamed or created by upgrade.

Every managed association gets a random generation-zero ID that references its
existing immutable `snapshots/ASSOCIATION` import. Its original UUID, hunter,
expedition instance, slot, revision, origin, imported member metadata and snapshot
remain. `authority` becomes `managed-state-history`; `writable: false` still means
native snapshots are immutable. Referenced associations remain referenced. Unknown
and bundled origins remain unknown/bundled and do not gain launch eligibility.
New explicit imports into an upgraded store receive the same G0 structure.

The durable `association.managed_state` object contains:

- `schema_version: 1`, `current_generation`, `generations`, and `receipts`.
- G0: ID, sequence 0, null predecessor/source session, import provenance, exact
  native members, immutable import snapshot location and creation metadata.
- Each accepted generation: new ID, sequence, predecessor ID, source session,
  exact native members, association/hunter/expedition/slot/content provenance,
  policy/build/contract/argv evidence, acceptance time and independent snapshot.
- Receipts keyed by source session, containing the generation/predecessor, member
  hashes, candidate journal digest, native policy/content/build evidence and
  `acceptance: explicit`. Head and receipt are in **the same authoritative file**.

Validation requires one complete chain, no missing/cyclic/unconnected records or
orphan receipts, and valid build/contract evidence. Metadata does not substitute
for snapshot bytes: resolving a generation freshly checks its complete membership
and hashes. A missing/corrupt active generation fails; there is no fallback to G0
or the previous head. `managed-state inspect ASSOCIATION` exposes current state,
full retained metadata history and original import provenance.

`recover-backup` is disabled once upgrade evidence or schema-2 authority exists:
restoring an old manifest would rewind history. General manual history rollback
is outside this milestone. Missing/corrupt main metadata never auto-falls back to
`.bak`; a missing upgraded main also cannot become an empty store. Evidence is
retained for a future deliberate recovery tool/operator review.

## Generation-pinned sessions

New `native-hunt plan/prepare` operations use schema 4, kind
`managed-native-hunt-v1`, when the store is schema 2. The pin contains the exact
current generation ID and immutable record, source root/member hashes and native
observations. Preparation independently copies that source to baseline/work/state;
engine `--session-source` names the same generation snapshot. Preflight reconstructs
all current pins, so an accepted intervening generation makes a prepared launch
fail even if hashes are equal. No substitution or automatic rebase occurs.

Return reconciliation resolves the session's **historical** generation, preserving
its real baseline and observations after the head advances. A clean but stale
result remains inspectable; the acceptance preview reports it ineligible. No
staleness diagnostic is cleared and no source reference is switched to recover
eligibility. A source/baseline/config/output failure still quarantines while
independently safe returned-state observation proceeds under the established
unknown/missing/corrupt distinctions.

Schema 1 synthetic, schema 2 observer and schema 3 original-import hunt journals
retain their original meanings. Terminal inspection never rewrites them. Their
prepared sessions cannot launch from an upgraded store, and new observer/synthetic
preparation in that store is deliberately blocked: only schema-4 normal hunts
currently resolve managed generations. These older kinds cannot be accepted.
A schema-1 store still offers A's candidate-only schema-3 hunt path.

## Preview and explicit acceptance

`managed-state preview SESSION --expected-generation GENERATION` is read-only.
It returns `allowed`, status/diagnostics, identity, policy/content/build evidence,
baseline/returned hashes and native before/after observations. The returned
`candidate_sha256` identifies the specific complete journal reviewed by the caller.

`managed-state accept SESSION --expected-generation GENERATION --candidate-sha256 DIGEST`
repeats validation under the writer lock. It requires all of:

- schema-4 normal-hunt kind and exact pinned policy; managed personal association;
- complete owned process lifecycle with clean exit, no cancellation/failure/review;
- complete recorded inventory/capture/codec stages and readable returned pair;
- fresh unchanged content, association, hunter, slot, generation, policy and codec;
- unchanged trusted executable hash and reconstructed contract/argv (no query);
- unchanged safe baseline; exact complete canonical native membership;
- independent safe regular files, no symlinks/reparse points/hardlink aliases;
- fresh returned/work hashes equal to the first durable capture and saved summary;
- real codec/registration results matching recorded before/after observations;
- valid fixed config/output and owned process log-capture evidence;
- expected generation equal to both the pinned predecessor and current head.

This remains trusted-code orchestration on a quiescent local filesystem, not
cryptographic protection against an adversary rewriting all evidence consistently.
A journal's word `candidate` alone is insufficient; no force/ignore-errors path
exists. No engine capability query, engine launch, stored-PID signal, native
re-encoding, score adjustment, rank normalization or installation sync occurs.
The explicit metadata action accepts native observations; it does not certify
world entry, gameplay success, save-pair atomicity or broad mod compatibility.

## Transaction and recovery

1. Under the store lock, independently revalidate the reviewed candidate and head.
2. Write exact captured native bytes into `generations/ASSOCIATION/.pending-ID`.
   Flush/fsync writes, verify membership and byte identity.
3. Rename to `generations/ASSOCIATION/ID`, verify again and sync its ancestors.
   This directory is durable but still **not authoritative**.
4. Save the prior manifest as `.bak`, then **atomically replace `lodge.json`** with
   the new generation, new current head and full authoritative receipt together.
   This replacement is the acceptance commit point.
5. Write a convenience copy to `sessions/SESSION/acceptance.json`. The candidate
   journal and returned evidence remain unchanged. This copy is not authority.

Before step 4, previous state remains current even when a complete orphan snapshot
exists. After step 4, new state remains current even if directory-sync reporting or
receipt-copy completion fails. Retrying the committed acceptance returns its
existing receipt, including after a later generation advanced. It cannot duplicate
history or rewind the head. Different expected predecessor/digest values reject.
Two candidates from G become competitors: accepting one makes the other stale.

`managed-state recover-acceptance SESSION` completes only a receipt already present
in the authoritative manifest, after validating the current and accepted snapshots.
It returns `not-committed` for an orphan/precommit attempt and never promotes it.
A divergent existing receipt copy blocks completion and is retained for review.
No directory is automatically cleaned. A crash may leave `lodge.lock`; existing
manual owner/child verification requirements remain, with no stale-lock stealing.

The durability contract uses the existing local-filesystem atomic replacement and
POSIX directory fsync discipline. Windows gets the existing Python file flush and
same-volume replacement behavior; power-loss durability and distributed/network
filesystem transactions are not certified. Injected failures cover both sides of
the exact commit point, not hardware crash guarantees.
