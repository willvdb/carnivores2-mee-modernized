# Pinned Genesis observer policy v1

Current follow-up: [NATIVE_OBSERVER.md](NATIVE_OBSERVER.md) adds an explicitly gated
native adapter with schema-2 journals against the implemented
[engine session v1 contract](../../docs/ENGINE_SESSION.md). Schema-1 synthetic
behavior and candidate-only authority remain. Historical missing-seam statements
below describe the prerequisite milestone, not current engine capability.

`genesis-current-mee-observer-v1` is a structural adapter for exactly:

* HUNTDAT SHA-256 `9c6fc5221744ad8e9a74689d308ba572b6aefe6cd6c317e030e5774757c2bf65`
* algorithm `huntdat-sha256-v1`, 344 files, 768,073,101 bytes
* observed and effective `mee-newer` grammar
* 8 advertised areas, 9 ordered license entries, 8 weapons, 4 priced accessories,
  9 physical maps including trophy material

The locally owned installation was rediscovered and fully fingerprinted during
this milestone. These facts match the prior audit; they do not certify the bundled
engine or the behavior of the modern engine. No proprietary files are included.
Tests fabricate policy observations and native byte fixtures independently.

## Exact scope

Only observer mode is accepted. No creature, weapon or accessory may be selected.
`din=0` and `wep=0` are explicit, so v1 does not depend on resolving grouped
licenses to individual species or ambiguous accessory descriptions. The legacy
Menu uses ordinal selection bits; the engine multiplies `din` by 1024 and consumes
`wep` directly. Nonzero masks are deliberately outside this adapter.

An area ID must resolve to exactly one catalog MAP/RSC pair. The relative project
stem is restricted to `area1`..`area8` or the evidenced `external` alias. No arbitrary
path, custom descriptor or trophy-room selection is accepted. Native slots are
0..7 with canonical root filenames and matching embedded registration. `dtm` maps
0 = dawn, 1 = day, 2 = night (`Menu/Hunt.h`). Observer selection requires sufficient
observed native score for the listed area price. The frontend neither debits score
nor changes rank/options. No general rank, unlock or progression equivalence is
claimed.

The candidate argument vector is:

```text
reg=<slot>
prj=huntdat/areas/<validated-stem>
din=0
wep=0
dtm=<0|1|2>
-observ
smod=0.85,0.70,0.80,1.0,1.25,1.0
```

The six `smod` values are camouflage, radar, scent, double ammunition,
tranquilizer, observer, in that order. Both pinned scripts have **no** `accessories`
override block. The values match explicit defaults in `Menu/Resources.cpp` and
`Hunt/Game/EngineInit.cpp` at reviewed main `7ab7d47`. The Menu always emits all six;
the engine parser accepts partial lists, but the adapter never emits a partial
list. `SubmitDinoScore` applies tranq/radar/scent/camo conditionally. Double and
observer multipliers are stored but no scoring use was found in this reviewed
engine. Emitting defaults preserves the evidenced Menu input without inventing
extra behavior or enabling equipment. Overrides require a new reviewed policy.

`-observ` is evidenced in Menu launch and engine parsing; engine weapon controls
guard observer mode. That does **not** establish a write-free observer session:
`SaveTrophy` can rewrite options/rank and saves SAV then SAB. No native observer
process was launched during this milestone.

## Preconditions and blockers

The CLI `genesis-observer-plan` applies session preconditions: managed personal
association, active hunter, exact unchanged source bytes and content revision,
readable state, no engine evidence review, and a pinned codec helper. A supplied
`--engine` is hashed for evidence only; it is never accepted as a certified build.
The result records provenance and the versioned policy, leaves executable/cwd
unset, and always sets `process_launch_allowed: false`.

Native execution is blocked by the missing [engine session seam](ENGINE_SESSION_SEAM.md)
and the absence of a certified engine build. Display/audio/host settings, arbitrary
extra arguments, equipment flags, multiplayer, survival, older MEE and other
editions are not passed through. A synthetic session uses a different adapter ID
and cannot advance Genesis runtime capabilities.

Read-only sources: `Menu/Menu.cpp` launch block and `CalculateDebit`,
`Menu/Resources.cpp` accessory defaults and overrides, `Menu/Hunt.h` time values,
`Hunt/Game/CommandLine.cpp`, `Hunt/Game/EngineInit.cpp`, `Hunt/Game/Trophy.cpp`.
