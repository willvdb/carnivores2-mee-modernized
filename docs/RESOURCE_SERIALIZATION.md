# Legacy resource serialization

Characterized from stable `main` (`f66adb8`) before production edits, by tracing
`LoadResources` in Resources.cpp and `LoadTexture`, `LoadBMPModel`, `LoadSky`,
`LoadSkyMap`, `LoadModel` and `LoadAnimation` in ModelLoader.cpp. Offsets and
lengths below are bytes. All multibyte integers are little endian. Signed fields
are two's-complement int32; floats are IEEE-754 binary32 bits. There is no magic
or resource version. Existing content needs no conversion.

## Stream order

1. `tc` int32 at 0, `mc` int32 at 4. Existing policy accepts 0..1024 textures
   and 0..256 models (negative values are rejected).
2. `FadeRGB[3][3]` at 8 (36 bytes), then `TransRGB[3][3]` at 44 (36 bytes).
   Each table is nine int32 values, row-major: three RGB triples. Day/night
   selects a row; mode 2 clears red/blue, then brightness scales/clamps colors.
3. Starting at 80: `tc` terrain base images, each 128 x 128 little-endian uint16
   words (32768 bytes). Zero words become 1, then brightness, average color,
   mipmaps, shaded copies, DATASHIFT and alpha processing run in their existing
   order. Mipmaps, averages and shaded copies are runtime data only.
4. For each model: one 64-byte object-info record, embedded model, 32768-byte
   billboard (128 x 128 uint16), then optional object animation if `ofANIMATED`.
   Embedded model and animation bytes are specified in
   [MODEL_SERIALIZATION.md](MODEL_SERIALIZATION.md); their codec is independent.
   Radius/YLo/YHi double, line length truncates to a multiple of 128; lighting,
   bound computation, mipmaps and alpha remain runtime operations. Stored BoundR
   is subsequently recomputed. Billboard brightness, DATASHIFT, nonzero alpha
   and billboard vertex construction follow the read.
5. Three consecutive 256 x 256 uint16 sky images, 131072 bytes each. The loader
   seeks forward `131072 * OptDayNight`, reads one image, then seeks forward
   `131072 * (2 - OptDayNight)`. Preserve these exact seeks (including their
   existing unchecked return behavior). Brightness, sky fades and alpha follow.
6. Sky map: 128 x 128 = 16384 raw bytes. `SkyMap` is BYTE storage and Effects.cpp
   samples four individual bytes for interpolation; it has no multibyte scalar
   encoding. This is deliberately an opaque byte/intensity plane.
7. Fog count int32 (0..255), then count x 20-byte fog records. Destination starts
   at `FogsList[1]`; runtime color processing includes the preexisting slot 0.
   The D3D RGB conversion remains; the source already removed night-vision fog
   tint in favor of the per-frame overlay.
8. Random sound count int32 (0..256), then for each: length int32 followed by
   exactly length PCM bytes. Length must be 0..16777216 (16 MiB).
9. Ambient count int32 (0..256), then per entry: length int32, exactly length
   PCM bytes, **all 16** random-effect records (256 bytes), RSFXCount int32
   (0..16), AVolume int32. RndTime is not stored: when count is nonzero it is
   `(first.RFreq / 2 + rRand(first.RFreq)) * 1000`. Mode 2 removes entries with
   nonzero Flags using the existing compaction. First-record RFreq/REnvir are
   saved before filtering and restored afterward, even if that record is removed.
10. Water count int32 (0..256), then count x 16-byte water records. Each texture
    index must be in [0, tc). Slot 255's wlevel is zeroed and fogRGB is recomputed
    from the chosen terrain texture's runtime average colors.

The resource handle is closed here. Trailing bytes are ignored. The subsequent
`Loading .map...`, map open, all planes and map postprocessing are outside this
format and this branch. There is no new filesystem abstraction.

## Fixed records

| Record | Offset | Encoding / field |
| --- | ---: | --- |
| Object info (64) | 0,4,8 | int32 Radius, YLo, YHi |
| | 12,16,20,24 | int32 linelenght, lintensity, circlerad, cintensity |
| | 28,32,36,40 | int32 flags, GrRad, DefLight, LastAniTime |
| | 44 | uint32 bits of float BoundR |
| | 48..63 | 16 opaque reserved bytes |
| Fog (20) | 0 | int32 fogRGB |
| | 4 | float bits YBegin |
| | 8 | int32 Mortal |
| | 12,16 | float bits Transp, FLimit |
| Random effect (16) | 0,4,8 | int32 RNumber, RVolume, RFreq |
| | 12,14 | uint16 REnvir, Flags |
| Water (16) | 0,4 | int32 tindex, wlevel |
| | 8 | float bits transp |
| | 12 | int32 fogRGB |

## Pixel and audio interpretation

Texture words retain all 16 bits at the boundary, including bit 15. Existing
555 color, brightness, zero replacement, DATASHIFT and alpha rules determine the
rendered result. All three resource texture paths have fixed even byte counts;
there is no odd resource texture payload convention.

Audio_DLL.cpp `GetBuffer` passes resource `short int` data to `AL_FORMAT_MONO16`
with the original byte length and rate 22050. This and the old Windows loader
establish signed 16-bit little-endian mono PCM, without an embedded WAV header.
Runtime storage holds ceil(length/2) zero-initialized samples. An odd final byte
is the low byte of the last sample, with high byte zero, so its value is 0..255.
The backend still receives the original odd byte length; decoding must neither
consume a padding byte nor change playback APIs, rate, channels or length caps.

## Implementation and failure boundaries

`Shared/LegacyResource.h` is a C++17, OS-independent decoder for int32, color
triples, object/fog/random-effect/water records, uint16 pixels and PCM16 samples.
It has no runtime pointers, Win32 types, packed records, host `long` fields or
serialized native object copies. Float values remain uint32 bit containers.
Negative counts/indices and unusual float bits are transported faithfully;
existing engine policy still decides whether they are usable.

`Hunt/Loaders/ResourceSerialization.h` adapts each field to the existing runtime
types, using checked IEEE-754 float bit transfer. `ResourceIO.h` retains Win32
`ReadExact` and reads bounded byte arrays before decoding/adaptation. Texture
and audio payloads use 4096-byte chunks and numeric assignment into runtime
words/samples. Audio storage retains the original rounded, zero-initialized
allocation and byte length. No OpenAL source or API changed.

All scalar/header reads and fixed records in the RSC portion of `LoadResources`
are migrated. The three RSC-only texture readers in ModelLoader.cpp use the new
word reader. Their brightness, mipmap, average-color, DATASHIFT, alpha and seek
logic is unchanged. The embedded model/animation implementation still exclusively
uses `LegacyModel`; its implementation and adapters are byte-for-byte unchanged.

Codec short-input/capacity/null failures do not modify output. Fixed engine
record reads also adapt only after a complete read. Streaming texture/audio
loads can retain earlier successful chunks on a later truncated chunk, then
halt through the existing error path; the whole file load is not transactional.
The old runtime-layout assertions and tests remain independent compatibility
oracles, not persistent sizing rules.

## Validation (2026-09-17)

MSVC 19.44.35229 / Windows SDK 10.0.26100.0 under Wine 11.17, using the local
commands in [WINDOWS_BUILDS.md](WINDOWS_BUILDS.md). No native Linux engine target
was introduced.

| Configuration | Full default build | Individual asset-free tests |
| --- | --- | --- |
| Windows x86 GL Debug | Passed | 184/187 |
| Windows x86 GL Release | Passed | 184/187 |
| Windows x64 GL Debug | Passed | 187/190 |
| Windows x64 GL Release | Passed | 187/190 |
| Windows x86 SOFT Release | Passed | 184/187 |
| Windows x86 menu Release | Passed | 184/187 |

Every resource, profile, model, layout, memory and fog test passes. The only
failures are the three documented Wine UI-font cases. The preserved `d62905a`
baseline executable was rerun: all six configurations reproduce its measurements
(38 versus 38 twice; 145 versus 144; 290 versus 288). CTest still reports exit 8,
7/8 executables passing. No test was suppressed or UI implementation changed.

Independent synthetic fixtures cover every record field and reserved byte,
all 18 color values, signed extrema, high-bit colors/uint16 flags, negative
indices/counts, negative zero/subnormal/infinity/NaN bit transport, texture
endianness and signed PCM extrema. Every short fixed-record length and payload
capacity/null/overflow boundary fails without partial codec output changes.
All six resource codec tests plus the twelve unchanged model/profile codec
tests pass GCC C++17 with ASan+UBSan (18/18), without sanitizer diagnostics.
Reproduction (GoogleTest source is fetched by the Windows builds):

```sh
gtest=build/cleanup-msvc-windows-x86-gl-release/_deps/googletest-src/googletest
g++ -std=c++17 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$gtest/include" -I "$gtest" tests/test_legacy_resource.cpp \
  tests/test_legacy_model.cpp tests/test_legacy_profile.cpp \
  "$gtest/src/gtest-all.cc" "$gtest/src/gtest_main.cc" -pthread \
  -o /tmp/carnivores-resource-codecs
/tmp/carnivores-resource-codecs
```

`Carnivores2ResourceTests` compiles the actual Resources.cpp, ModelLoader.cpp
and engine state definitions, with renderer/lighting/UI test doubles. Its seven
tests stop by throwing from the **existing** `PrintLoad("Loading .map...")`
notification, after RSC close and before MAP open. There is no test loader or
production test-only seam. Tests cover complete streams, all three sky selections
and exact seek positions, all 16 ambient records and filtering/RndTime behavior,
zero counts, maximum fog/water tables, invalid counts/lengths/texture indices,
section truncation, payload chunk boundaries, odd PCM, and an animated embedded
model followed by the real sky/sound/water sections. Geometry/UV/texture
processing uses the selected build's actual GL or SOFT path.

### Copied user-owned resource corpus

Fresh copies are outside the repository at
`~/Games/carnivores-resource-validation/`. Available content is **Genesis Redux
1.1**, a mod distribution, not independently authenticated pristine stock C2.
The installed copies and backups under `~/Games` contain the same nine distinct
RSC hashes; no additional unique MEE/stock resource corpus was found. Original
and copied resource hashes were checked after validation and still match.

| File | Textures | Models | Fog | Random sounds | Ambients | Water |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| AREA1.RSC | 83 | 8 | 2 | 8 | 3 | 10 |
| AREA2.RSC | 40 | 13 | 3 | 12 | 16 | 1 |
| AREA3.RSC | 57 | 15 | 5 | 19 | 7 | 2 |
| area4.rsc | 67 | 14 | 2 | 1 | 4 | 3 |
| AREA5.RSC | 177 | 19 | 14 | 12 | 18 | 10 |
| AREA6.RSC | 71 | 25 | 4 | 3 | 5 | 10 |
| area7.rsc | 48 | 9 | 4 | 4 | 3 | 3 |
| area8.rsc | 111 | 33 | 7 | 6 | 5 | 8 |
| TROPHY.RSC | 25 | 8 | 1 | 2 | 1 | 0 |

An independent Python byte inventory records every section's offset, length
and SHA-256. All nine files end exactly after the water table. None contains
animated resource objects or odd PCM lengths; synthetic production tests cover
those conventions. PCM section interpretation agrees with the signed mono16
backend contract; audible playback was not certified.

Local-only MSVC probes compile stable `main` (`f66adb8`) and the new production
source, with identical lighting/bounds/render-cache test doubles and deterministic
random inputs. Read-only trace hooks hash the actual runtime destinations at
pre-processing and final checkpoints. All nine files load for all three day/night
values in stable/new x86 GL, stable/new x64 GL and stable/new x86 SOFT: **162
complete resource loads**. Every stable/new checkpoint matches within each
configuration, and all new x86/x64 GL checkpoints match each other.

Compared values include both color tables, every pre-transform object record,
terrain words before zero replacement/brightness, billboard and selected sky
words before brightness, sky-map bytes, fog records before conversion, all 16
ambient records before filtering, water before fog-color recalculation, sound
lengths and complete runtime sample storage. Final comparisons cover colors,
terrain mipmaps/averages, object info, embedded-model vertices/faces/textures,
billboards, sky/fades, fog, random PCM, ambient PCM/metadata/filtering and water.
The RSC end position is identical for every run. Lighting/bounding calculations
are test doubles in these probes; full-engine runtime checks supplement them.

Evidence: `evidence/corpus.json`, `available-corpus.json`, `section-inventory.json`,
`comparison.log` and six checkpoint transcripts under the validation directory.
No HUNTDAT, profiles, screenshots, local instrumentation or binaries are committed.

### Runtime smoke checks and limits

Full Release engines (x86 GL, x64 GL, x86 SOFT) each ran two hunts under headless
Gamescope at 1024 x 768 with matching OpenAL DLLs and the copied content:

- AREA1 at (430.5, 203.5): textured ground/beach, sky/clouds, tree/foliage models,
  distant water and weapon/compass are visible in compositor captures.
- AREA5 at (430.5, 432.5): an active fog-map region (index 12), with nearby and
  distant forest/foliage and terrain. AREA1's stored fog map is empty, so it was
  not used to claim active fog coverage.

All six runs entered the game loop, responded to window probes, initialized
OpenAL, loaded random/ambient resources, reached evacuation/save, logged both
`Trophy Saved.` and `TrophyB Saved.`, shut down audio and exited with code 0.
The save/evacuation path does not emit the WM_CLOSE-specific normal-shutdown
message. Screenshot inspection shows no obvious texture, sky, static-object,
water or fog regression. GL x86/x64 scenes agree; SOFT retains its characteristic
pixel/dither appearance. This is bounded scene inspection, not exhaustive visual
coverage of every resource or close-up certification of every billboard.

Preserved x86 Release executables from the previous model validation were also
run on these fresh copies for baseline captures (AREA1 GL, AREA5 GL and SOFT).
Their source revision `120e226` has the same complete tree as stable `f66adb8`.
The source-compiled checkpoint comparisons above use `f66adb8` directly. The
baseline and new scene captures retain the same terrain/model/water/fog appearance;
moving clouds and session timing prevent pixel-identical frame comparisons.

Only copied profiles were adjusted by the earlier visual-validation setup
(neutral mouse sensitivity -64, weapon-show bound to V) and subsequent hunt
saves. No resource/model/map assets were edited. External X11 capture remains
black/unusable; Gamescope compositor capture supplied the inspected images.
The first compositor request occasionally produced no file, but the later
captures and motion sequences succeeded.

Native Windows runtime behavior, audible playback, exhaustive stock/mod
compatibility, every billboard/animation and long-session gameplay are not
certified. The available corpus and local tests are bounded evidence, not a
claim of pristine stock coverage. Build/test/sanitizer/baseline-font/audit logs
are in `~/.cache/carnivores-resource-20260917/`; per-run save logs, profiles and
captures are under `~/Games/carnivores-resource-validation/evidence/`.

## Final scope audit and next slice

The full diff against stable `main` changes only the resource codec, adapters,
Win32 byte readers, RSC production call sites, tests, test registration and this
document. The entire Resources.cpp suffix starting at the MAP boundary compares
byte-for-byte equal to `main`. Undoing only the three resource texture read
substitutions and their include makes ModelLoader.cpp equal to `main` as well.
Shared/LegacyModel.h, ModelSerialization.h and all profile serialization sources
are unchanged. There are no host `long` disk fields or native scalar/struct reads
remaining in the RSC path.

Intentionally retained direct byte reads:

- `LoadSkyMap`: exactly 16384 BYTE values, individually sampled by Effects.cpp;
  no endian conversion applies.
- Fixed-size byte staging arrays/chunks in ResourceIO and the unchanged embedded
  model loader: explicit codecs follow these reads; these are not runtime records.
- Commented-out historical billboard `ReadFile` code is inactive.

The raw `.MAP` plane reads following the resource close remain unchanged, as do
CAR PCM/name bytes in the separate model reader and unrelated picture/audio
formats. Existing CreateFile/ReadFile/SetFilePointer APIs, resource semantic
checks, error routing, renderer algorithms, gameplay and OpenAL architecture
remain in place. Pure decoding supports endian-independent numeric values; this
does not claim the rest of the Windows engine runs on a big-endian host.

The subsequent `.MAP` slice must characterize and explicitly decode its
multibyte planes, retain byte-oriented planes, preserve all three light-map
selection/seek semantics and reference validation, and test real production
stream positions. It must not infer a persistent format from runtime array sizes.
