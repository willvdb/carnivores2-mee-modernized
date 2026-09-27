# Tibbee v1.1.9.1 semantic integration

## Pinned inputs

- Fork remote main: `44bda6e6b71d8f2b13f47284336b75880e3baeb0`.
- Upstream main and dereferenced `v1.1.9.1-modernized`: `81fe90bccc5a9071e3925902bcf6e159c009b63d`.
- Merge base: `ca6aebfcdd7a100a990e7da3374e1efa8936b5b4`.
- Divergence: 344 fork-only commits, 118 upstream-only commits.
- Original local main was `a86be96faec2aacb4594d253bb9385091a0c2739` with untracked `carnivor.log` and `render.log`; left untouched.
- Branch: `integration/tibee-v1.1.9.1`, separate clean worktree based on remote main.
- Release: https://github.com/Tibbee/carnivores2-mee-modernized/releases/tag/v1.1.9.1-modernized

The fork's platform, explicit disk codecs, display configuration and managed session contract are the baseline. Upstream changes are examined as cumulative subsystem diffs at the pinned revision; intermediate strict-mod regressions are not integration targets. This document is an in-progress record until final validation is recorded.

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
