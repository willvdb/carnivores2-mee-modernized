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
