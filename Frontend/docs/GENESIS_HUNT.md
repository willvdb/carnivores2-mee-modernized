# Pinned Genesis normal hunt v1

`genesis-current-mee-hunt-v1` supports only the exact `huntdat-sha256-v1`
revision `9c6fc5221744ad8e9a74689d308ba572b6aefe6cd6c317e030e5774757c2bf65`
(344 files, 768073101 bytes), with observed/effective current-MEE grammar.
This is experimental support, not universal MEE compatibility or a gameplay
acceptance claim. [Observer policy](GENESIS_POLICY.md) remains distinct.

## Selection and progression evidence

References below describe the production sources at base `c4078d6`.

| Concern | Evidence and policy |
| --- | --- |
| License identity | `Menu/Menu.cpp::MenuEventStart` builds `g_DinoList` in definition order, filtering AI >=10; its rank test is commented out. Catalog `licenses:N` is that filtered ordinal. A grouped license remains one selection; duplicate AI values do not merge IDs. |
| Masks | `Menu/Menu.cpp::MenuEvent`, Hunt/Next block, emits `din |= 1 << i`, `wep |= 1 << i`. `Hunt/Game/CommandLine.cpp::ProcessCommandLine` multiplies **din only** by 1024; `Hunt/Loaders/ScriptParser.cpp::ReadCharacters`, `ReadWeapons` and table readers test `char0`..`char9` at bits 10..19. Never pre-shift in Python. |
| Bounds | Pinned catalog has 9 licenses (0..8) and 8 weapons (0..7). One bit each: din 1..256, wep 1..128, only powers of two. Engine weapon array/read limit is 10; v1 does not expose its extra capacity. Time is 0 dawn/1 day/2 night (`Menu/Hunt.h`); slots 0..7. |
| Areas | Eight catalog price slots, each with exactly one available MAP/RSC pair. `Menu/Resources.cpp::ReadPrices`/`MakeOldAreaInfo` and Menu launch resolve slot 6's `external` alias. The frontend refuses ambiguous pairs, arbitrary projects and `.c2map` selection. |
| Eligibility | `CalculateDebit`, `RestoreHuntSelections` and `MenuEvent` selection toggles use the sum of selected area/license/weapon prices against native score. The price>=1000/score<1000 condition chooses hidden artwork; it is not a separate rank gate. Integer nonnegative prices, total <= native int32 score. |
| Debit | `CalculateDebit` names/comments and drawn remaining credits suggest a fee. Actual Hunt/Next calls `TrophySave`, `SaveConfig`, `LaunchProcess`, then `TrophyLoad`, with **no subtraction**. Neither engine argument parser nor `LoadTrophy` subtracts listed costs. Frontend enforces selection eligibility, writes no debit. |
| Rank disagreement | `Menu/ProfileSerialization.h::UpdateRank` computes 0/1/2 at 100/300, then 1000 at score>=10000. `Hunt/Game/ProfileSerialization.h::UpdateRank` stops at 2. `Menu/Resources.cpp::TrophyLoad`/`TrophySave` recompute; engine `SaveTrophy` recomputes independently. Frontend does neither; stored rank is a native observation, not eligibility authority. This disagreement does not affect the supported selection because rank filtering is inactive. |
| Mode/equipment | Hunt omits `-observ` and all equipment/cheat/mode flags. Engine defaults disable equipment; `EngineProfile::ApplyOptions` deliberately does not restore equipment from SAV. Menu's false branches setting saved camo/radar/scent true do not supply engine flags; v1 does not reproduce that Menu save mutation. |
| Scoring | `Menu/Resources.cpp::LoadResources` defaults and `Hunt/Game/EngineInit.cpp::InitEngine` define camo=.85, radar=.70, scent=.80, double=1, tranq=1.25, observer=1. Pinned scripts have no accessory overrides. `ProcessCommandLine` receives all six; `SubmitDinoScore` applies active tranq/radar/scent/camo only, truncating the award to int. Double/observer have no scoring use there. No accessory active in v1. |
| Progression owner | Engine gameplay awards score in `SubmitDinoScore`, maintains hunt stats/trophies, and `SaveTrophy` serializes native options/rank plus `SaveTrophy2`'s room. Engine room load regenerates version; SAV body[0] is the room-present marker. These native effects are observations, not frontend transformations. |

The frontend does not implement the entire Menu or promise a universal economy.
No unresolved selection-affecting rank rule was found in this bounded path.
Multiple selections, accessories, cheats, survival, multiplayer, other revisions,
missing/ambiguous area pairs, blank labels and unknown prices remain blocked.
Generic catalog `declared_references` are advisory: the engine resolves character
files below HUNTDAT and weapon file/pic names below HUNTDAT/WEAPONS
(`CharacterLoader.cpp::LoadCharacters`). Those generic root-relative observations
are not an availability gate; the exact complete content fingerprint is.

## Callable operations

`lodge.genesis_hunt.hunt_policy` validates fresh revision/catalog plus structured
selection; `lodge.native_hunt.plan_hunt`, `prepare_hunt`, `run_hunt` implement the
backend boundary. Selection has exactly `area`, `licenses`, `weapons`, `equipment`,
`mode`, `time_of_day`; the first two lists contain exactly one ID, equipment `[]`,
mode `hunt`. There is no arbitrary argv or flag input.

```sh
python3 Frontend/frontend.py --store TASK_STORE native-hunt plan ASSOCIATION \
  --area areas:0 --license licenses:0 --weapon weapons:0 --time 1
python3 Frontend/frontend.py --store TASK_STORE native-hunt prepare ASSOCIATION \
  --area areas:0 --license licenses:0 --weapon weapons:0 --time 1 \
  --engine REVIEWED_ENGINE --trusted-engine-sha256 REVIEWED_SHA256 \
  --experimental-native-hunt --timeout 900
python3 Frontend/frontend.py --store TASK_STORE native-hunt run SESSION \
  --engine REVIEWED_ENGINE --trusted-engine-sha256 REVIEWED_SHA256 \
  --experimental-native-hunt
python3 Frontend/frontend.py --store TASK_STORE native-hunt inspect SESSION
```

Pass `--probe` before the command or configure `C2_PROFILE_PROBE` as before.
The engine/hash/gate is mandatory at prepare and run. It authorizes only that
reviewed binary and capability query. Never automatically choose a bundled engine.
Timeout is configurable 30..3600 seconds; default 900. Cancellation reaps the owned
child. Plan/inspect never launch. Common `session reconcile`/`recover` retain their
no-relaunch/no-PID-signaling rules. Prepared pins are revalidated before launch.

A's journal schema **3**, kind `experimental-native-hunt-v1`, pins the original
managed import and uses the existing native workspace contract. Schema 1 synthetic
and schema 2 observer retain their meaning. A has no acceptance implementation.
Shared `native_session` owns trust/capability, immutable baseline and independent
work/state copies, fixed config, output allowlist, explicit argv/cwd and preflight.
Mode-specific policy stays in observer/hunt modules. Fixed developer display
config is unchanged (800x600 windowed); Phase 4 requested/applied display behavior
and native profile/key bytes are not reimplemented. Host overrides remain deferred.

Only managed personal, readable, complete canonical SAV/SAB pairs qualify.
Zero exit, safe complete returned evidence and no diagnostics yield a candidate;
nonzero, interrupted/cancelled, missing/corrupt/aliased state quarantine or fail.
`hunt_save_round_trip_validated` remains false: even a readable return cannot prove
world entry, controls or a successful hunt. Original installation, imported state
and baseline remain unchanged; all evidence is retained.

## Validation boundaries

The standalone test suite compiles the actual `ProcessCommandLine` body for mask,
time, mode and score modifier characterization (state/platform services doubled).
The native fixture uses production Session/Files I/O and real C++ codecs, with
explicit authored test mutations. Its policy double is test-only; the unmodified
Genesis gate rejects the fixture content hash. Fixture execution is not gameplay.

For human native validation, use a **new disposable task store**, an explicitly
owned eligible baseline and exact content, never bundled/unknown saves relabeled
as personal. Record code/engine hash, revision, OS/backend, area/license/weapon/time,
world entry, movement/weapon controls, normal evacuation/return, returned hashes
and observed changes. Compare installation/import/baseline hashes afterward.
The previous milestone's owner-reported observer success does not validate hunts.
