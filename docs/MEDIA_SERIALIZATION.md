# Legacy image and audio inputs

Characterized at stable main `fabd0a8` before production edits. These are the
contracts consumed by the code, not general TGA/BMP/RIFF specifications.

## Images

Engine `LoadPictureTGA` reads unsigned little-endian width/height at 12/14,
then width * height little-endian uint16 words from offset 18. Every file row
lands at height - row - 1. All other header fields are ignored, including ID,
color map, type, depth and descriptor. Positive dimensions and the existing
INT_MAX pixel/native allocation checks apply. `conv_pic` is a separate unchanged
555-to-565 pass for HARD3D; it is not part of loading.

Menu `ReadTGAFile` consumes an 18-byte header, rejects nonzero color-map type
and image type other than 2, skips ID-length bytes, then reads
width * height * floor(bits/8) bytes. All header fields, including unused color
map offset/length/depth, x/y origin and descriptor, survive as values. Descriptor
and origins do not control orientation. `LoadPicture` requires depth 16, keeps
file row order and forces bit 15 on every pixel. Menu drawing reverses rows.
Full-screen backgrounds instead copy 960000 bytes into 800 * 600 words without
forcing alpha; this is another persistent word boundary in Menu.cpp.

Engine `LoadPicture` consumes exactly 14 + 40 header bytes, uses only signed
little-endian width/height at 18/22, then consumes tightly packed BGR triples
from offset 54. Signature, file length, reserved fields, pixel offset, DIB size,
planes, bit depth, compression, image size, resolution and color counts are
ignored. Width must be 1..800 and height positive with checked allocation.
There is no row padding skip. Rows reverse into runtime storage; each channel
is divided by 8 and packed B/G/R into bits 0/5/10. No conv_pic occurs here.
Negative/top-down heights are rejected by existing policy.

## Audio

Engine `LoadWav` searches bytewise from offset 36 for literal `data`; a failed
'd' candidate rewinds three bytes. File size/exact reads bound the search.
The next four bytes are signed little-endian int32 length, accepted only in
0..16 MiB. Exactly that many signed little-endian PCM16 bytes follow.
Runtime storage has ceil(length/2) zero-initialized samples: an odd final byte
is the low byte of the last sample, with zero high byte. No pad byte is consumed.
The original byte length goes to mono16 OpenAL at 22050 Hz; RIFF/fmt metadata
is not interpreted and playback is unchanged.

CAR sound records have ignored 32-byte names, already explicit signed int32
length, then the same PCM bytes/limit/rounding. Optional association data begins
immediately afterward; odd payloads must not shift it. RSC already decodes PCM
explicitly and is outside this migration.

Menu `LoadWave` independently starts at 36 and uses the same scan/rewind pattern.
Its length is unsigned uint32 and frequency defaults to 22050. The old reader
has no EOF check or length cap and allocates floor(length/2) before reading
length bytes. Missing chunks can loop forever and odd lengths overrun storage.
Small bounded failure/rounding fixes will be isolated from representation edits.

## Initial corpus

Copied user-owned Genesis Redux 1.1 content lives outside the repository at
`/tmp/carnivores-media-validation/corpus`. The initial tree contains 90 TGAs,
73 WAVs and 120 CAR paths (duplicates will be reported separately). All TGAs
are type 2, depth 16, ID=0, color-map type=0; 82 use descriptor 0 and eight 1.
No BMP exists in this tree, so BMP compatibility requires synthetic tight-row
fixtures. WAV data markers are at 36 (67 files) or 112 (six); all lengths even.
This is mod content, not independently authenticated pristine stock content.

## Explicit header fields

All multibyte fields below are little endian. The codecs transport ignored
fields without adding specification-driven rejection rules.

| TGA offset | Width | Decoded value |
| --- | --- | --- |
| 0, 1, 2 | 1 each | ID length, color-map type, image type |
| 3, 5 | 2 each | color-map offset, color-map length |
| 7 | 1 | color-map entry depth |
| 8, 10 | 2 each | x/y origin |
| 12, 14 | 2 each | unsigned width/height |
| 16, 17 | 1 each | pixel depth, descriptor |

| BMP offset | Width | Decoded value |
| --- | --- | --- |
| 0 | 2 | signature (normally bytes `BM`) |
| 2 | 4 | file size |
| 6, 8 | 2 each | reserved words |
| 10 | 4 | pixel offset |
| 14 | 4 | DIB header size |
| 18, 22 | 4 each | signed width/height |
| 26, 28 | 2 each | planes, pixel depth |
| 30, 34 | 4 each | compression, image size |
| 38, 42 | 4 each | signed x/y pixels per meter |
| 46, 50 | 4 each | used/important color counts |

A valid engine TGA consumes `18 + width*height*2` bytes, irrespective of ID,
descriptor or declared depth. Reading all 18 header bytes now detects a missing
header tail earlier; a positive-size image could not previously succeed without
bytes at/after offset 18 anyway. BMP consumes `54 + width*height*3`, even for a
non-54 declared offset or a width requiring padding under the formal BMP spec.
No signature/depth/compression checks were added. No active engine call site for
BMP was found beyond the retained public declaration/implementation; there are
also no user-owned HUNTDAT BMPs. Its historical reader remains covered rather
than being removed or generalized.

Menu TGA consumes `18 + ID length + payload bytes`; trailing/footer bytes are
ignored. Palette *metadata* can be nonzero when color-map type is zero; it is
preserved and no palette bytes are skipped. Origin bits never flip loaded data.
Zero menu dimensions retain the historical successful empty-image behavior;
engine dimensions remain positive-only. The byte API still accepts type-2
non-16-bit data, while `LoadPicture` rejects it. `LoadMenuBackground` retains the
old first-480000-word behavior without enforcing new dimensions or depth rules,
but rejects payloads smaller than its fixed destination. It does not force alpha.
A raw-byte read therefore remains intentional in `ReadTGAFile`: **every active
word consumer now decodes little-endian pixels instead of casting/copying bytes**.

## Implementation and bounded malformed-input changes

- `Shared/LegacyImage.h`: 18/54-byte header value decoders and bounded uint16
  pixels. Value objects are not packed disk structures. Signed BMP fields are
  converted by value, not implementation-defined unsigned-to-signed casts.
- `Shared/LegacyAudio.h`: uint32 length decoding, overflow-safe rounded sample
  count, signed PCM16 decoding including the odd low byte. Engine WAV rejects
  values above 16 MiB before assigning to its signed runtime length, preserving
  rejection of negative/high-bit legacy int32 lengths. Menu retains uint32.
- `Hunt/Loaders/ImageIO.h` and `AudioIO.h`: bounded 4096-byte Win32 staging
  reads, explicit decode and numeric assignment to existing runtime storage.
  The audio adapter asserts the existing backend's signed 16-bit sample range.
- `PictureLoader.cpp`, `SoundLoader.cpp`, CAR sounds in `ModelLoader.cpp`,
  `Menu/Resources.cpp` and the background calls in `Menu/Menu.cpp` are migrated.
  `TARGAINFOHEADER` remains for menu/API/layout compatibility and existing layout
  tests, but no native header read remains. `TargaImage` still owns byte storage.
- RSC codec/I/O and regression tests are unchanged. Model geometry, animation,
  texture codecs and adapters are unchanged. The CAR diff is only the include
  and PCM read replacement. Profile/MAP and renderer/audio backend code are unchanged.

The separately reviewable `fix(menu)` commit bounds the WAV scan at EOF, checks
length/payload truncation, and replaces the floor-sized allocation with rounded,
zero-initialized storage. It verifies the payload is present before allocation,
checks native allocation multiplication, and leaves the prior sound intact on
failure. It **does not** impose the engine's 16 MiB cap on menu files; valid menu
lengths retain their original uint32 domain, subject to available runtime memory.
The added seek-to-end check restores the payload position and reads exactly the
original byte count. No RIFF padding or following byte is consumed.

Menu TGA now rejects short headers/payloads, signed-int byte-product overflow
(the previously defined product was at most INT32_MAX), and undersized background
buffers. These are adjacent memory-safety corrections needed to decode known
bytes, not new image formats. Truncated data no longer reports success with
uninitialized pixels. The old byte storage is retained until a full read succeeds;
header values may already have changed on a failed call, matching the existing
nontransactional API. Fixed codec failures leave output unchanged. Engine chunked
load failure can retain prior complete chunks before DoHalt; no partial chunk is
adapted and no failed load continues into playback/rendering.

## Validation (2026-09-17)

MSVC 19.44.35229 / Windows SDK 10.0.26100.0 under Wine 11.17, using the six
presets and installed toolchain described in [WINDOWS_BUILDS.md](WINDOWS_BUILDS.md).
All default builds pass and PE architectures match. All asset-free tests run.

| Configuration | Full build | Individual tests |
| --- | --- | --- |
| Windows x86 GL Debug | Pass | 215/218 |
| Windows x86 GL Release | Pass | 215/218 |
| Windows x64 GL Debug | Pass | 218/221 |
| Windows x64 GL Release | Pass | 218/221 |
| Windows x86 SOFT Release | Pass | 215/218 |
| Windows x86 menu Release | Pass | 215/218 |

Only the known `UITextTest.LongNameShrinksTheFontToFitThePanel`,
`SingleLongRowIsClampedEvenWithoutPairing`, and
`FiveRowBlockFitsThePanelHeight` fail. The preserved `d62905a` baseline was
rerun; every configuration matches its measurements (38 versus 38 twice,
145 versus 144, 290 versus 288). CTest remains exit 8, 9/10 executables passing;
no failure is suppressed. No new final-build warning was introduced.

Five portable media codec tests plus the 22 unchanged profile/model/resource/map
codec tests pass GCC C++17 with ASan+UBSan and leak checking: **27/27, no diagnostics**.
No Linux engine target was introduced. Reproduction:

```sh
gtest=build/cleanup-msvc-windows-x86-gl-release/_deps/googletest-src/googletest
g++ -std=c++17 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$gtest/include" -I "$gtest" tests/test_legacy_image.cpp \
  tests/test_legacy_audio.cpp tests/test_legacy_map.cpp \
  tests/test_legacy_resource.cpp tests/test_legacy_model.cpp \
  tests/test_legacy_profile.cpp "$gtest/src/gtest-all.cc" \
  "$gtest/src/gtest_main.cc" -pthread -o /tmp/carnivores-media-codecs
/tmp/carnivores-media-codecs
```

Synthetic fixtures contain original bytes, no proprietary assets. Codec tests
cover every header field, signed extrema, endian-distinguishing values, all short
header lengths, zero dimensions without imposing loader policy, 0/1/0x1234/
0x9234/0xffff pixels, PCM INT16_MIN/MAX/-1/0, odd/even/zero lengths, short input,
null/capacity and SIZE_MAX probes. `Carnivores2MediaTests` runs eight tests against
the actual engine picture/sound sources: tight BGR rows and ignored BMP fields,
orientation, conv_pic, invalid dimensions/width 801, every truncated fixture
prefix, WAV scan rewind, exact close position, odd PCM, 4096-byte boundaries and
non-consuming capacity failures. Close/open observation is confined to the test
translation unit; no production-only test seam exists.

`Carnivores2MenuMediaTests` runs six tests against actual Resources.cpp, covering
ID skipping, unused header metadata, file order, descriptor/origin ignored,
opaque icon versus unmodified background alpha, depth policy, zero dimensions,
background capacity, truncation/overflow, bounded WAV scanning and odd storage.
Two additional existing-model-target tests check CAR sounds (including a second
sound and all 64 following associations), signed extrema, odd/zero/4097-byte
payloads, bad lengths and truncation. Initial valid-file characterization tests
were run against stable production code before migration.

### Real asset comparisons

The initial copied current Genesis Redux tree was extended with **all additional
distinct HUNTDAT media found in the local pre-1.1 backup**. Final distinct corpus:

| Kind | Distinct files / payloads | Result |
| --- | --- | --- |
| TGA | 103 files | Identical raw pixels, dimensions, orientation and converted arrays |
| WAV | 68 files (73 current-tree paths before deduplication) | Identical marker offsets, lengths, sample counts and hashes |
| CAR | 125 files, 1069 embedded sounds | Identical lengths, samples, following positions and associations |
| BMP | None available | Synthetic production-loader characterization only |

The current tree's 120 CAR paths include the 118 HUNTDAT models from previous
model validation plus two multiplayer models; five older backup variants expand
this to 125. All 103 TGAs use ID=0, color-map type=0, image type=2, depth=16;
95 have descriptor 0 and eight descriptor 1. Width/height are the only engine
header values used. This corpus supplies no ID/color-map/compressed/origin-bit
variant; independent fixtures establish the preserved policies for such values.
Images include full-screen menus, mapframe, loading/pause/exit/score/trophy art,
weapon/ammunition/flash art, equipment and area/creature/weapon icons.

After deduplication, 67 WAVs locate `data` at 36 and one at 112 (six paths share
that latter content). WAV and CAR payloads are all even in this corpus; odd-byte
behavior is established by synthetic tests. No separate pristine stock C2
installation or unrelated total conversion was available. No BMP padding or
formal-BMP support claim is made from this corpus.

Local probes compile stable `fabd0a8` and new **production loaders**, with identical
heap/lighting/render-cache doubles, for x86 GL, x64 GL and x86 SOFT. Every distinct
file runs in each variant: 1776 engine loads. x86 menu additionally loads all
103 TGAs and 68 WAVs in both variants (342 media entries, with byte/picture and
background adapters exercised for images). Stable background comparison uses the
exact old Menu.cpp memcpy; the new run uses production LoadMenuBackground.
Read-only local instrumentation observes engine close and menu stream positions.
These probes and byte inventories are not committed.

Every stable/new metadata transcript and snapshot matches; all new x86/x64 GL
snapshots also match each other. Snapshots encode numeric runtime words to
canonical little-endian bytes before SHA-256 comparison. An independent Python
literal-offset parser verifies file payloads, engine row reversal, 555-to-565
conversion, menu alpha OR, background prefix, signed samples, sound names,
association tables and CAR final position. PCM canonical sample hashes equal
serialized payload hashes for the even corpus. The engine's consumed byte lengths
and menu's original byte lengths remain equal to stable. Original/copy full-file
hashes are retained and rechecked; no user asset was edited or committed.

### Runtime and menu checks

Full Release x86 GL, x64 GL and x86 SOFT ran on isolated copies under headless
Gamescope at 1024x768 with matching OpenAL DLLs. All entered AREA1, loaded
character and standalone audio, initialized OpenAL, responded to window probes,
displayed weapon/ammunition HUD and compass, opened the coastal map with its TGA
frame, then evacuated, logged both Trophy Saved and TrophyB Saved, shut down
audio and exited 0. Inspected compositor captures show coherent colors and
orientation in all three targets. SOFT retains its normal dithered appearance.
The save path does not emit the WM_CLOSE-only normal-shutdown message.

The x86 Release menu ran in a separate copied game/prefix: selected the existing
profile, entered the main/hunt menus, hovered area/creature/weapon/equipment
entries, returned through quit confirmation, opened options, then shut down and
saved its profile with exit 0. Captures verify menu backgrounds and thumbnails;
its log confirms OpenAL initialization and message-loop/interface shutdown.
Optional `type.wav`/`typego.wav`, several hidden icons and descriptive texts are
absent from the supplied runtime content; the menu reports these and continues
with its existing fallbacks. Old engine logs copied with the runtime directory
are not counted as new menu evidence.

Audible quality is not subjectively certified. Source tracing confirms the
unchanged engine upload is `alBufferData(... AL_FORMAT_MONO16, lpData, length,
22050)` and menu passes m_Data/m_Length/m_Frequency unchanged. Binary comparisons
validate those input buffers/lengths; the AL call itself was not intercepted.
Score/collection/trophy overlays were loaded and compared as pixels but not all
triggered visually during bounded hunts. Native Windows runtime, every gameplay
transition, and exhaustive mod compatibility remain unverified.

Evidence is outside the repository at `/tmp/carnivores-media-validation/`:
`evidence/{corpus,available-corpus,distinct-corpus,hash-comparison,comparison-summary,
test-summary}.json`, per-preset build/test logs, baseline UI log, sanitizer log,
source audit, eight probe transcripts and runtime/menu captures. This temporary
location is not a committed or permanent asset archive.

## Final persistence audit and phase recommendation

Repeated direct-read searches cover engine/menu production sources, byte buffers
subsequently interpreted as words, and all active model/resource/map/profile
adapters. Remaining reads classify as:

| Category | Remaining sites |
| --- | --- |
| Explicit fixed-width codecs | LegacyProfile, LegacyModel, LegacyResource, LegacyMap, LegacyImage and LegacyAudio; all staging arrays precede value decoding |
| Intentional byte/text | MAP byte planes, RSC SkyMap, ignored CAR sound names, reserved/name fields, BGR triples, WAV marker bytes, menu TGA staging bytes and hit-map, shader/config/resource text |
| Output-only | Native Windows BMP screenshot headers/writes; GDI image descriptors and GPU buffers are runtime/platform interfaces |
| Deferred networking | WinSock wire helpers and packet transport, separate from persistent asset loading |
| Inactive/historical | `_iceage` profile/options and registration branches, commented billboard reads, unsupported legacy variants |

No additional active C2 gameplay/menu asset native-layout read was found. Packed
TARGAINFOHEADER layout assertions remain historical regression oracles, not the
disk contract. Windows scalar/runtime audio types can remain in adapters because
serialized values are decoded before assignment; replacing those platform APIs
is future work.

**The active asset-loading path required for normal Carnivores 2 / MEE gameplay
and menu operation no longer depends on native host representation for persistent
multibyte values.** The repository is ready to leave this serialization phase and
begin the planned thin platform, filesystem and Linux work, with existing legacy
formats preserved. This is a statement about input boundaries, not certification
of a big-endian engine, native Linux runtime, screenshot output or network protocol.
