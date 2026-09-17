# Legacy model serialization

Characterized from stable `main` (`2077b4b`) before production changes. This is
what `ModelLoader.cpp` consumes, not a specification inferred from runtime sizes.
All offsets below are decimal. Integer words are little endian, signed unless
specified otherwise; coordinates are IEEE-754 binary32 bits. No magic/version
is checked. Existing files need no conversion.

## Containers and ordering

`.3DF` (`LoadModelEx`) and embedded model records (`LoadModel`) start with a
16-byte header: VCount at 0, FCount at 4, OCount at 8, TextureSize at 12, each
int32. Faces follow (FCount × 64), then vertices (VCount × 16), then objects
(OCount × 48), then TextureSize bytes. Embedded model records are the only
`.RSC` geometry changed by this slice; surrounding resource tables are separate.

`.CAR` (`LoadCharacterInfo`) starts with 32 opaque name bytes, AniCount int32 at
32, SfxCount int32 at 36, VCount int32 at 40, FCount int32 at 44, TextureSize
int32 at 48. Faces start at 52, followed by vertices and texture. There is no
OCount or object-record block. Animation records follow, then sound records,
then an optional 256-byte animation/sound association table (64 int32 entries).
A missing/short association table sets **all** entries to -1. Trailing bytes
are ignored. Fixed names preserve all bytes, including bytes after a NUL.

Existing limits: 1 <= VCount <= 1048576; 0 <= FCount <= 1048576;
0 <= OCount <= 1024; 0 <= AniCount/SfxCount <= 64; TextureSize >= 0.
Face indices must each address a loaded vertex. These are loader policies,
not new fields or codec normalization rules.

## Geometry records

| Record | Offset | Bytes | Value |
| --- | ---: | ---: | --- |
| Vertex (16 bytes) | 0, 4, 8 | 4 each | x, y, z float bits |
| | 12, 14 | 2 each | owner, hide signed int16 |
| Face (64 bytes) | 0, 4, 8 | 4 each | v1, v2, v3 int32 |
| | 12, 16, 20, 24, 28, 32 | 4 each | tax, tbx, tcx, tay, tby, tcy **integer** UVs |
| | 36, 38 | 2 each | Flags, DMask unsigned uint16 |
| | 40, 44, 48 | 4 each | Distant, Next, group int32 |
| | 52 | 12 | opaque reserved bytes |
| Object (48 bytes) | 0 | 32 | OName, opaque name bytes |
| | 32, 36, 40 | 4 each | ox, oy, oz float bits |
| | 44, 46 | 2 each | owner, hide signed int16 |

No padding bytes are omitted. Objects are metadata in global gObj, not serialized
TObject instances. Vertex coordinates later scale by (2, 2, -2). `LoadModel`
computes hardware lighting before that scaling; `LoadModelEx`/CAR do not.
`CorrectModel` receives UV **integer bits**, even though GL runtime fields are
floats. GL then converts signed integers numerically to floats (the inactive
D3D branch additionally divides by 256). SOFT retains its x86 16.16 conversion
(raw << 16) + 0x8000. Flags, face ordering, transparent/opacity passes, and the
existing face-copy count remain unchanged.

## Animations

Object animation (`LoadAnimation`, following an embedded model when resource
flags request it): a 16-byte header contains ignored recordType at 0, vertexCount
at 4, KPS at 8, storedFrameCount at 12, all int32. The vertex count must match
its model. storedFrameCount is in [1, INT32_MAX/256 - 1]; **stored + 1** frames
are read and retained. No frame is synthesized on this path.

Each CAR animation starts with name[32], KPS int32 at 32 and frameCount int32
at 36 (40 bytes total). frameCount is in [1, INT32_MAX/256]. Exactly frameCount
frames are read. For one frame, storage holds two identical frames but runtime
FramesCount and duration continue to use 1. KPS is a signed integer rate, not
float FPS. Duration is floor(runtimeFrames × 1000 / KPS), must fit a positive
runtime int, and KPS must be positive.

Each frame contains VCount XYZ triplets, each **6 bytes**: signed int16 x at 0,
y at 2, z at 4. No runtime pointer/header is stored. Existing interpolation and
coordinate interpretation remain unchanged. File payload multiplication retains
the UINT32_MAX Win32 transfer ceiling, independently of native runtime storage
size and the extra one-frame copy.

Each CAR sound record contains an ignored name[32], int32 byte length at 32,
then that many PCM bytes. Existing length limit is 0..16 MiB with rounded-up
runtime sample storage. Audio payload/backend changes are outside this slice.

## Model texture words

TextureSize is a **byte count**, not a pixel count. Words are little-endian
uint16, with B/G/R in bits 0..4 / 5..9 / 10..14. Bit 15 is preserved on decode;
existing brightness/alpha operations subsequently determine its runtime value.
Rows are 256 pixels (512 bytes). HARD3D allocates 256 rows regardless of the
stored byte count; SOFT uses TextureSize >> 9 rows. The existing check rejects
stored bytes exceeding normalized allocation (so SOFT accepts only whole rows).
An odd HARD3D byte count historically puts its last byte in the low half of the
next zero-initialized word; preserve that value and consume exactly that byte.
Brightness visits only floor(storedBytes/2) words. Existing DATASHIFT, alpha,
mipmap behavior and zero-filled unused storage remain unchanged. `LoadModel`
does not generate mipmaps here; `LoadModelEx` and CAR do. Terrain, sky, sky-map
and billboard texture readers elsewhere in this same source are separate resource
payloads and remain outside the model texture migration.

## Implementation and failure boundaries

`Shared/LegacyModel.h` contains only fixed-width format values and little-endian
readers. Float values remain uint32 bit containers until fieldwise adaptation;
no packed structs or object-representation deserialization is used. There are
no encoders because these engine paths only load models. Record decoders reject
null/short input before touching outputs. Samples and texture words check buffer
bounds without overflowing count products. Semantic validation remains in the
loader; negative counts are decoded faithfully, then rejected by existing policy.

`Hunt/Loaders/ModelSerialization.h` adapts vertices, faces and objects. Its UV
mapping preserves the integer inputs to the unchanged renderer passes, with
explicit numeric assignment for SOFT and integer-bit transfer for GL float
storage. Range assertions ensure runtime integers can hold the decoded values.
`ModelLoader.cpp` retains Win32 handles and reads bounded byte chunks before
codec/adaptation. Chunking does not relax the previous total animation transfer
limit. Animation runtime allocations independently use native element sizes;
one-frame duplication copies runtime elements, not file byte strides. Memory
tags, ownership, count/index checks, lighting, brightness and mipmaps are retained.

Migrated paths: `LoadModel`, `LoadModelEx`, `LoadCharacterInfo` geometry, model
headers and textures; `LoadAnimation` and CAR animation headers/samples; CAR sound
length words and the optional Anifx table. Remaining direct payload reads are:

- Terrain texture (32768 bytes), sky image (131072), sky map (16384), and
  billboard texture (32768): separate resource pixel/byte buffers, outside this
  model-format slice. These still need resource-format endian work.
- CAR sound name[32]: deliberately ignored byte blob.
- CAR sound PCM: deliberately unchanged audio byte blob into the existing
  rounded-up audio storage. Its length now has explicit int32 decoding; PCM
  endianness/audio integration remain a separate slice.

There are no remaining direct model geometry, animation sample, or model header
reads into native runtime records. No model disk field uses host `long` or
runtime `sizeof`; `sizeof` remaining here describes allocations, renderer
scratch/conversion or unrelated runtime buffers. Old layout assertions and
serialized-layout tests are retained as independent regression oracles. The
profile implementation and all pre-existing profile tests are unchanged.

## Validation (2026-09-17)

MSVC 19.44.35229 / Windows SDK 10.0.26100.0 under Wine 11.17, using the local
toolchain and six commands in [WINDOWS_BUILDS.md](WINDOWS_BUILDS.md). All default
builds passed and PE machine types matched the requested architecture.

| Configuration | Build | Individual asset-free tests |
| --- | --- | --- |
| Windows x86 GL Debug | Passed | 169/172 |
| Windows x86 GL Release | Passed | 169/172 |
| Windows x64 GL Debug | Passed | 172/175 |
| Windows x64 GL Release | Passed | 172/175 |
| Windows x86 SOFT Release | Passed | 169/172 |
| Windows x86 menu Release | Passed | 169/172 |

Only the same three Wine UI-font cases fail; the preserved `d62905a` executable
was rerun and reproduces the exact measurements (38 vs 38 twice, 145 vs 144,
290 vs 288). CTest reports 6/7 executables passing and exits 8, without suppressing
those failures. All profile, model, allocator and fog tests pass.

New coverage: `legacy_model_fixtures.h` provides independent synthetic bytes;
`test_legacy_model.cpp` has six OS-independent codec tests;
`test_model_serialization.cpp` checks stable layouts and every adapted byte;
`test_model_loader.cpp` exercises actual production file I/O with synthetic
files, heap/lighting test doubles and the selected renderer's real correction
path. Its eight tests (including the two shared adapter/layout cases) pass for
GL and SOFT. Coverage includes one-frame duplication, stored-plus-one object
frames, short optional associations, precise stream position, invalid geometry
and animation counts, duration/size limits, malformed headers, all scalar/sample
extremes, every short record length, full names, reserved bytes, integer UVs,
odd texture bytes, and SIZE_MAX buffer edge cases. Original profile tests and
legacy record-size/offset assertions remain independent and green.

Standalone GCC C++17 with ASan+UBSan runs all six new codec tests and all six
unchanged profile codec tests: 12/12 pass, no sanitizer diagnostics. This adds
no Linux engine target. Reproduction from the repository root (GoogleTest source
already fetched by a Windows test build):

```sh
gtest=build/cleanup-msvc-windows-x86-gl-release/_deps/googletest-src/googletest
g++ -std=c++17 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$gtest/include" -I "$gtest" tests/test_legacy_model.cpp \
  tests/test_legacy_profile.cpp "$gtest/src/gtest-all.cc" \
  "$gtest/src/gtest_main.cc" -pthread -o /tmp/carnivores-codec-tests
/tmp/carnivores-codec-tests
```

### User-owned asset comparison

Fresh copies live at `~/Games/carnivores-model-validation/`, outside the repo.
All 122 model files in the copied Genesis Redux 1.1 HUNTDAT were tested: 118 CAR
and four 3DF (BINOCUL, COMPAS, MOON, SUN2). They include stock-style names such
as STEGO, TIREX_old1, SHOTGUN and PISTOL, and custom models such as amarg_f,
igu_f, stego variants, compbow2 and oushot. This is a mod distribution, not
independently authenticated pristine stock content; no separate original C2
installation was available. The other locally installed Carnivores game is
Cityscape and does not supply this format family.

The sample spans 4 vertices (muzz4) through 1145 (amarg_f), and up to 2042 faces
(igu_f), with multiple face flag combinations including transparency/opacity.
There are 1072 animation records; none has one stored frame, so that convention
is covered by synthetic production-loader tests instead.

A local-only MSVC harness compiles the actual stable `main` (`2077b4b`)
ModelLoader.cpp and the new source, with heap/lighting/render-cache test doubles.
For every geometry record it also compares new decoded/adapted bytes against the
stable native-layout oracle **before correction**, including all reserved/name
bytes. All files loaded successfully. Stable x86 GL, new x86 GL and new x64 GL
produce identical counts and hashes for pre-correction geometry, pre-brightness
textures, final vertices/faces/textures, runtime animation samples (including
storage conventions), frame counts, KPS and durations. A separate stable/new
x86 SOFT comparison also matches for all 122 files with HARD3D disabled and the
actual SOFT UV conversion. Independent Python parsing records per-section SHA-256
hashes, flags, names, counts and animation metadata. Original installation model
hashes still match the copied fixtures. No assets or local harness binaries are
committed. This sample does not establish exhaustive mod compatibility.

### Runtime checks and limits

Full x86 and x64 GL Release engines entered area1 on the copied content, loaded
creature/weapon models and embedded resource models, initialized matching OpenAL
DLLs, responded to window probes, evacuated through the save path, logged both
`Trophy Saved.` and `TrophyB Saved.`, shut down audio and exited with code 0.
Both observer and ordinary hunt runs passed. x86 SOFT Release also entered the
hunt, rendered and saved/exited successfully.

Compositor screenshots were inspected: both GL architectures show the textured
shotgun, hands, trees/foliage and compass; x64 GL and SOFT captures additionally
show a textured flying creature. Time-separated captures show moving scene
content. Local visual runs set copied profile mouse sensitivity to -64 (neutral
mouse delta) and remapped weapon-show to V to avoid headless pointer drift;
models and content files remained unchanged. This is bounded automated
observation, not a human gameplay session or close-up inspection of every
creature/animation. The x86 GL captures did not directly capture a nearby
creature, and no full animation cycle was visually certified. Native Windows execution, audible playback, broad gameplay,
and exhaustive visual/mod coverage remain unverified.

External X11 capture returned black frames; Gamescope's compositor capture
provided the usable images. A built-in F12 screenshot attempt lost the game
window on both new and stable x86 engines; that reproduced baseline limitation
was left outside this slice, with failed evidence retained. Successful runs use
compositor capture and reach the normal evacuation/save path.

Build/test/sanitizer/audit logs: `~/.cache/carnivores-model-20260917/`.
Asset comparison inventories, per-run logs, copied profiles and screenshots:
`~/Games/carnivores-model-validation/evidence/`. Test commands use the existing
`build/cleanup-msvc-<preset>` directories. Final scope review changes only the
model codec/adapters/loader, synthetic model tests, their CMake registration and
this document; no profile, map, network, gameplay, renderer algorithm, platform,
audio backend or other resource-format implementation is changed.
