# Carnivores lodge backend prototype

An optional, independent frontend CLI for universal hunters and isolated
expedition associations. It does not change the engine or Win32 Menu, select a
GUI toolkit, write native saves, or execute hunts. The lodge and full-screen
Expedition Console are presentation layers to build on this backend later.
An opt-in synthetic session runner now exercises real child processes against
disposable state copies. A pinned Genesis observer policy remains blocked from
native engine execution pending an explicit engine session seam.

## Build and test

Requires Python 3.10+, CMake 3.20+, and a C++17 compiler. No Python packages or
network downloads are required. From the repository root:

```sh
cmake -S Frontend -B /tmp/carnivores-frontend-build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/carnivores-frontend-build
ctest --test-dir /tmp/carnivores-frontend-build --output-on-failure
python3 Frontend/frontend.py --help
```

`c2-profile-probe` reads bytes from stdin and emits JSON through the existing
`Shared/LegacyProfile.h` codec. It never opens a game file for writing. On Windows
use your Python command and the configuration-specific `.exe` probe path. Windows
execution of this new frontend has not yet been validated.

Pass `--probe /path/to/c2-profile-probe` before each command or set
`C2_PROFILE_PROBE`. Without it, profile inventory and raw copying still work, but
save inspection reports only size candidates and dry-run validation is blocked.
Tests needing the probe are skipped if run directly without that variable; the
CMake/CTest command above supplies it and runs the complete suite.

## Walk through the pipeline

Use a fresh frontend store outside all game folders for initial experimentation.
Commands emit JSON; take UUIDs from their output. Names are display labels, not IDs.
The examples below use symbolic `HUNTER_UUID`, `INSTANCE_UUID`, `ASSOCIATION_UUID`.

```sh
export C2_PROFILE_PROBE=/tmp/carnivores-frontend-build/c2-profile-probe
python3 Frontend/frontend.py --store /tmp/my-lodge hunter create 'My hunter'
python3 Frontend/frontend.py --store /tmp/my-lodge hunter list
python3 Frontend/frontend.py --store /tmp/my-lodge hunter select HUNTER_UUID
python3 Frontend/frontend.py --store /tmp/my-lodge hunter rename HUNTER_UUID 'New name'

# Read-only discovery; nested roots are searched and overlays reported as incomplete.
python3 Frontend/frontend.py --store /tmp/my-lodge expedition discover /path/to/Expeditions
# Optional explicit managed registration of coherent roots below that directory.
python3 Frontend/frontend.py --store /tmp/my-lodge expedition discover /path/to/Expeditions --register-managed
# Register an arbitrary existing installation without reorganizing its files.
python3 Frontend/frontend.py --store /tmp/my-lodge expedition register '/path/to/Game' --dialect mee-newer --family 'User-supplied family' --release 'User-supplied edition'
python3 Frontend/frontend.py --store /tmp/my-lodge expedition list
python3 Frontend/frontend.py --store /tmp/my-lodge profiles INSTANCE_UUID
python3 Frontend/frontend.py --store /tmp/my-lodge catalog --instance INSTANCE_UUID

# Declare origin explicitly; choose personal only when ownership is established.
python3 Frontend/frontend.py --store /tmp/my-lodge associate HUNTER_UUID INSTANCE_UUID trophy00 --origin personal --ownership referenced
# Alternatively import an independent, exact-byte snapshot (new association identity).
python3 Frontend/frontend.py --store /tmp/my-lodge associate HUNTER_UUID INSTANCE_UUID trophy00 --origin unknown --import-copy

python3 Frontend/frontend.py --store /tmp/my-lodge launch-dry-run ASSOCIATION_UUID --area areas:0 --license licenses:0 --weapon weapons:0
python3 Frontend/frontend.py --store /tmp/my-lodge launch-dry-run ASSOCIATION_UUID --area areas:0 --mode observer
python3 Frontend/frontend.py --store /tmp/my-lodge simulate-return ASSOCIATION_UUID
python3 Frontend/frontend.py --store /tmp/my-lodge refresh-state ASSOCIATION_UUID
python3 Frontend/frontend.py --store /tmp/my-lodge expedition refresh INSTANCE_UUID
python3 Frontend/frontend.py --store /tmp/my-lodge host-settings --json '{"display":{"mode":"borderless"},"audio":{},"input":{}}'
```

Dialect hints are user assertions, not certification. Use `iceage-triassic` for a
known classic Triassic instance; the C2 save codec is then explicitly disabled.
`mee-older` observations expose the audited engine dispatch gap. Unknown remains
unknown. Registration is idempotent by canonical path and does not overwrite an
existing instance's evidence with later command arguments.

Content recognition requires the existing coherent script/menu structure and a
non-trophy MAP/RSC pair. A bundled executable or renderer is independent evidence:
its absence is diagnosed but does not block registration or catalog projection.
Discovery and dry runs report `content_recognized` and `bundled_engine_evidence`
separately. Candidate binaries never certify modern-engine compatibility, and
engine-less content is not claimed to be runnable. Partial overlays remain rejected.

Every `launch-dry-run` result has `process_launch_allowed: false`. A structurally
valid selection may contain candidate argv, but there is no executable choice,
native rank/unlock certification, writable-state transaction, or actual process
launch. Equipment slots retain unresolved meanings; selecting them blocks argv.
Normal hunting requires a license and weapon; observer planning permits neither.
Price sums are displayed requirements, never purchases or score subtraction.
Host settings are stored preferences, not writes to native options or config.

No new native profile slot is allocated by creating a universal hunter. New-native
profile creation, slot reconciliation and writable launch are separate work.

## Controlled session lifecycle

See [SESSION_MODEL.md](docs/SESSION_MODEL.md) for journal/state/authority contracts,
[GENESIS_POLICY.md](docs/GENESIS_POLICY.md) for the exact observer assumptions, and
[ENGINE_SESSION_SEAM.md](docs/ENGINE_SESSION_SEAM.md) for the native blocker.

Only a readable, unchanged **managed personal** association qualifies. A managed
copy declared `unknown` or `bundled-example` remains ineligible; do not relabel
packaged profiles as personal progression. Tests create their own synthetic data.
The runner accepts a fixed, reviewed Python fixture and enumerated scenarios,
not arbitrary executables, shell commands or game binaries.

```sh
# Uses an already imported eligible managed association; take the session UUID
# from the returned JSON. Source snapshot and native files remain unchanged.
python3 Frontend/frontend.py --store /tmp/my-lodge session prepare-synthetic ASSOCIATION_UUID --area areas:0 --scenario sav
python3 Frontend/frontend.py --store /tmp/my-lodge session run SESSION_UUID
python3 Frontend/frontend.py --store /tmp/my-lodge session inspect SESSION_UUID
python3 Frontend/frontend.py --store /tmp/my-lodge session recover SESSION_UUID
# Resume inspection only after a durable child return:
python3 Frontend/frontend.py --store /tmp/my-lodge session reconcile SESSION_UUID

# Exact pinned Genesis content only. Hash-only --engine evidence is optional;
# this produces a blocked plan and never launches an engine.
python3 Frontend/frontend.py --store /tmp/my-lodge genesis-observer-plan ASSOCIATION_UUID --area areas:0
```

`session run` waits, captures bounded logs, inventories/inspects returned bytes,
and leaves either a clean `candidate` or `quarantined` review result. Spawn or
preflight failure is `failed`. Default timeout is five seconds (synthetic range
0.05..30); Ctrl-C cancels and reaps the owned child. Nonzero exit, timeout,
corruption, missing/extra members and registration mismatch never become clean
candidates. A process return code alone does not prove successful reconciliation.

Journals and evidence live under `sessions/SESSION_UUID/`. The original association
stays authoritative, schema 1 is unchanged, and there is **no promotion**. No
session directory is automatically deleted. An intermediate launching/running
journal becomes `interrupted` on recovery: no PID signal, relaunch or potentially
live state capture. A stale store lock requires manual owner/child verification,
as with existing manifest recovery. All real-engine capability flags remain
unvalidated, including `process_launch_allowed: false`; the distinct
`synthetic_process_launch_allowed` field authorizes only this fixture.

## State safety and lifecycle

* Native `.sav` and `.sab` bytes remain authoritative. Discovery/association never
  normalizes options, rank, room version, names or embedded registrations.
* All discovered saves are unclaimed. A root save may still be bundled author
  state. `MODDAT`/nested state is additionally marked as non-root state.
* Reference ownership defaults for registered installs; independent managed copies
  default for managed installs and explicit imports. Neither mode is writable by
  this prototype. No automatic synchronization between copies.
* A second reference to the same instance/slot is rejected. A deliberate managed
  fork gets a new association UUID and retained source hashes.
* Other files sharing a slot basename are reported as unclassified companion
  candidates; managed import blocks until their relationship is reviewed.
* Close legacy games and launchers before importing. Repeated identical reads
  detect drift, but cannot prove an external writer did not pause between writing
  `.sav` and `.sab`. Pair coherence remains explicitly unverified.
* Missing roots retain their identities. After moving an installation, use
  `expedition relocate INSTANCE_UUID /new/path`. The old path must be absent and
  content fingerprints must match. Matching fingerprints alone never trigger
  an automatic move. Managed registration persists a separate `managed_root`
  locator (`path`, `path_flavor`). A rename/move within that existing root remains
  managed; moving outside relinquishes installation management. Already registered
  installs remain registered. Association ownership never changes on relocation.
  Missing root context (including older records), an unavailable/retargeted root,
  or foreign managed paths block relocation pending explicit ownership
  reconciliation; this prototype does not provide that reconciliation operation.
* Content changes append revision history on refresh. Existing association
  provenance remains pinned; the prototype cannot approve a save migration.
  Engine/launcher hashes are tracked separately and changes require review.
* Explicit relocation still requires the exact HUNTDAT revision. Destination
  engine candidates are hashed and compared to the retained registration baseline.
  If they differ (including removal/addition), relocation records a persistent
  `engine_relocation_reviews` entry with `status: required`, both evidence sets,
  both locations and observation time. The original `engine_evidence` is never
  replaced. Inspection/refresh report `engine_review_required`; pending reviews
  block candidate argv even if the original binary bytes later return. Refresh
  and subsequent moves do not clear reviews. Approving a new engine baseline is
  deliberately deferred to a future reviewed reconciliation operation.
* `hunter archive HUNTER_UUID` is reversible archival metadata, not native save
  deletion. Restoring an archived hunter is not exposed in this first CLI.
* Store writes use `lodge.lock`, atomic replacement and `lodge.json.bak`. On a
  stale lock, verify that its PID/host no longer has a writer before manually
  removing it. Never steal it automatically. Prefer a local filesystem store;
  distributed locking and network-filesystem durability are not certified.
* `recover-backup` explicitly restores the validated previous manifest while
  retaining any damaged current file as `lodge.recovery-UUID.json`. Unknown schema
  versions fail closed; there is no automatic lossy migration.
* Incomplete `.pending-*` or unreferenced snapshot directories are retained after
  interrupted work; no automatic garbage collection deletes native evidence.

Fingerprints include relative spellings and bytes of HUNTDAT content. Recognized
save/log/temp/backup suffixes and saves/logs/screenshots/cache directories are
excluded; HUNTDAT `.cfg` files are included. Root `config.cfg`, native state and
logs are outside the content hash. They are not claims of unchanged gameplay.
Symlinked or case-colliding content requires future policy and is rejected for
registration/fingerprinting. Asset references diagnose missing, unsafe and
case-ambiguous paths rather than picking one.
Presentation assets such as `MENUM.TGA` also change the full HUNTDAT revision.
Distinguishing gameplay/semantic revisions from presentation-only revisions is a
future design issue; this pass retains the conservative fingerprint unchanged.

## Evidence, limits and development

[State model](docs/STATE_MODEL.md), [portable boundaries](docs/BOUNDARIES.md),
[audit decisions](docs/AUDIT_DECISIONS.md), and [handoff](docs/HANDOFF.md) describe
ownership, dialect limitations, validation and the next milestone.

Replay read-only observations against locally owned audit material:

```sh
python3 Frontend/tools/inspect_corpus.py /path/to/extracted/audit/material --probe "$C2_PROFILE_PROBE"
python3 Frontend/frontend.py catalog --path /path/to/partial/audit/game
```

Partial material can be projected without being registered as an installation.
Catalog entries retain raw script observations, file/line provenance and ambiguous
references. All creature rows are **licenses**, possibly covering multiple species.
AI numbers never identify entries. Blank labels stay blank. Extra maps stay in the
physical inventory. Accessories are native priced slots, not inferred grants of
modern equipment. Artwork is referenced, but semantic text embedded in images is
not recovered automatically. `.c2map` and explicit area declarations are recorded
without claiming a validated launch mapping.

There is no trophy collection merge. Raw records retain float bits/reserved words;
statistics are native observations without universal accuracy/unit assumptions.
Future manually curated Hall of Fame exhibits must retain instance, revision,
profile/file hash and record provenance, with offline snapshots distinguished from
live assets. No historical hunt map is fabricated.
