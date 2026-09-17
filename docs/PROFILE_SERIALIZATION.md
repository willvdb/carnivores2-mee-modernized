# Legacy C2 profile and trophy bytes

Source characterization at `d782687`, before replacing persistence (D07/D08).
All offsets are decimal bytes. Integers are signed 32-bit little endian; floats
are IEEE-754 binary32 bits. No pointers, host `long`, native padding or one-byte
booleans belong to these files. Fixtures are original synthetic data.

## Prefix and items

The `.sav` prefix is 1516 bytes: name[128] at 0 (including bytes after any NUL),
registration at 128, score at 132, rank at 136, last stats at 140, total stats at
156, and 24 items starting at 172. Each 16-byte stats record contains shots made,
successes, path float bits, time float bits at offsets 0, 4, 8, 12.

Each 56-byte item (both `.sav` and `.sab`) contains type, weapon, phase, height,
weight, score, date, time at offsets 0..28 in four-byte steps, scale and range
float bits at 32/36, and four reserved int32 values at 40/44/48/52. Date/time
integers and reserved values are opaque to the codec.

## Complete C2 `.sav`

The suffix is 144 bytes; complete files are exactly 1660 bytes.

| Offset | Field (engine / menu) |
| ---: | --- |
| 1516 | OptAgres / Aggression |
| 1520 | OptDens / Density |
| 1524 | OptSens / Sensitivity |
| 1528 | OptRes / Resolution |
| 1532 | FOGENABLE / Fog |
| 1536 | OptText / Textures |
| 1540 | OptViewR / ViewRange |
| 1544 | SHADOWS3D / Shadows |
| 1548 | OptMsSens / MouseSensitivity |
| 1552 | OptBrightness / Brightness |
| 1556 | 17 int32 keys, 68 bytes |
| 1624 | REVERSEMS / MouseInvert |
| 1628 | ScentMode |
| 1632 | CamoMode |
| 1636 | RadarMode |
| 1640 | Tranq / TranqMode |
| 1644 | OPT_ALPHA_COLORKEY / AlphaColorKey |
| 1648 | OptSys |
| 1652 | OptSound / SoundAPI |
| 1656 | OptRender / RenderAPI |

Keys are forward, backward, reload, resupply, hold breath, firing mode, fire,
show weapon, strafe left, strafe right, strafe, jump, run, crouch, call, secondary
call, binoculars. All options, including booleans, occupy four bytes.

`Hunt/Game/Trophy.cpp::LoadTrophy` ignores 1628..1643 entirely: command-line /
session equipment stays active. `SaveTrophy` writes current equipment, so those
bytes need not round-trip. The engine restores the requested registration index
regardless of the stored index, clamps view range, forces multiplayer density to
128 and normalizes audio after a complete key map. It retains key defaults when
fewer than 68 key bytes exist, applies preceding options, and returns before the
remaining suffix. Missing view range uses its default. Previously other partial
scalar/record reads could mutate a fraction of a runtime value; this is unsafe
malformed-input behavior, not a byte contract to preserve.

The engine uses item 0's type as the `.sab` presence marker: if nonzero it loads
the room, otherwise it sets the marker to 1. Engine saves recalculate rank at
scores 100/300. Menu load and save also assign rank 1000 at score >=10000.

`Menu/Resources.cpp::TrophyLoad` requires exactly 1660 bytes for C2, loads all
equipment values, converts nonzero booleans to true, clamps view range,
normalizes renderer/audio, and translates the old resolution index into the
current resolution list (fallback 800x600 or first entry). Its writer canonicalizes
booleans to 0/1 and writes the runtime resolution index unchanged. Extended
settings remain in config.cfg. These transformations prevent unconditional exact
round trips at the application layer; the codec itself preserves every bit.

`Menu/Menu.cpp::MenuEventStart(MENU_REGISTER)` additionally reads only the first
140 bytes for the profile list. This reader also belongs to this format family.
Profile creation/deletion and menu hunt-return paths call the above routines.
The `_iceage` 1664-byte menu variant is outside this C2 slice.

## `.sab`

7176 bytes: versionID at 0, survival high score at 4, and 128 items at 8.
Only the engine reads/writes this file, through `LoadTrophy2`/`SaveTrophy2`.
A successful load replaces the stored version with `MODDERS_EDITION_VERSION_ID`;
save writes that runtime version. Missing files leave the zero-initialized room.
No format version change or content conversion is introduced.

## Implementation and failure boundaries

`Shared/LegacyProfile.h` is a small C++17 codec with fixed byte-array sizes,
explicit little-endian word operations, fixed-width value objects, and float-bit
transfer through `memcpy`. Its value objects are never serialized by copying
their object representation. It has no OS or engine/menu header dependency.
`Hunt/Game/ProfileSerialization.h` maps engine fields; `Menu/ProfileSerialization.h`
maps menu fields. The engine and menu keep their separate runtime structures.

Migrated call sites are the engine's four `LoadTrophy`/`SaveTrophy` and
`LoadTrophy2`/`SaveTrophy2` functions, the menu's `TrophyLoad`/`TrophySave`, and the
registration list's 140-byte header reader. Win32 handles and stream opening
remain at the existing call sites. The registration reader uses binary mode and
a bounded name, so newline/control bytes do not alter its fixed byte reads.
Ice Age's separate raw persistence remains under `_iceage`; it is not C2 coverage.

Strict header, prefix, suffix, key, complete-save and room decoders reject short
buffers before modifying their destination. The engine deliberately has a
separate compatibility path after a complete prefix: complete option words are
applied, missing words retain their defaults, missing view range gets its default,
and incomplete keys retain the entire current mapping. Prefix/room short reads
now leave the initialized fallback instead of a partially overwritten object.
The menu retains its exact-length requirement. Writers check transfer/stream
failure. This is bounded input hardening, not a general I/O error-policy redesign
or a crash-safe/atomic-file replacement scheme.

Existing runtime layout assertions and original serialized-layout tests remain
as regression oracles. The new production codec does not reference runtime
`sizeof`, padding, pointer width, host `long`, or serialized C++ `bool`. Future
runtime layout changes can update those historical oracles independently of the
explicit byte contract and golden tests.

The existing engine/menu audio normalization functions differ for some legacy
values (for example, 4 becomes 0 in the engine and 1 in the menu); both are
unchanged. Resolution index remapping and the menu's additional rank-1000 rule
are also preserved, not reconciled in this serialization slice.

## Validation (2026-09-17)

The Microsoft compiler/SDK and Wine environment are the same as
[WINDOWS_BUILDS.md](WINDOWS_BUILDS.md): MSVC 19.44.35229, SDK 10.0.26100.0,
Wine 11.17. All six default builds passed; PE machine types match the targets.

| Configuration | Build | Individual tests |
| --- | --- | --- |
| Windows x86 GL Debug | Passed | 153/156 passed |
| Windows x86 GL Release | Passed | 153/156 passed |
| Windows x64 GL Debug | Passed | 156/159 passed |
| Windows x64 GL Release | Passed | 156/159 passed |
| Windows x86 SOFT Release | Passed | 153/156 passed |
| Windows x86 menu Release | Passed | 153/156 passed |

All serialized-layout, codec, adapter, memory and fog tests pass. The only
failures are the three documented Wine UI-font baseline tests. The preserved
`d62905a` baseline executable was rerun and reproduced the same measurements:
38 versus 38 for both font checks; 145 versus 144 and 290 versus 288 for height.
CTest still reports these failures (exit 8). No UI test or implementation changed.

Golden coverage uses `tests/legacy_profile_fixtures.h`, `test_legacy_profile.cpp`,
and expanded engine/menu serialized-layout tests. Every prefix/item field,
reserved value, all 17 keys, all option slots, first/last items, negative integers,
noncanonical true values, version/high score, and complete byte round trips are
covered. Separate adapter tests cover ignored equipment, registration restoration,
short-key defaults, rank thresholds, menu boolean canonicalization and regenerated
room versions. Float-bit tests include negative zero, subnormal, infinity and NaN
payloads. Truncation checks include 139, 1515, 1555, 1623, 1659 and 7175 bytes,
plus isolated 143-byte suffix and 67-byte key buffers; failures leave outputs
unchanged. All six standalone codec tests also pass GCC with AddressSanitizer and
UndefinedBehaviorSanitizer, without Win32 headers or a Linux engine target.

Reproduction uses the six preset/build commands documented in WINDOWS_BUILDS.md
and `ctest --test-dir build/cleanup-msvc-<preset> -R '^Carnivores2Tests'
--output-on-failure --timeout 60`. Local build/test, baseline comparison,
sanitizer and final audit evidence is under
`~/.cache/carnivores-profile-20260917/`.

### Controlled user-owned profile interoperability

Fresh copies of the locally owned Genesis Redux content and profiles live outside
the repository at `~/Games/carnivores-profile-validation/`. Original profile
hashes were recorded and verified unchanged. Local-only harnesses compile the
actual `Menu/Resources.cpp` load/save routines from stable `main` (`d782687`)
and this branch with the x86 Microsoft compiler. They use a controlled eight-entry
resolution list and an error-dialog stub, exercising real stream I/O and menu
normalizations without automating menu UI clicks.

The completed sequence was stable x86 menu load/save → new x86 Release engine
hunt/evacuation/save → new x64 Release engine hunt/evacuation/save → new and
stable x86 menu load/save. An additional stable-menu load accepted the new-menu
output. Both full engine runs used area1 in observer mode, matching OpenAL DLLs,
headless Gamescope and copied `.sav`/`.sab` files. They loaded assets, responded to
window probes, logged both `Trophy Saved.` and `TrophyB Saved.`, shut down audio
and exited with code 0. This exercised real save paths, not `WM_CLOSE` alone.
The evacuation exit does not emit the `WM_CLOSE`-specific normal-shutdown log.

| Stage | `.sav` SHA-256 |
| --- | --- |
| Before/after stable menu | `50ed85a22ac0c2036c3688355bee3ebeb7eff12e135636e50948fff4954e2a65` |
| After x86 engine | `d93b24daa2a4bd7eb7b6c2815173439c668e74658e5e9b5de7a9ad8fe7c10977` |
| After x64 engine, new menu and stable menu | `8b3f31e1eed38bdccf1b06135c65c49146aebe1d6fd5f54f8b4a14e5cb8d5a40` |

All `.sav` outputs were 1660 bytes. The x86 save changed only the words at 140
(last shots), 148 (last path), 152 (last time), 168 (total time), and 1628/1632/1636
(scent/camo/radar). No shots or movement were supplied, and the launch omitted
those equipment flags, so the old last-hunt stats reset and equipment became zero
as expected. The x64 save changed only last/total time (152/168). Both menu writers
produced identical bytes from the engine output and preserved it exactly.

Locally available `.sab` data was loaded and saved by both engines. Every stage
remained 7176 bytes with SHA-256
`5593028babae85add59208a10c520e611fd61f6395f368dcb2e4380b8166e91f`.
Its stored version already matched the engine. Synthetic tests separately cover
version regeneration and all 128 items with nonzero reserved fields.

The production new-menu I/O also rejected copied files of lengths 0, 139, 1515,
1516, 1555, 1623, 1659 and 1661 without writing them. Full hashes, changed-word
values, per-run logs and results are in the validation directory's
`evidence/interoperability.json` and adjacent directories. No profiles, HUNTDAT,
assets, harness binaries or local automation are committed.

An initial automation attempt lost its Wine/Gamescope window before save;
restarting the dedicated Wine server resolved it. That attempt is retained as
failed evidence and is not counted as save validation. Screenshot capture still
failed, so no visual comparison is claimed. Native Windows execution, full menu
UI launch/return automation, broad mod coverage and interactive playtesting are
not established by these bounded checks.

## Final scope audit and follow-up

The full diff against `main` changes only this format family, its tests and docs,
plus one test source registration in CMake. C2-preprocessed menu sources contain
no raw profile/options/key-map persistence, and engine trophy I/O contains no
`sizeof(runtime_record)` transfers. Remaining menu raw persistence is the
explicitly excluded `_iceage` path; runtime initialization `sizeof` is not disk
I/O. Existing 1516/1660/7176-byte contracts and original layout assertions remain.

`.CAR`/`.3DF`, `.RSC`, `.MAP`, animations, other resource records and networking
still need their own serialization-hardening slices. Audio-value reconciliation,
resolution-index semantics, filesystem/platform work, SDL/Linux and launcher/UI
changes remain separate work; no unrelated format or gameplay logic was changed.
