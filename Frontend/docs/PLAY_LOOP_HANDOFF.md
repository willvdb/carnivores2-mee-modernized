# Backend play loop → Expedition Console

The implemented loop is: select association → validate one area/license/weapon →
prepare isolated hunt → explicit trusted launch → inspect candidate → preview →
explicit acceptance → prepare again from the accepted generation. No GUI, server,
RPC layer, artwork or toolkit is introduced. Native gameplay acceptance is still
pending; asset-free fixture validation is described below.

## Operations the GUI can call

Python module functions raise `lodge.store.FrontendError` for blocked operations.
The CLI emits JSON; ordinary errors go to stderr as `{"error":"reason"}` with
exit 2. Acceptance preview returns exit 0 with `allowed: false` for reviewable
blocked states. Use that field, not CLI success or a zero engine exit, for eligibility.
Pass `--store TASK_STORE --probe PROBE` **before** each command.

| Intent | Existing module/CLI boundary | UI rule |
| --- | --- | --- |
| List/select hunter | `Store.read`, `hunter`; `hunter list`, `hunter select UUID` | UUID is identity; display name can repeat/change. |
| Expedition/association | `expedition list`, `status`; `associate` is an explicit import/reference operation | `status.associations` joins hunter and instance by UUID; origin and ownership stay visible. |
| Catalog | `catalog.project`; `catalog --instance UUID` | Render IDs/labels/diagnostics as supplied; grouped licenses are selections, not species. |
| Validate loadout | `native_hunt.plan_hunt(store, association, selection, probe)`; `native-hunt plan` | Fresh pinned revision and backend eligibility; no GUI rank/cost/mask rules. |
| Upgrade disposable metadata | `managed_state.upgrade_store`; `managed-state upgrade` | Explicit separate operation; reading never upgrades. Show backup and new G0 IDs. |
| Current state/history | `managed_state.inspect_history`; `managed-state inspect ASSOCIATION` | `current_generation` is the sole current state. Import fields remain provenance. |
| Prepare | `native_hunt.prepare_hunt`; `native-hunt prepare` | Pin returned session UUID/generation; do not edit argv or substitute source bytes. |
| Run | `native_hunt.run_hunt`; `native-hunt run` | Engine/hash/experimental gate required again. Synchronous wait; GUI may use its own worker and cancellation event. |
| Inspect/reconcile | `read_journal`, `reconcile_session`; `session inspect/reconcile` or `native-hunt inspect` | Module run returns `returned`; CLI native run also reconciles. Surface candidate/quarantine and observed-vs-unavailable state. |
| Review acceptance | `acceptance.preview_acceptance(store, session, expected_generation, probe)`; `managed-state preview` | Read-only and independently revalidated later. Keep returned candidate digest with this preview. |
| Accept | `acceptance.accept_candidate(store, session, expected_generation, candidate_sha256, probe)`; `managed-state accept` | User explicitly accepts this candidate/predecessor/digest. Never call just because process exited. |
| Recovery | `recover_session`, `recover_acceptance`; `session recover`, `managed-state recover-acceptance` | No query/launch/PID signaling; receipt recovery never promotes orphan state. |

Historical schema-1/2/3 journals remain inspectable. Only schema-4 generation-pinned
normal hunts are eligible for acceptance. New observer/synthetic preparation on
upgraded stores is currently blocked; schema-1 stores keep their original paths.
See [managed-state contract](MANAGED_STATE.md) and [hunt policy](GENESIS_HUNT.md).

## Request and result examples

Examples below show selected fields, with symbolic IDs/digests replacing actual
UUIDs and SHA-256 values. Do not send these placeholder identities as requests.

A `status` projection sufficient for selecting the association:

```json
{
  "schema_version": 2,
  "active_hunter": "HUNTER_UUID",
  "associations": {
    "ASSOCIATION_UUID": {
      "id": "ASSOCIATION_UUID", "hunter_id": "HUNTER_UUID",
      "instance_id": "EXPEDITION_UUID", "filename_slot": 0,
      "origin": "personal", "ownership": "managed",
      "authority": "managed-state-history",
      "managed_state": {"current_generation": "G0_UUID"}
    }
  }
}
```

Structured selection for `plan_hunt`/`prepare_hunt`:

```json
{"area":"areas:0","licenses":["licenses:0"],"weapons":["weapons:0"],
 "equipment":[],"mode":"hunt","time_of_day":1}
```

Exactly one ID per license/weapon list; empty equipment only. No arbitrary flags.
The plan returns `kind: genesis-hunt-plan-v1`, `process_launch_allowed: false`,
`result: validated-intent-only`, `pins` and `policy`, including masks and summed
`score_requirement` plus `score_mutation: none`. It is not an executable approval.

Prepared/returned session fields:

```json
{"schema_version":4,"id":"SESSION_UUID","state":"candidate",
 "pins":{"association_id":"ASSOCIATION_UUID","generation_id":"G0_UUID",
         "adapter":"genesis-current-mee-hunt-v1"},
 "capabilities":{"native_hunt_lifecycle":"completed","engine_process_executed":true,
                 "returned_native_state_readable":"yes","hunt_save_round_trip_validated":false},
 "reconciliation":{"status":"clean-candidate","authority":"managed-state-history",
   "promotion":"explicit-only","comparison_status":"complete",
   "changed_members":["trophy00.sav"],
   "observation":{"inventory":"complete","byte_capture":"complete",
                  "retained_capture":"verified","codec_inspection":"complete"}}}
```

`prepared`, `launching`, `running`, `returned`, `inspecting`, `candidate`,
`quarantined`, `failed`, `interrupted` are the backend state machine. Display
requested/applied facts remain engine-owned; the developer adapter keeps fixed
800x600 windowed config, not a new display selector. Native logs stay in the
workspace and are not part of the accepted profile pair.

Preview/acceptance fields:

```json
{"kind":"acceptance-preview-v1","session_id":"SESSION_UUID",
 "association_id":"ASSOCIATION_UUID","hunter_id":"HUNTER_UUID","instance_id":"EXPEDITION_UUID",
 "expected_generation":"G0_UUID","candidate_sha256":"PREVIEW_DIGEST",
 "allowed":true,"status":"eligible","diagnostics":[]}
```

The full preview additionally includes `revision`, `policy`, `execution`,
`baseline_members`, `returned_members`, `observed_changes` (native before/after and
changed member names), and session diagnostics. Display raw score/rank/stats and
raw trophy changes as observations; do not reinterpret them as a new economy.

```json
{"result":"accepted","current_generation":"G1_UUID",
 "receipt":{"schema_version":1,"association_id":"ASSOCIATION_UUID","session_id":"SESSION_UUID",
   "generation_id":"G1_UUID","predecessor":"G0_UUID","acceptance":"explicit",
   "candidate_sha256":"PREVIEW_DIGEST","policy":"genesis-current-mee-hunt-v1"}}
```

The full receipt also contains exact native member hashes, revision, execution
and acceptance time. Retrying after G2 yields `result: already-accepted`, the same
G1 receipt, and `current_generation: G2_UUID`. Receipt copies are not active state.
After success, request a fresh `native-hunt prepare`; its source/work/baseline are
the accepted bytes. Never reuse a previous prepared session or retarget its pins.

A stale result remains inspectable, with preview:

```json
{"kind":"acceptance-preview-v1","session_id":"STALE_SESSION_UUID",
 "expected_generation":"G0_UUID","allowed":false,"status":"blocked",
 "diagnostics":[{"code":"acceptance-blocked","message":"candidate predecessor is stale or mismatched"}]}
```

Other blockers include missing/corrupt current snapshots, unsupported policy/kind,
content/engine drift, mismatched identity/registration, incomplete observations,
unsafe paths, unclean return and workspace anomalies. There is no force-accept.
`null` returned members/changed list means unavailable observation; observed empty
lists are distinct from `[]` changed members on a safely inspected unchanged pair.
Absent historical fields mean no recorded observation, not implicit success.

## Reproducible asset-free loop

The mandatory CTest suite uses authored disposable native bytes, real C++ codecs,
production Session/Files child I/O and the actual CommandLine parser body. The
fixture changes score by 7 solely as labeled test data; it is **not a hunt**, trophy
or gameplay-success claim. The real Genesis gate rejects its real fixture content
hash. Only test code supplies an explicit edition-policy double.

```sh
cmake -S Frontend -B /tmp/c2-play-loop-b -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/c2-play-loop-b --config Debug --parallel 6
ctest --test-dir /tmp/c2-play-loop-b -C Debug --output-on-failure --no-tests=error
```

`test_acceptance_fixtures.AcceptanceFixtureTests.test_s1_accept_g1_s2_actual_input_accept_g2_and_late_retry`
checks G0 → S1 changed candidate → explicit G1 → S2 with **actual** G1 source path,
work bytes and baseline → changed candidate → explicit G2. It checks original
installation/import bytes at each stage, retains both sessions' baselines and G1,
and retries S1 after G2 without adding history or changing the head. Other tests
inject copy, rename, atomic manifest replacement, postcommit and receipt failures;
exercise stale/equal-byte competitors, metadata corruption, aliases and recovery.

## Human native acceptance checklist

No real Genesis normal-hunt/acceptance/continuation gameplay was performed during
this assignment. No suitable baseline was explicitly configured in a new task
store; no unrelated personal directories/default store were searched or modified.
Prior observer reports are not evidence for this new loop.

1. Build/review the engine and identify its trusted SHA-256 independently. Supply
   your already-owned exact pinned Genesis content and a complete, explicitly known
   personal native pair. Close legacy writers. Never pick a bundled executable or
   infer that packaged saves are personal.
2. Choose a **new empty disposable task store**, outside content and engine trees.
   Using that explicit `--store`, create a test hunter, register the content with
   `--dialect mee-newer`, inventory its saves, and explicitly import the known
   personal slot with `associate ... --origin personal --import-copy`. Alternatively
   independently copy an eligible managed baseline into a new task-only store while
   retaining its provenance. Never upgrade or accept into your real/default store
   as part of this validation.
3. Record original installation and imported native member hashes; run
   `managed-state upgrade` on that task store, then `managed-state inspect` to get G0.
4. Run `native-hunt plan/prepare` with one selected area/license/weapon/time. Review
   the complete plan. Prepare/run requires `--engine`, `--trusted-engine-sha256`,
   `--experimental-native-hunt`; use a 30..3600-second validation timeout.
5. Enter the world and exercise normal movement/weapon controls, then normal
   evacuation/return. Record exact code/build/revision, platform/backend/loadout,
   observed world entry, controls and native save changes. A zero exit alone is
   insufficient. Inspect the candidate/quarantine and compare protected hashes.
6. Preview with `--expected-generation G0`, inspect native changes, then explicitly
   accept that session with the preview's `--candidate-sha256`. Record G1 receipt.
7. Prepare S2, verify its source/baseline/work hashes equal G1, run/inspect another
   real session, and explicitly accept G2 only after reviewing that candidate.
   Confirm original/import/G1 and both session baselines remain unchanged.
8. Retain sanitized evidence and all disposable state. Do not commit assets, real
   profiles, private paths or unsanitized logs. Repeat on Windows and Linux before
   making a cross-platform gameplay acceptance claim.

## Next graphical milestone

Build a thin Expedition Console view over these operations: hunter/expedition and
current-generation selection; revision-bound single-loadout controls; explicit
experimental launch with running/cancel/status; returned native before/after review;
a separate acceptance confirmation showing predecessor and candidate digest; and
history/current-state refresh before the next hunt. Backend results determine
eligibility, masks, prices, stale states and adoption. Keep the GUI free of native
codec, economy, rank, filesystem-authority and recovery policy duplication.
