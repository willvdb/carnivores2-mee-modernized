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
