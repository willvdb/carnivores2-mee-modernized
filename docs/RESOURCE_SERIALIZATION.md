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
