# Session lifecycle, journal schema 1

Current play-loop extension: [MANAGED_STATE.md](MANAGED_STATE.md) explicitly versions
accepted authority as manifest schema 2 and generation-pinned normal hunts as
journal schema 4. [GENESIS_HUNT.md](GENESIS_HUNT.md) defines candidate-only schema 3.
The historical contracts below remain applicable to their original schemas;
[PLAY_LOOP_HANDOFF.md](PLAY_LOOP_HANDOFF.md) documents the current UI boundary.

Current follow-up: [NATIVE_OBSERVER.md](NATIVE_OBSERVER.md) adds an explicitly gated
native adapter with schema-2 journals against the implemented
[engine session v1 contract](../../docs/ENGINE_SESSION.md). Schema-1 synthetic
behavior and candidate-only authority remain. Historical missing-seam statements
below describe the prerequisite milestone, not current engine capability.

This bounded backend proves a controlled synthetic child lifecycle. The pinned
Genesis/current-MEE observer policy describes intent; it does not authorize an
engine binary. Native execution remains blocked pending the engine session seam.

## Identity and preconditions

A session has its own UUID, separate from hunter, installation, association,
native slot and PID. Preparation pins those identities, the full content revision,
installation locator/dialect/engine evidence and review status, association
provenance, selection, adapter version, source hashes, codec helper, and the exact
synthetic interpreter/fixture bytes. Launch re-reads the manifest and all pinned
evidence. Drift fails closed, without spawning. The synthetic policy is explicitly
named and cannot certify Genesis or accept an arbitrary executable.

Only an active hunter's `managed`, `personal`, unchanged, readable association
qualifies. The source is its independent snapshot, never its native installation.
`personal` is an explicit provenance declaration, not something discovery guesses.
Tests use authored synthetic state; bundled-example and unknown origins are
rejected even if copied. Both SAV and an optional SAB must decode exactly; slots
are 0..7 with canonical root filenames and matching embedded registration.

## Workspace and authority

Under the frontend store, `sessions/<UUID>/` contains:

* `journal.json`: atomically replaced, fsynced journal; never an execution script.
* `baseline/`: durable exact copies of the approved snapshot members.
* `work/state/`: disposable, independent copies of baseline members.
* `returned/`: captured returned bytes, including unexpected regular files.
* `logs/`: bounded stdout/stderr from the controlled child.

The child has explicit cwd `work/` and only operates on `state/`. Content stays
referenced as provenance, and is not exposed to this synthetic child as writable
data. No asset copy, symlink, junction or hardlink is used. Paths are derived from
validated UUIDs; journal paths do not authorize arbitrary execution or filesystem
access. Symlinks, special files, nested state, case collisions and linked regular
files are ineligible. The runner is for trusted fixed fixture code, **not an OS
sandbox for untrusted executables**. External concurrent writers must be closed.

Authority strategy **B**: even a clean return is a durable reconciliation candidate,
never a promoted state. The schema-1 association and its original provenance stay
unchanged, including `writable: false`. Previous good state is still the original
snapshot. No manifest migration, sync to native saves, or candidate reuse/promotion
is provided. Rolling back means continuing from the unchanged association.

## State machine and durable boundaries

`prepared -> launching -> running -> returned -> inspecting -> candidate | quarantined`

* `prepared`: baseline/work copies and a durable journal exist.
* `launching`: final preflight passed; exact executable, argv and cwd persisted
  **before** spawn. Spawn failure becomes `failed` with a distinct diagnostic.
* `running`: an owned child handle exists; PID and launch time are persisted.
* `returned`: child has been reaped; exit code, stop reason and times persisted.
* `inspecting`: inventory/capture and codec observation are underway.
* `candidate`: zero exit, complete unchanged membership, exact readable formats,
  matching registration, stable source/baseline and no failure diagnostics.
* `quarantined`: return evidence retained with explicit review reasons.
* `failed`: preflight/spawn failed; no successful process claim.
* `interrupted`: recovery found an ambiguous launching/running journal.

Terminal states cannot relaunch. Prepared sessions can explicitly launch once;
no recovery automatically spawns or signals a stored PID (PID reuse is unsafe).
Recovery of `returned`/`inspecting` resumes evidence capture idempotently. Recovery
of `launching`/`running` marks `interrupted`, retains the workspace, and does not
inspect/copy potentially live state. The operator must establish quiescence before
manual forensic access; this milestone has no “assume dead” switch. A process
timeout/cancellation terminates and reaps the owned child before inspection.

Writers use the existing store lock. A crash may leave that lock: verify its owner
and all children before manually removing it, as with existing manifest recovery.
An orphan directory without a valid journal is retained, never auto-discovered as
launchable. Atomic replacement preserves the previous journal if writing fails.
Disk/OS failures may leave the last durable intermediate state for recovery.
POSIX directory fsync is used; Windows replacement has the same existing Python
store durability limits and needs native validation.

## Return policy and independent capabilities

Inventory **every entry** in the state directory; missing/extra members, corrupt
lengths, registration mismatch, nonzero exit, timeout and interruption quarantine.
An optional SAB absent at baseline is valid; adding one later needs review. Never
repair one member from another, discard companions, or normalize decoded fields.
Preserve byte hashes and decoded before/after observations. SAV-only and SAV+SAB
changes can be clean candidates, but quiescence and readability do not prove the
legacy pair was written as a transaction or that progression is semantically valid.

Workspace validation, synthetic child completion, native format readability,
Genesis structural policy, real engine execution, observer launch and complete
hunt/save validation are independent statuses. Synthetic success leaves all real
engine/runtime statuses unvalidated. Logs are bounded with explicit truncation;
the subprocess uses an argv list, `shell=False`, explicit cwd and finite timeout.

## Retention

There is no automatic cleanup in this milestone. Successful, failed, partial and
quarantined sessions retain their journals, baseline, workspace, logs and captured
returns. Manual removal of an entire reviewed, quiescent session directory is the
only cleanup; the authoritative snapshot must remain. Future promotion, retention
and semantic-versus-presentation content revisions require separate designs.
