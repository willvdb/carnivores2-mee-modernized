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
- LMap: selected lighting samples, interpolated by GetLandLight and sampled by
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
