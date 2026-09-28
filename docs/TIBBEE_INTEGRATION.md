# Tibbee v1.1.9.1 semantic integration

## Pinned inputs

- Fork remote main: `44bda6e6b71d8f2b13f47284336b75880e3baeb0`.
- Upstream main and dereferenced `v1.1.9.1-modernized`: `81fe90bccc5a9071e3925902bcf6e159c009b63d`.
- Merge base: `ca6aebfcdd7a100a990e7da3374e1efa8936b5b4`.
- Divergence: 344 fork-only commits, 118 upstream-only commits.
- Original local main was `a86be96faec2aacb4594d253bb9385091a0c2739` with untracked `carnivor.log` and `render.log`; left untouched.
- Branch: `integration/tibee-v1.1.9.1`, separate clean worktree based on remote main.
- Release: https://github.com/Tibbee/carnivores2-mee-modernized/releases/tag/v1.1.9.1-modernized

The fork's platform, explicit disk codecs, display configuration and managed session contract are the baseline. Upstream changes are examined as cumulative subsystem diffs at the pinned revision; intermediate strict-mod regressions are not integration targets. The implementation is ready for branch review subject to the runtime and sanitizer limits below. Final-head validation records are retained with the handoff; no merge to main is authorized.

## Upstream commit inventory

```
61b2112d2498243c8423fa69248314264661c71e ci(build): select the x86 MSVC toolchain
c2cbd212c428f72a5ffadabb021b0e4313bbdff7 docs(comments): drop dead section symbols
721a4ef8848f0e89a784e5955d48f475157c5e1a fix(renderer): pin the ScrPoint layout contract
a080bfc46d925e43a7fa4a1fdac683dcc807f1ee docs(comments): repoint stale file references
d30b93e161025322d52bf6cf492009ae5b3e6c3b chore(comments): label superseded blocks
edb6d932dabd5fc3db64b3fd0cb5c73149a18765 fix(ship): derive the WCircles guard from its array
778d73f03aa94443400f2f699ad81b0925ba735b fix(model): pin VLight as a raw pointer array
89f804c16e87a947143e9e7430b020138968d8b5 test(core): cover the config reader and water tint
5b408ace349238a3f0f4d884cb2fff745ed86c42 refactor(game): name the underwater-transition predicate
dcea07b29043994293519e468dcc5be0e6b0e40e refactor(game): share the smod= wire order with the menu
1763724e1035c298fea5541a2348aae136b94666 chore(tools): add comment accounting for bulk comment work
49c78ab44bd9dace7bd9be735476debc9f77d9e6 docs(comments): drop the stale generator claims
d46d9a4689df84c6dce55733040911aaec3c18f7 docs(comments): collect the mesh-upload note at its declaration
6023d47c00bc7a2c07a0a636d9a8a201b62a0438 docs(core): mark the reserved GameMode values
7e24381326dc889a2e24b1242091a0d6df217ee7 test(smoke): refuse to run over a running game instance
802d8dedd8bf3c54087a5b707accb7389d8b6cb6 docs(comments): use ASCII dashes in comments
9be5ed85c3b1ec8465f24541417fc9ab2f3b6d4d fix(ai): decouple hearing from render distance
24fb93bdf89339b1ed264dfacc98d06a1fd46620 fix(ai): make wounded predators retaliate
fe1f2d90dc5e5fb13fcbe2f6ef8dfe13283cc836 fix(ai): investigate last-known shot positions
1f6275558f3a8043bc518ab11ab5f67e7cedb45d fix(ai): track explicit hunter reactions
c86439d3fad900248a873eb07eef0fdb5d397b6b fix(loader): accept longer resource paths
3050130877d0450f31489f22e1fc6b2c023b15b9 test(renderer): lock software vertex layout
85aea66db19c3561b73d761d04361688958528d6 ci(test): gate deployed smoke tests
9a3f9fc64b72471bc80ef6ba96f8cf0b9e018a1f chore(docs): validate canonical doc paths
b84804f4f186eeaaa27d0888d998afec6d730a85 fix(menu): align scrolled hunt selections
619be2861b2794bb0becd07598c0105ad5bf0956 fix(ai): localize hunter-call reactions
6c5cadb3b2b3c136fda120dc15ab5450f6f34fdc fix(ai): limit pack awareness sharing
61300e598485675e3191c234515b66c5064ecef5 fix(menu): share safe resource text parsing
84320f3cfbd8ad428dc438361b0b45757c7fa395 docs(ai): record perception safeguards
f55527f3903495d45e0f3e6aa2913be15f85be8f fix(renderer): preserve edge-visible models
c0a4a6dd1895f810de9624dbabf4096d7868ee82 fix(hud): clear transient overlay pixels
a408664b4035c9c5557817195cfc352b3f44a118 fix(renderer): align sun with time of day
44bad444bd4515fa388ad785e50d4e91ce6dc531 fix(renderer): stabilize sky fog under optics
5bf73ccb7b8ffd10f0790ef76d01b34d4aaeec39 fix(game): preserve first trophy plaque
ef6ce86f0738bf0d3c563d171fd273fa3a1f6801 fix(renderer): close remaining edge gaps
1c905d1ba2c0df36ce63c7b3e20e781ac847eaf7 fix(game): protect static mount state
98df739f80c56ee1a91709e75e02caf87f7c19cd feat(renderer): add selectable sky mapping modes
da253023e77668a284dfae05590bc3990fe596c1 docs(changelog): record selectable sky mapping modes
3d8fc6ce184a77792ab0494f04dd78aa043f208f fix(renderer): preserve fog across sky modes
b697e33de4fce8db0c5129358fd21b5f83f29c75 fix(packaging): include default sky settings
39dc8cd8daaa4af0977f038b392cac8b1eb4b254 fix(safety): bound command-line values
1c6df9aa915384cd194cceb2e5942a40529d422d fix(safety): validate script values and weapon references
2f8867194e23498e8bdb71655e22a4eca8af05a0 fix(safety): preserve fog and bound loaders
4e16f5fdfb8a10b6338cb10ea586ce244445bfb1 test(smoke): add runtime contract fixture
b8dfb698fd099afc5b1c7534fecd15e771fd567d ci: enforce analysis and runtime contracts
5a4e8fdca5d34d275c310884f5f762c301fbcfce test(menu): cover launch argument contracts
b8b37e161452dd4f6c63a468ac06bb04ea894e59 test(game): cover menu overlay restoration
da003753f913b9d42ff909871cf6b0ee3e60a062 test(loader): exercise production WAV failures
3b2aebb71cb7b55a339ca3067b62ad57e833d711 ci: analyze complete push ranges
1a9f2ce77ea7649793bd56d225aaa206d1c1f157 test(loader): cover truncated WAV failures
e6627031e1bb3ee14a704d570508121eaa1eb169 ci(hardening): add C2 safety validation gates
8b291dcc8bff601736681b4281721bbefb6beb3c docs(legal): clarify modernization license scope
3b6943519a6039283a725268e908ed5cd01a2b33 fix(loader): harden parsing and spawn setup
4d03d1e09e46e70ba4fe2e2aaba45e2cfa9db94e fix(game): guard blank animation morphs
689be6b419d0fc6d597c730469913261b485a415 fix(menu): match legacy keys and bound prices
221bfb954acce70b91d425ab8573cbb57f3d8844 fix(loader): read text fields before numeric keys
19edb321547b7364de95e44f6605fa836cef198b test(runtime): cover menu and script reader contracts
9b746274843d1016f22a671dc993a4fee2c2b7ea docs(changelog): record post-v1.1.8 review fixes
d2c6dfbb73036598c3ce6749e4dcf9dbce8f6a23 fix(menu): build launch args in an empty stream
2ef51948d6e403969bab93300c4da11484452955 fix(map): disable map in trophy room
048bb53bef2ebc7c3686e2f1177a36d246fb1801 chore(release): prepare v1.1.9
8ff8b70d4640d70278741d9ed8d2f582c2c8f6f6 ci(analysis): resolve clang-tidy paths safely
622e50b71c7f09ebb41b0e89ba68f4259b1df55f fix(ai): respect aggression range for threat reactions
91421cbe773b9aa0d627e15ecd70daa778b4ca6d fix(spawn): allow zero-weight pack members
ea8ded5d110c6b2aa584aa70aaab285569a642df fix(compat): restore legacy threat and pack behavior
946b6b7a8a44f05eded9694d7111b5b9251addbd fix(ai): let T-Rex acquire hunter during reactions
5fb1e230aeebd76e99ad4b3ce09c53bfdde3b910 fix(ai): preserve pursuit across repeated T-Rex hits
ac1ad1e572b5dba2d82db45aa03aaaf985262ff8 fix(ai): avoid repeated alert resets for aware dinosaurs
30c01ad9980a608254d92673401375161972aeec fix(ai): charge immediately when the T-Rex is hit
91fe8616cfbbfacc20443d4a7e20754f5673572c fix(spawn): tolerate legacy zero and empty pack data
c0e7299f18599c9f625cfc719919cb5f3f3a5ba8 fix(ai): investigate heard shots and keep flee moving
de84813ee61ff983570037c9891c0bd5209492f1 fix(load): accept legacy numeric prefixes in _RES.TXT scalars
927aa03d125a2d204d87e43a8337b4a55eeb9c5a fix(ai): scale hunter-event reactions by authored aggression
9db51ec07e32320b85941d9a93ae4de383ee83bf fix(menu): number huntable assets by list position
079a6ccde17aa6ef09d44104e4377b17f79e1378 fix(ai): restore contact-range threat
bd0c61152d4b639c815782b40e936725f06ca68e refactor(ai): route hunter events through one resolver
b7f106d4b4c08e0dee856470ea0d4e32a8b83ba8 refactor(ai): own fixed-reaction navigation
b75e8161a8fd79eec1c9ead5a7c8493d9eb03cb2 refactor(ai): own authored flee/pursue decision
87ac37dfed9bed705737ba7695f437016e4847cc refactor(ai): own live hunter targets and geometry
49f8fbb2b619458586e2f874fd79b0e62142b47d feat(ai): unify the hunter kill gate
f2e0bfb9d0abb260a290f5756e4432ee169d9eaa feat(ai): own reaction timers and contact promotion
526bdeeaac91696048d9accd196dcd20aad37403 refactor(ai): remove the legacy awareHunter flag
33f79a4930d00c90e309f538bbc53c77319ae3a2 docs: record the awareness redesign in the changelog
348b341a5e995e49bf6e3456cc816e1bd8d8625f fix(ai): let a fleeing body crush at contact
769b0aa903189a88605cde711ad04f6d97df78b9 docs: record the flee-contact kill
9b84588819e441baa9be86cf11f936cb2630a84f feat(loaders): add lenient/strict load policy with recovery diagnostics
e97476852b5bedc98434111fd33b4dbe46e4282d chore(ai): add opt-in awareness trace
a87a2780171e60ea27e7ffaac670435fc41770bc fix(ai): keep a fixed flee running straight
127c5a2516aa7cfdbc4633b7e8c6f7f6bdefde51 docs: record the fixed-flee straight leg
d7b9f8bf75ff30be4e81cd8230b565a221d638b9 fix(ai): pick walkable flee destinations
927ad43c1e31b70f8b8132bf93582e407265e630 docs: record the walkable flee destinations
db58700ef26bc54589307b52c9248541e600d33c fix(renderer): fog weapon overlays once
a2f6f22c55b205ef88a674a0f9cc42081518154a fix(ai): aim fixed flee legs away from the hunter
71ab408822c17d0365ccc7ae5d918d22f9619012 docs: record the hunter-away flee legs
52bd6466bf999abeffd8c03855221273d8a2d332 feat(ai): add a permanent awareness trace
ec95d95e71530cc30f1cd80f30bb8307a85b88b5 docs: record the AI awareness trace switch
da589c94b68b664f7cf1330edfee7df68409bfaf fix(ai): alarm the pack during fixed reactions
57318cdcd6b0fc4f2ef03155f4eeeed1b0caa413 docs: record the fixed-reaction pack alarm
983f5c3eeafb6e4ad3c90cd6410743e28cdf0790 feat(ai): follow the packmate tracking the hunter
df3ce8d6f2c001d9e293d1f0ebe2bdc4fbea264b docs: record the pack hunt anchor
5e6cd521857a46d4ae6b82b759cdc92dbd2fda36 fix(ai): restore the generic fish wander radius
1bed5a842b6ba3bd7d1e3557983b78264608750a refactor(ai): centralize hunter-stimulus eligibility
2733c8849632795261f84bf82f63f4a9a76b4e16 feat(ai): allow species aggression overrides
cf9e5e6129e535947833153fbf288a72789ca544 docs: record the awareness follow-ups
20f8bbbbc245c7750518d91a8379f79beaa8b476 fix(game): keep trophy exhibits inert to bullets
ee773ee5e748af312d4526f7aa2486de7a62c4e8 fix(ai): close the hunter-awareness seams
979bb02077adb197f975b572569e2bcf78398cbc docs: record the awareness audit fixes
3a67fbfd77afcf2ea29ce2d5954a17cf5acbe1e6 fix(ai): re-aim flee legs that cannot be closed
bbff8910adc02be31fde65b092733ecb67521278 docs: record the flee-leg fix
b263c98e5bed89b82a67b22b0cead91934145eba feat(ai): trace every awareness acquisition
2dd10a2d95105a87f45d1c45856a386a1c10328f docs: record the trace coverage
d26fbf41bc5daa44b5c6eff106d9dba7c0fd4f0b fix(game): honor view distance in trophy room
6c41cecae0c3f31ba55fad61054f27a63b830f74 docs: record the trophy-room view distance fix
603db549b715fcc94b5d782fa9ae6ff90bd90e27 chore(release): prepare v1.1.9.1
9ad6c35be9c5a2f37ae0eb90a9070a278ddf046a fix(game): log clean session exits as exits
d6d61ce78e35900324163c650919d5e9569115be fix(loaders): name the entry behind a missing file
bba3c4394e908e625cb5fa70a620a502f05f021e fix(menu): refuse areas with incomplete data
81fe90bccc5a9071e3925902bcf6e159c009b63d chore(tools): add a HUNTDAT checker with tests
```


## Integration disposition and review order

The source tree after the final code checkpoint is `dab1479b62bcebc08049f4d3412c0a2966075983`.
History was consolidated without changing that tree: launch-probe/MSVC test fixes
belong to the launch commit, float-precision expectations to the renderer commit,
and flee retry timing to the awareness follow-up. No merge commit, original game
assets, or HUNTDAT files are included.

| Review order | Commit | Integrated behavior and architecture retained |
| --- | --- | --- |
| 1 | `8768204` | Exact/bounded portable arguments, numeric/range/finite checks, shared score-mod wire order, config parsing helpers and production command-line tests. `Platform::Arguments`, monitor/refresh settings, Linux networking refusal and managed argument filtering remain. |
| 2 | `a7c19a7` | Completed v1.1.9.1 lenient/strict recovery policy, numeric prefixes/C suffixes, safe clamps/truncation, exact assignment keys, source diagnostics, longer resource paths, missing-model context, array/index/weapon/spawn bounds, zero-weight packs, blank animation guards and animation bounds. All disk records still pass through the explicit codecs and portable file APIs. |
| 3 | `58aef65` | Coherent hunter awareness state/resolver, shot/hit/call/contact memory, finite exact tracking, species aggression overrides, predator/aquatic/T-Rex behavior, pack alarm and hunter-finding packmate anchors, obstacle-aware fleeing, fish wander, opt-in AI trace, trophy protection/plaque links/map prohibition, overlay clearing and clean session exits. |
| 4 | `058253d` | Extent-aware scenery/creature culling, animated bounds, terrain edges, sun/moon/shadows, authored night fog colors, optic-invariant sky fog, configurable sky mapping and matching shaders, single fog application to weapon overlays, trophy view distance. SDL display/context architecture remains unchanged. |
| 5 | `baa16d9` | Legacy menu argument preservation, bounded launch masks, scroll indices, safe resource field dispatch/prices, incomplete MAP/RSC refusal, roster-position thumbnails/descriptions and explicit pic override. Profile/media codecs preserved. HUNTDAT checker and Windows production menu-reader tests included. |
| 6 | `ceec171` | Additive native frontend resolved presentation API and narrowly scoped engine-compatible integer interpretation, with frozen raw projection and plan schemas unchanged. |
| 7 | `43b71a4` | MSVC release `/GS`, project-scoped `/W4` and optional `/WX`, deployed smoke opt-in, controlled smoke fixture CI, architecture-aware optional clang-tidy tooling. Full Linux/Windows x86/x64/SDL/WGL/SOFT/menu matrix retained. |
| 8 | `77f7452` | Review corrections: one reaction timer owner, no expired kill/anchor publication, accumulating flee progress with one re-aim per retry interval, pre-conversion travel-time cap, wrap-safe pack-anchor age. Production resolver tests supplement upstream math tests. |

### Frame-loop timer and destination ownership correction

A follow-up to `cf2efe2` traced the actual `AnimateCharacters()` order: target
aging and generic 30-second (plus Icth 50-second) timeout handling precede
contact promotion, `TickHunterAwareness()`, `UpdateHunterNavigation()`, and
species animation. The outer age increment doubled fixed-flee progress and
could consume a retry boundary before the navigator saw it. Generic wandering
could also replace a remembered shot/hit target while its reaction stayed active.

`AfraidTime` remains owned by `TickHunterAwareness()`. For ordinary wandering
and fixed pursuit/investigation, the dispatcher advances `tgtime` as target age;
target selection resets it. Both generic timeout branches are disabled during
fixed pursuit or fixed flee. Only awareness may replace those destinations via
arrival search, flee extension/retry, contact promotion, or reaction release.
Live tracking continues to refresh its target and reset target age as before.

For fixed fleeing, only `UpdateHunterNavigation()` advances `tgtime`, together
with its four-second retry-boundary check. Arrival starts a new leg at zero;
a stuck retry preserves elapsed progress so rotation attempts accumulate and
re-aiming occurs once per interval. Fixed-reaction expiry clears the reaction,
destination and `tgtime`, allowing ordinary target selection to resume. Trophy
and inert-character bypasses and pack alarm/anchor policies are unchanged.

The awareness test executable now links the production frame dispatcher as well
as the resolver, stubbing species movement/animation and world effects. The six
existing resolver tests remain; eleven frame cases cover all three fixed-flee
states, missed retry boundaries, remembered shot/hit timeout protection,
arrival search, expiry and resumed wandering, contact/live tracking/pack anchors,
and trophy/inert bypasses. Against the original `cf2efe2` production code, ten
new cases fail; with the correction all 17 pass. These are frame-policy tests,
not asset-dependent species animation or visual/balancing validation.

Correction validation uses the existing `build/final-debug`, `final-release`
and `final-asan` configurations below. Debug/Release engine builds and all 27
CTest entries succeed (22 pass, the same five display-harness skips); the 17
awareness cases also pass under Clang ASan+UBSan with leak detection enabled.
The full sanitizer suite was not rerun for this correction; the documented SDL
limitation below remains. Frontend Debug and Release each pass all 45 tests,
and the native-only Release build succeeds. Before/after and suite logs are
retained at `~/.local/state/c2-awareness-correction-20260927/`.

### Directly reused and adapted work

Pure parsing, score-order, spawn/awareness math, menu launch/list, sky projection,
and validation helpers/tests are carried from the pinned cumulative upstream
state, with portable strings/formatting where required. AI animators, renderer
and shader behavior are integrated as coherent groups, including their final
v1.1.9.1 follow-ups rather than earlier strict-loader or overlapping awareness
implementations. The complete commit inventory above is the reviewed range.

Adaptation is concentrated at the architecture boundaries: command-line source,
managed config lookup and shutdown status, platform I/O/messages/keys, explicit
model/resource/profile codecs, x64 runtime sizes, test seams and build matrices.
`LoadLoadPolicy()` runs before `_RES.TXT` through the same session-aware
`GetConfigPath()` as the later full config read. A missing private config does
not trigger an installation-global fallback. `C2_STRICT_DATA=1/0` now implements
the documented numeric switch as well as named modes.

### Already subsumed or intentionally omitted

- Upstream's x86-only toolchain assertion and x86-only build selection are
  superseded by the fork's explicit x86/x64 compiler validation, software-only
  x86 restriction and broader platform matrix. They must not be reinstated.
- Raw model/media/resource/profile read paths are superseded by portable explicit
  codecs with truncated-input, layout, size, association and payload coverage.
  Upstream `test_sound_loader_entry.cpp` and `test_picture_loader_entry.cpp`
  overlap the fork's production `MediaLoader`/`ModelLoader` tests and were not
  copied as duplicate Win32-only seams. New blank-animation and bounds tests
  extend the existing production suites instead. No existing tests were removed.
- Upstream old-menu incomplete-area behavior is integrated for Windows; the
  native frontend already requires one unambiguous complete pair for launch.
  That existing portable rule remains the authority for native launches.
- The comment-accounting/hygiene tooling, repository-wide dash/comment rewrite,
  external CarnivoresDoc path conventions, deleted historical comments and
  changelog-only commits are not imported. They do not fix runtime behavior and
  would obscure upstream review of the functional integration.
- Upstream's mandatory x86 clang-tidy CI job is not copied. Its useful analyzer
  configuration/runner is adapted to the selected ABI and remains optional;
  broad warning cleanup is a separate task. The release stack-protection check
  and smoke-script contracts are active CI gates.
- Release labels/resources (`048bb53`, `603db54`), the menu's fourth version
  component, upstream release README/CHANGELOG and release packaging changes
  are deferred to a fork release decision. The fork is not relabeled as an
  official Tibbee release. Packaging assumes an x86 Windows distribution and
  sibling release-notes repository; a cross-platform fork package needs its own
  review. Runtime defaults for sky/load/AI settings are integrated in the engine.
- `8b291dc` moves the existing modernization-only license scope from LICENSE to
  NOTICE. The fork already retains that limitation; both existing files remain
  unchanged. Attribution and third-party obligations are not relaxed.
- Intermediate strict-mod behavior is intentionally superseded by the pinned
  final lenient-default policy. Strict mode remains opt-in.
- The pinned upstream intentionally aims **new fixed-flee legs** away from the
  current hunter (`a2f6f22`); this differs from its fixed *pursuit* event-memory
  rule. The stated upstream behavior is preserved, not silently redesigned.

## Conflict resolutions

There was no blind merge or `theirs` resolution. Cumulative semantic patches
were compared against the common base; these overlapping areas were reconciled:

| Files/area | Resolution |
| --- | --- |
| `CommandLine.cpp`, CMake and CI | Reimplemented upstream validation around platform/session arguments and retained all display/network/platform branches; manually composed test/build additions into the fork matrix. |
| `EngineAPI.h`, `GameTypes.h`, `GameState.h` | Combined awareness declarations/globals and longer runtime fields with fixed-width types, portable signatures and architecture-conditional assertions. Runtime size changes do not define serialized size. |
| `EngineInit.cpp` | Kept platform display initialization, session path resolver, explicit config write handling and NUL-tolerant reads; added early load policy, AI/sky settings and bounded default-template creation. |
| `CharacterLoader.cpp`, `ModelLoader.cpp` | Kept platform file handles, explicit decoding and exact-read guards; added resource identity diagnostics, blank-animation semantics and cached animation bounds. |
| `ScriptParser.cpp` | Preserved Platform arguments and portable strings/I/O; integrated exact text-before-numeric dispatch, final recovery policy, bounds and species fields. Upper/lowercase legacy booleans are both covered. |
| `CharacterSpawn.cpp` | Combined safe spawn/pack logic with portable keys/messages and runtime setup. |
| `AnimateFish.cpp`, `AnimateTitan.cpp`, `CharacterAI.cpp` | Integrated final awareness behavior while preserving standard C++ declaration lifetimes across legacy goto labels; removed duplicate local declarations introduced by overlapping changes. |
| `Hunt.cpp`, `Interface.cpp` | Kept portable keys, window/input lifecycle and session failure latch; adapted trophy map and normal-exit behavior. Overlay clear uses actual dimensions on every platform. |
| `Resources.cpp` | Preserved explicit codecs, portable open/close and log routing; integrated trophy view distance, authored night fog colors, incomplete-pair and AI diagnostics. The night-color test expectation changed deliberately with the behavior. |
| `Menu.cpp` | Retained the fork's legacy profile include and added upstream launch/list helpers; no portable frontend architecture replaced. |
| `test_load_validate.cpp` and production test extraction | Combined upstream boundary tests with existing checked-size tests; adapted source extraction so full portable consumers, including helper functions, remain compiled. |

Automatic merges were also reviewed. No conflict markers remain. The cumulative
conflict audit and per-checkpoint failures are retained with the validation logs.
The history consolidation itself applied without conflicts.

## Frontend semantics

See [CATALOG_PRESENTATION](../Frontend/docs/CATALOG_PRESENTATION.md).
`resolve_huntables(root, projection)` resolves the selected script's roster in
huntable order, including duplicate AI. Stock Iguanodon/Carnotaurus/T-Rex map to
slots 8/9/10 despite AI 17/17/18. Both default thumbnails and descriptions use
that position; nonempty explicit `pic` overrides only the thumbnail. The portable
reference resolver still rejects traversal and ambiguous case matches.

Regression coverage includes duplicate AI, all stock ordering positions,
descriptions, explicit/default pictures, menu-script precedence, case resolution,
unsafe overrides and legacy numeric suffixes. `legacy_integer(Attribute)` shares
the engine's representable prefix interpretation for explicitly requested integer
fields, without globally weakening raw scalar parsing or granting recovery values.

The immutable v1 catalog export and native planning schema remain frozen. The
existing launch planner still refuses non-integer raw prices/entries it cannot
resolve; admitting those plans requires an explicit schema/policy migration.
The new typed presentation view does not grant purchase or launch authority.

## Compatibility findings and validation boundaries

SAV (1660 bytes), SAB (7176 bytes), trophy/profile codecs and their byte contracts
are unchanged. Profile/model/resource/map/image/audio layout and production-loader
suites remain in the full engine tests, and native profile/oracle/session tests
remain in the frontend suite. Awareness state and longer asset paths are runtime
fields only. Existing Windows x86 and x64 coverage tests the same byte fixtures.

Managed sessions retain private state/config/output, exact capability query,
argument preservation, source/baseline independence, failure-latched exit status,
whole-pair inspection and explicit acceptance. Tests cover early private load
policy with a conflicting global config and no private-config fallback. Normal
DoQuit does not erase a latched I/O failure. Native staged/sandboxed workflows
exercise G0 -> G1 -> G2, stale-generation refusal and idempotent acceptance.

Licensed Genesis Redux assets were available locally. Tests use a disposable
copy; original content/profile hashes are checked before/after and no assets are
committed. The real managed hunt queried capability v1, loaded private SAV/SAB
and config, initialized SDL/OpenGL, rendered until an owned 30-second timeout and
exited 0. Reconciliation correctly quarantined the timed-out session and left
managed authority unchanged. Three direct managed trophy smokes cover sky modes
0/1/2 paired with dawn/day/night, clean shutdown and exact SAV/SAB byte lengths.
These are startup/render/shutdown probes, not interactive gameplay acceptance.

The first smoke used Genesis's older installation shaders. It is superseded by
a repeat with that directory moved aside **only in the disposable copy**, so the
engine loads the matching built shaders via module fallback. Deploy engine and
shaders together: existing installation shader overrides otherwise take priority.
No global shader-resolution policy was changed.

Remaining manual checks: visual edge-culling/fog/weapon-overlay quality, all stock
licensed assets (not locally present as a separate stock installation), interactive
calls/shots/hits/pack navigation, and an actual changed-save Genesis hunt followed
by explicit native acceptance. Automated production-awareness tests cover the
state transitions, expiry, kill gates and pack sharing without claiming visual or
balancing validation. Windows runtime GL with licensed assets is also manual;
Windows CI proves builds and asset-free behavior, not GPU visual correctness.

## Validation commands and evidence

Full logs, exact command/commit records and smoke harnesses are retained at
`~/.local/state/c2-tibbee-integration-20260927/`. The final handoff records the
final branch SHA and final-head CI run links. Earlier checkpoint results must not
be attributed to a later SHA. The final documentation commit is followed by
rebuilding/rerunning affected/full suites at that exact head.

Fresh configurations used:

```sh
cmake --preset linux-x64-sdl-gl-debug -B build/final-debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/final-debug --parallel 10
ctest --test-dir build/final-debug --output-on-failure
cmake --preset linux-x64-sdl-gl-release -B build/final-release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/final-release --parallel 10
ctest --test-dir build/final-release --output-on-failure
cmake -S Frontend -B build/final-frontend-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE=/tmp/c2-integration-python/cpython-3.12.14-linux-x86_64-gnu/bin/python3.12
cmake --build build/final-frontend-release --parallel 6
ctest --test-dir build/final-frontend-release --output-on-failure --parallel 4
```

Frontend Debug uses the same pinned Python 3.12 oracle interpreter in
`build/frontend` (45 tests). System Python 3.14 initially disagreed with the
frozen Unicode oracle; no parser or oracle was changed to mask that difference.
The frontend Release/native-only artifact is built with `BUILD_TESTING=OFF` and
`CMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE`, installed to `build/final-stage`, then
run through `test_native_workflow.py ... --sandbox` without a Python runtime.

Clang ASan+UBSan configurations apply
`-fsanitize=address,undefined -fno-omit-frame-pointer` to C and C++ (engine and
bundled SDL), with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1`. The engine integration-sensitive suites pass.
The full sanitizer CTest run reproduces the pre-existing SDL 3.2.28
`SDL_video.c:1341` null pointer passed to zero-length memcpy in its display
catalog test; it is not suppressed or claimed clean. The focused rerun excludes
only the exact `Carnivores2Tests.SDL` executable. The frontend focused sanitizer
run covers 17 catalog/presentation/probe/session/acceptance/workflow tests.

Ordinary engine CTest has 27 executables, with five display-server-dependent
executables skipped outside their dedicated harnesses. Local X11/Wayland harness
attempts cannot start Xorg because the dummy video module is absent. The existing
CI Linux jobs install it and run both dedicated harnesses in Debug and Release.
No host display or system configuration is changed for the smoke tests.

Windows CI (`.github/workflows/build.yml`) retains x86/x64 WGL and SDL Debug and
Release, x86 SOFT and menu, plus release stack protection, menu-reader/HUNTDAT
contracts and the controlled deployed-smoke fixture. Frontend CI independently
builds/stages native Release and runs all native tests on Linux and Windows.
Intermediate failures were resolved (probe helper extraction, MSVC macro parsing,
and sky coefficient float precision); final-head CI status is reported separately.
