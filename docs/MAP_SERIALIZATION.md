# Legacy map serialization

Characterized from merged stable `main` (`e0771f8`), before production edits.
`LoadResources` in `Hunt/Loaders/Resources.cpp` opens the `.MAP` after closing its
matching `.RSC`. There is no header, magic, version, count, or alignment padding.
The dimensions are fixed, independently of runtime representation. Coordinates
are row-major `[y][x]`, x advancing first. Each full plane is 1024 x 1024;
fog and ambient planes are 512 x 512. Trailing bytes are ignored.

## Byte contract

All offsets and sizes below are decimal bytes. The complete consumed stream is
**14,155,776 bytes (13.5 MiB)**. Independent file inventories confirm that all
nine distinct available Genesis Redux maps have exactly this length. This is mod
content, not independently authenticated pristine stock Carnivores 2.

| Plane | Offset | Bytes | Persistent element |
| --- | ---: | ---: | --- |
| HMap | 0 | 1048576 | unsigned byte |
| TMap1 | 1048576 | 2097152 | little-endian uint16 |
| TMap2 | 3145728 | 2097152 | little-endian uint16 |
| OMap | 5242880 | 1048576 | unsigned byte |
| FMap | 6291456 | 2097152 | little-endian uint16 |
| Light 0 | 8388608 | 1048576 | unsigned byte |
| Light 1 | 9437184 | 1048576 | unsigned byte |
| Light 2 | 10485760 | 1048576 | unsigned byte |
| WMap | 11534336 | 1048576 | unsigned byte |
| HMapO | 12582912 | 1048576 | unsigned byte |
| FogsMap | 13631488 | 262144 | unsigned byte |
| AmbMap | 13893632 | 262144 | unsigned byte |
| End | 14155776 | 0 | |

`Core/GameState.h` declares unsigned-char byte planes and WORD texture/flag
planes; `Core/Constants.h` fixes ctMapSize=1024 and ctHScale=64. These declarations
agree with the historical loader's `sizeof` reads; they are not a license to
change file sizes if runtime storage changes.

## Texture indices and flags

**Neither TMap1 nor TMap2 packs flags into the texture word.** Each is an entire
unsigned 16-bit texture index. The only special value is 0xffff, accepted only
when there are at least two RSC textures; `CreateTMap` later replaces it with 1.
All other values must be below the loaded texture count (at most 1024).
Consequently 0x1234 and 0x9234 must be transported intact, then rejected as
texture references, not masked into valid indices.

GLTerrain.cpp `CollectTerrainTile` uses TMap1 directly as the texture layer for
both triangles. SOFT `ProcessMap` uses TMap1 for near tiles; `ProcessMap2` uses
TMap2 for the coarser two-cell terrain path. `GenerateMapImage` uses TMap1 on dry
terrain and WaterList[WMap].tindex on water. `ProcessWaterMap` in SoftWater.cpp
uses zero TMap1/TMap2 to gate its first/second glass triangle respectively (both
use Textures[0]); this does not make either plane a packed bitfield. There are
no discovered texture-word rotation, reverse, detail, or blend bits. Existing
mip selection, water alpha and distance blending are runtime renderer decisions.

These meanings instead belong to the **FMap** uint16 bitmask:

| Mask | Meaning / source consumers |
| --- | --- |
| 0x0003 | Terrain texture direction, `FMap & 3`, GLTerrain / SOFT ProcessMap |
| 0x000c | Object orientation, `(FMap >> 2) & 3`, GLModel, SoftTerrain, bound collision |
| 0x0010 | `fmReverse`, terrain triangle diagonal; terrain queries and rendering |
| 0x0020 | `fmNOWAY`, character placement/collision restriction |
| 0x0080 | `fmWater`, primary water region, terrain/character/effects/minimap consumers |
| 0x0300 | Secondary/coarse texture direction, `(FMap >> 8) & 3`, SOFT ProcessMap2 |
| 0x8000 | `fmWater2`, secondary/shore water; also added by CreateTMap |
| 0x8080 | `fmWaterA`, union of primary and secondary water masks |

`Math.cpp::TraceModel` also uses `(FMap >> 2) & 7` for an eight-way orientation;
that includes bit 4, overlapping fmReverse. Preserve this existing consumer
behavior rather than rationalizing it. Bits 6 and 10..14 have no active flag
consumer found in the map search. No bits, including unknown/high bits, are
normalized at decode. Postprocessing may legitimately change flags afterward.

## Intentional byte planes

These planes persist individual unsigned bytes. Bounded exact raw-byte reads
are deliberate, not residual native multibyte serialization:

- HMap: terrain height samples, scaled by ctHScale in TerrainQueries, Math,
  Effects and renderers; interpolation/triangle choice happens afterward.
- LMap: selected lighting samples, interpolated by GetLandLt and sampled by
  Effects and object renderers; RenderLightMap later modifies object shadows.
- OMap: object/model index; 255 is empty, 254 is a landing marker. Validation
  bounds other indices against mc and limits landing markers to 64. CreateTMap
  gathers markers and replaces them with 255; its existing default landing stays.
- WMap: WaterList index when FMap & fmWaterA. Validation retains the existing
  water-count check, including rejection of 255 on water unless 256 entries
  exist. Dry cells are not validated; CreateTMap later sets their WMap to 255.
- HMapO: object-height byte, scaled by ctHScale; CreateTMap zeroes empty cells
  and recomputes ground-placed objects. It is not a signed byte or word.
- FogsMap: per-region FogsList index, sampled using world coordinates >> 9
  (two terrain cells per region). Zero is the no-fog region. Controls, Hunt and
  GL fog sampling use byte values; underwater index 127 is a runtime override.
- AmbMap: per-region Ambient index, likewise coordinates >> 9; Controls selects
  ambient PCM, volume, environment and random effects from this byte index.

Existing map validation does not add RSC count checks to fog/ambient indices;
this slice retains that policy. The fixed runtime tables contain 256 entries.

## Light selection and postprocessing

The valid OptDayNight domain is 0=dawn, 1=day, 2=night. The menu constrains this
selection; CommandLine.cpp accepts `dtm=` via atoi without range validation.
Earlier RSC color/sky accesses already require the same domain. This slice does
not redesign command-line validation or claim to make invalid global options safe.

At offset 8388608, skip `1048576 * OptDayNight`, read one full byte plane into
LMap, then skip `1048576 * (2 - OptDayNight)`. Every valid choice arrives at
11534336 before WMap, and 14155776 after AmbMap. No permanent storage for the
other two light maps is needed. Win32 seeking remains at the engine boundary.

ValidateMapReferences runs after all planes and before fog mutation. Its texture,
object, landing and conditional water checks must receive identical values.
The existing fog pass fills zero cells in [0,509] x [0,509] with index 1 if
FogsList[1].YBegin > 1 and any sample in the corresponding 3 x 3 height patch is
below that threshold. The handle closes, then the existing `Prepearing maps...`
notification precedes CreateTMap, RenderLightMap, UI picture loading and
GenerateMapImage. Raw-file hashes must not be confused with these later runtime
mutations (water expansion, sentinel conversion, object placement, SOFT diagonal
recalculation, shadows and fog filling).

## Implementation and failure boundaries

`Shared/LegacyMap.h` defines fixed dimensions, byte sizes, offsets and a bounded
C++17 little-endian uint16 decoder. It has no OS headers, handles, runtime
structures, renderer state or dependency on LegacyResource. The decoder
transports every bit, checking size/capacity/null/overflow before any assignment.
Empty decodes succeed. Input/output buffers must not overlap.

`Hunt/Loaders/MapIO.h` owns Win32 exact reads and bounded light-map seeks.
`ReadWordPlane` iterates actual row arrays, using `ReadWords` for bounded byte
chunks, portable decoding and numeric assignment to WORD. This avoids both a
second map-sized allocation and pointer arithmetic across C++ subarray bounds.
`ReadBytePlane` asserts the characterized dimensions and eight-bit storage,
then performs an exact byte read. `ReadLightPlane` retains the two skips and
one selected read, rejecting an invalid day locally. `SkipLightBytes` uses
SetFilePointerEx/GetFileSizeEx because an ordinary successful seek can go past
EOF; truncated unselected light data now fails promptly instead of relying on
a later byte-plane read to detect it. Valid file positions are unchanged.

Only the MAP block in LoadResources and its formerly MAP-only ReadRscExact
wrapper are migrated. Errors still halt through DoHalt. Capacity/null/overflow
failures do not move the file or change output. A short byte read can leave
partial bytes, and a short streamed word plane can leave earlier complete
chunks; no incomplete word chunk is assigned. The load is not transactional,
but no failed load proceeds to reference validation or map postprocessing.

## Validation (2026-09-17)

Microsoft MSVC 19.44.35229 / Windows SDK 10.0.26100.0, executed under Wine 11.17
using [WINDOWS_BUILDS.md](WINDOWS_BUILDS.md). No Linux engine target was added.

| Configuration | Full default build | Individual asset-free tests |
| --- | --- | --- |
| Windows x86 GL Debug | Passed | 194/197 |
| Windows x86 GL Release | Passed | 194/197 |
| Windows x64 GL Debug | Passed | 197/200 |
| Windows x64 GL Release | Passed | 197/200 |
| Windows x86 SOFT Release | Passed | 194/197 |
| Windows x86 menu Release | Passed | 194/197 |

Only the three previously documented UIText tests fail. A fresh run of preserved
baseline `d62905a` reproduces the exact measurements in all six configurations:
38 versus 38 twice, 145 versus 144, and 290 versus 288. CTest remains exit 8,
7/8 executables passing; no test is hidden or suppressed. All map, resource,
model, profile, allocator and fog tests pass. After a test-only temporary-handle
cleanup, the affected production-loader target was rebuilt/retested in all six
configurations; engine code did not change.

Four portable map codec tests check literal offsets, single/adjacent words,
0/1/0x00ff/0x0100/0x1234/0x9234/0xffff, every individual flag bit, combinations,
chunk boundaries, every short fixture length, capacity/null and SIZE_MAX cases.
GCC C++17 ASan+UBSan runs these plus the unchanged resource/model/profile codec
suites: **22/22 pass**, no diagnostics. The initial sandbox run passed tests but
could not complete LeakSanitizer under tracing; the unrestricted rerun completed
successfully, including leak checking. Reproduction:

```sh
gtest=build/cleanup-msvc-windows-x86-gl-release/_deps/googletest-src/googletest
g++ -std=c++17 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$gtest/include" -I "$gtest" tests/test_legacy_map.cpp \
  tests/test_legacy_resource.cpp tests/test_legacy_model.cpp \
  tests/test_legacy_profile.cpp "$gtest/src/gtest-all.cc" \
  "$gtest/src/gtest_main.cc" -pthread -o /tmp/carnivores-map-codecs
/tmp/carnivores-map-codecs
```

`Carnivores2ResourceTests` now runs **13 tests** (seven existing resource tests,
six map tests). It still compiles the actual Resources.cpp, ModelLoader.cpp and
StateDefs.cpp. A test translation unit observes the existing Win32 close call,
records the current position and calls the real CloseHandle. Existing PrintLoad
notifications stop RSC-only tests before MAP open, or map tests after fog mutation
and handle close, before CreateTMap/UI loading. There is no alternative parser
or production test-only branch. Initial characterization tests passed against
the unchanged reader before migration.

The independent synthetic writer streams rows, covering every plane, byte
0/0x80/0xff, word/sentinel values, known/unknown flags, row edges and last cells.
Production coverage checks all three selected light maps and end position,
truncation before/inside every plane and each stored light plane, one-byte-short
end, invalid primary/secondary textures (preserving rejected high-bit values),
object/water references, dry-water behavior, 64/65 landing markers, sentinel
requirements, byte-reader bounds, word chunk boundaries and fog mutation.
0x1234/0x9234 texture values are deliberately rejected by unchanged validation;
claiming they form a valid full map would weaken existing content checks.

### Real-map corpus and stable comparison

Available copies and the Genesis Redux installation/backup under `~/Games`
contain nine distinct area MAP hashes. All are 14155776 bytes. Fresh copies,
independent per-plane SHA-256 inventory and production checkpoint comparisons
were kept outside the repository. No game assets are committed.

| Map | Complete-file SHA-256 |
| --- | --- |
| AREA1.MAP | `7f5f33ca2fefe728fa210907dc8042342e829306cbf6dc13925e193bd3e89ac4` |
| AREA2.MAP | `14d7f2dc7f106ef610aea964cbb77a8a58c24e408ffb924d74f141c8d355d562` |
| AREA3.MAP | `cba54f8a88cd6e21b5c06d778ec9012b057c13f38e35393ac626ddd74934b3de` |
| area4.map | `45c658e49d453b1257052ab9dfc7aa6ed68cedb8b0031a9f551c47420cda332d` |
| AREA5.MAP | `132a8aca505878ee221354ccec25b8af132cc338c3d95349215d0df8ac4513fb` |
| AREA6.MAP | `855b5d3628b2370cb08ee842c410967623a43a176e8752008ea2c16d9f0b9a7e` |
| area7.map | `8a6792a72db1940178a40ae9884a5fecf392351cdfeb460a6350cc968ab279e4` |
| area8.map | `588313d8e87f2af232da3a4cbecb99567a2ce1dd5b82835ab298dbd078ee9078` |
| TROPHY.MAP | `3d989a086c4bdf33da527fb856fd65eaa78c204475ef293e96576a7e015a3e18` |

An independent Python inventory reads literal file offsets, decodes uint16 with
`struct.unpack('<...H')`, and hashes canonical little-endian values separately
from byte planes. It records every stored light plane, all other planes, full
file size/hash and final offset. Terrain ranges vary across maps (AREA1 0..169,
AREA5 0..246; several reach 255); fog and ambient regions vary substantially.

Local-only probes compile stable `e0771f8` and new production Resources.cpp with
identical lighting/model/UI doubles and deterministic random inputs. Read-only
checkpoints capture every runtime plane before validation/fog mutation and the
post-fog plane separately. Runtime WORD snapshots are encoded numerically to
canonical little-endian bytes for SHA-256 comparison, not treated as opaque host
memory. The probes execute all nine maps at days 0/1/2 in stable/new x86 GL,
stable/new x64 GL and stable/new x86 SOFT: **162 complete loads**.

Every stable/new raw-plane and post-fog SHA-256 matches within each target.
New x86/x64 GL hashes also match. Every raw runtime plane independently matches
its file-plane SHA-256, including all three selected LMaps. All runs stop at
14155776. Raw file values and the existing fog mutation are kept distinct;
CreateTMap, shadow rendering and minimap generation are covered by full-engine
smoke checks rather than the probe's pre-postprocessing boundary. All original,
backup and copied map hashes were rechecked unchanged.

### Runtime smoke checks

Full Release x86 GL, x64 GL and x86 SOFT engines each ran two scenes using fresh
copies, matching OpenAL DLLs and isolated Wine prefixes under headless Gamescope
at 1024 x 768:

- AREA1 at (430.5,203.5): beach/grass terrain, tree/foliage objects, horizon water,
  textured weapon and sky. The raw fog plane is empty; this is water/coast coverage.
- AREA5 at (430.5,432.5): different terrain and forest/foliage, in an active fog
  region (12), with multiple ambient-region indices elsewhere in the map.

All six entered the game loop, responded to window probes, initialized OpenAL,
loaded ambient/random resources, logged Trophy Saved and TrophyB Saved through
evacuation, shut down audio and exited 0. Compositor captures show intact terrain,
texture placement, object locations, water and forest/fog appearance. GL x86/x64
scenes agree; SOFT retains its expected pixel/dither appearance. AREA5 minimaps
rendered in all three targets. AREA1's initial Tab requests left the map closed
because the weapon was raised (existing ToggleMapMode policy); an additional
x64 run stowed it, displayed the coastal minimap successfully, then saved and
exited cleanly.

This is bounded automated visual/runtime validation, not exhaustive gameplay,
pristine stock coverage, native Windows execution or certified audible playback.
Ambient region data and resource loading are verified; listening to transitions
between every ambient region was not performed. Evacuation uses its save/exit
path, which does not emit the WM_CLOSE-specific normal-shutdown message.
Existing copied-profile sensitivity -64 and V weapon binding were retained to
control headless pointer drift; no map/resource/model assets were edited.

Build/test/sanitizer logs, independent inventory, checkpoint SHA-256, scope audit,
per-run logs, profiles and compositor captures are archived outside the repository
at `~/Games/carnivores-map-validation/evidence/`. Working copies/probes were run
under `/tmp/carnivores-map-validation/`.

## Final scope and remaining persistence audit

The full diff against stable main changes only map codecs/I/O/call sites, tests,
test registration and this document. The RSC portion of LoadResources,
ValidateMapReferences, and the entire suffix beginning at that validation call
compare byte-for-byte equal to stable main. Model, resource, profile and trophy
codecs/adapters are unchanged. No map native WORD reads remain. Intentional raw
map reads are exactly the byte planes listed above; staging buffers precede
explicit word decoding. All three stored lights retain their order and consumed
size. No renderer, terrain, gameplay, filesystem, platform, networking, input,
audio-backend or menu algorithm changes are included.

The remaining direct-read inventory was traced through engine and menu loading
paths, including reads into byte arrays later reinterpreted as multibyte data:

| Classification | Remaining paths and interpretation |
| --- | --- |
| 1. Already explicit | CAR/3DF geometry, headers, animations, texture words and association tables through LegacyModel; RSC metadata, pixels and PCM through LegacyResource; C2 saves/trophies through LegacyProfile; MAP uint16 planes through LegacyMap. Their byte staging reads are intentional. |
| 2. Genuine byte blobs | MAP unsigned-byte planes; RSC SkyMap; fixed model/sound names and reserved bytes; BMP BGR byte triples; WAV chunk-name scanning; menu 400 x 300 hit-map (`m_Image_Map` uint8); config/resource/shader text. None requires endian conversion. |
| 3. Small remaining persistence risks | PictureLoader.cpp: native 14/40-byte Windows BMP headers; TGA width/height WORDs at 12/14 and 16-bit pixel rows. SoundLoader.cpp: native four-byte WAV length and raw PCM16. ModelLoader.cpp: CAR PCM remains raw signed-short storage (its length is already explicit). These are active gameplay/UI/audio dependencies, not hypothetical unused formats. |
| 3. Deferred menu equivalents | Menu/Resources.cpp: packed TGA header plus uint8 payload subsequently reinterpreted/copied as uint16 pixels; native WAV length and PCM. Menu's existing odd-length PCM allocation and unbounded chunk scan are additional separate malformed-input risks. The TGA byte allocation does **not** make its pixels representation-independent. |
| 4. Not required for initial C2 Linux gameplay loading | `_iceage` profile branch and its registration reader; native BMP screenshot **writes** (output path); inactive historical reads in comments. Runtime GPU buffers and network helpers are not persistent asset reads; networking remains separately deferred. |

The major proprietary model/resource/map/save contracts are now explicit enough
to begin thin platform work targeting little-endian Linux x64. There is no new
large undocumented proprietary binary container found in the main gameplay
asset path. **Serialization is not universally complete:** required TGA UI art,
BMP headers and standalone/CAR PCM still rely on native multibyte representation.
A small image/audio boundary follow-up is recommended before declaring all asset
loading host-endian independent, and should accompany platform preparation rather
than being silently omitted. Linux x64's little-endian representation alone does
not justify retaining native disk headers indefinitely. No such follow-up was
folded into this branch.
