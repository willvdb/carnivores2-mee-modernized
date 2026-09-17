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
