# Hunter and expedition state, schema 1

Status: backend prototype contract, 2026-09-18. The authoritative conclusions in
the task and the full user-supplied 20-edition static audit are the evidence base.
Repository evidence: `Shared/LegacyProfile.h`, `docs/PROFILE_SERIALIZATION.md`,
`Menu/Resources.cpp`, `Menu/Menu.cpp`, `Hunt/Game/CommandLine.cpp` at base
`ba556538ac97cfe8ec3035bed46740ce70d13737`. Static parsing is not certification.

## Identity and authority

* Hunter: random UUID, mutable display name, creation time and optional archive
  time. Select by UUID; duplicate names are allowed. Rename changes only frontend
  metadata, never native name bytes. Delete means archive, clear active selection,
  and retain associations/history. No native files are deleted.
* Expedition family/lineage: optional evidence-bearing label, distinct from an
  edition/release label. Neither is guessed from folder names, titles, AI numbers,
  or archive names. Unknown stays null. These labels are not installation keys.
* Installed instance: random UUID issued on explicit registration (or managed
  discovery). Native absolute location is a locator, not identity. Canonical path
  aliases cannot be registered twice in one registry. Independent installations
  with identical content get different UUIDs. No marker is written into arbitrary
  installations.
* Revision: versioned SHA-256 inventory of immutable content under HUNTDAT,
  including relative names, sizes and bytes; HUNTDAT configuration files are included. Saves, logs, screenshots and other
  recognized mutable files are excluded. Engine/launcher byte hashes are separate evidence;
  identical content does not prove identical engine behavior. Revisions are kept
  in history; a content change invalidates interpretations and launch assumptions.
* Engine/dialect: independent, evidence-bearing fields. A recognizable menu
  grammar does not prove an engine version. Classic Triassic is an Ice Age-derived
  family; older/newer MEE require separate semantic adapters. A user-supplied
  dialect hint is recorded as an assertion, not detection or runtime validation.
  Coherent content recognition does not require bundled engine/launcher files.
  Their absence remains an advisory diagnostic and a separate `none` capability;
  script/menu and non-trophy MAP/RSC requirements still reject partial overlays.
  Recognition is structural evidence, not a validated map decode or runtime claim.
* Native slot: installation-local `trophyNN` filename slot, never hunter identity.
  The current menu enumerates eight slots (0..7); discovery preserves outliers but
  cannot authorize using them. Embedded registration/name remain separately
  observable. A disagreement is a blocking diagnostic, never an implicit rename.
* State set: `.sav`, optional `.sab`, and later explicitly recognized companions.
  Missing `.sab` can be valid; orphan `.sab` is retained and reported. Unknown
  layouts remain opaque; other files sharing a profile basename are reported as
  unclassified companion candidates and block managed import until reviewed, never
  synthesized or discarded. Exact bytes
  and hashes are the source of truth. Manifest fields never replace progression.

## Ownership and defaults

| Entry path | Default | Initial implementation |
| --- | --- | --- |
| Installation under configured managed Expeditions directory | managed copy | immutable byte snapshot in frontend store; launch disabled |
| Arbitrary registered existing installation | referenced in place | read/associate without native writes |
| Explicit import of existing profile into hunter | managed copy | fork with new association UUID and source hashes; original retained |

Association always requires selecting a hunter, instance and filename slot plus
an explicit personal/bundled/unknown source declaration. Discovery cannot know
whether a packaged save is the user's. All first-seen saves are unclaimed;
bundled/example state can be copied only by explicit choice and remains so marked.
Associating does not initialize, normalize, rename or debit progression. One
live referenced association per canonical source slot in this registry; a second
hunter must deliberately fork a managed snapshot. Managed copies get independent
identities and are never synchronized back automatically. Cross-registry and
legacy-process ownership cannot be enforced yet: neither referenced nor managed
state is writable by the prototype. This restriction prevents claiming a global
writer lock that legacy executables do not honor.

## Transactions and recovery

Schema-1 UTF-8 JSON stores hunters, active hunter, instances, associations and
host settings. Strict validation rejects unsupported schema versions, duplicate
keys, invalid IDs/references and malformed shapes before mutation. Unknown fields
are preserved. Future migrations must make a byte backup and perform an explicit,
tested version-to-version transaction; no speculative version-0 migration.

A create-exclusive lock file serializes frontend writers. It records PID/host;
stale locks are never silently stolen (PID reuse and remote shares are unsafe).
An operator may remove a stale lock only after checking processes. Writes use a
same-directory temporary file, file flush/fsync, atomic replace, and directory
fsync where supported. The immediately previous manifest is retained as `.bak`.
Corrupt main JSON never automatically falls back to an older association state;
recovery is an explicit restore with the current damaged bytes retained first.

Managed state snapshots are staged in a unique directory, read twice and hash
compared as a whole set, then renamed before association is committed. A crash
before commit may leave an unreferenced snapshot, safe for manual inspection.
There is no destructive garbage collection. An unstable read is rejected; a
stable read cannot prove that an external writer did not pause between `.sav`
and `.sab` updates. Every snapshot reports this coherence limit. Users must close
legacy processes before import. Future writable launch needs exclusive process
ownership, whole-set preflight backup, a durable session journal, and explicit
reconciliation after partial writes. Never restore only one half automatically.

## Installation lifecycle and paths

Missing roots retain identities and associations. Discovery may suggest a move
when a missing instance has the same fingerprint; it never automatically decides
that a clone is the original. Explicit relocation requires the old path absent,
the new path unclaimed, and the same revision; conflicting/changed replacements
require a separate reviewed reconciliation. Native relative filenames remain
relative to the instance or managed snapshot. Native absolute paths carry OS
flavor and are not portable machine IDs. Cross-OS relocation is explicit.
Legacy references accept backslash/slash and case-insensitive components; case
collisions, traversal, absolute references and symlinks outside the root fail
closed. Managed discovery does not follow directory symlinks.

Managed instances persist `managed_root: {path, path_flavor}` separately from the
installation locator. It is the canonical absolute configured Expeditions
directory; installations must be strict descendants. Explicit relocation within
that root preserves `mode: managed`; outside it changes mode to `registered`.
The root locator is retained as historical context after ownership is relinquished.
Registered instances never become managed merely by moving into a directory.
State-association ownership and pinned revision provenance remain unchanged.

The recorded managed root must still exist on the current OS at its canonical
location, without symlink retargeting, and contain the previous installation
locator. Missing context, missing roots, foreign flavors or ambiguous paths fail
without changing metadata. The whole managed tree cannot implicitly move with an
instance. Older schema-1 records without `managed_root` still load, but managed
relocation requires a future explicit ownership reconciliation; no parent path is
guessed. Present root locators are shape-checked using their recorded POSIX or
Windows flavor (including drive/UNC boundaries), not the current OS parser.
Like installation locators, these are not machine/filesystem identities: a
same-path directory replacement cannot be distinguished without future evidence.

Uninstall is observed as missing, never cascades into deleting hunter state.
A reinstall at a new path gets a new ID unless explicitly reconciled with a
missing instance. Reusing a registered path retains its registry record and is
subject to revision/state-drift checks, not automatic adoption of replacement
profiles. An identical-byte reinstall at the same path is observationally
indistinguishable from the original without a future installation marker; this
cannot establish ownership continuity. An update appends a revision and exposes change diagnostics; it never migrates
saves or retargets trophy metadata. Save hash drift is separately reported.

Host settings own display/monitor/resolution/mode/refresh, audio and input.
They are frontend preferences pending a validated engine adapter; native gameplay
options stay in expedition saves/config. No runtime values are rewritten here.

## Future curated Hall of Fame

Native trophy rooms remain authoritative. A manually selected exhibit will carry
hunter, instance, revision, association, state-file hash, record index, raw record
bytes and optional metadata resolved against that exact revision. No inferred
map/history. An offline snapshot and a live asset-backed exhibit are distinct.
This schema reserves no final mod extension format and implements no trophy merge.
