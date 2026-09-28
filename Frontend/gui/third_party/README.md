# Offline dependency provenance (GUI)

Only the optional GUI (`C2_FRONTEND_GUI=ON`) compiles anything in this
directory. The CLI, engine and Windows menu never include it.

## stb_image

* Upstream: https://github.com/nothings/stb
* Exact commit: `f58f558c120e9b32c217290b80bad1a0729fbb2c` (stb_image v2.30)
* File copied verbatim: `stb/stb_image.h`
* SHA-256 of header:
  `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3`
* License: dual MIT / public domain (Unlicense), text at the end of the
  header. This project uses it under the MIT alternative.
* No local changes. The GUI defines `STBI_ONLY_PNG` and `STBI_NO_STDIO` in
  its single implementation unit, so only the PNG decoder is compiled and the
  bytes are supplied by the caller; images are never opened by path from
  within the decoder.

Why a vendored decoder: RmlUi's OpenGL 3 sample renderer only decodes
uncompressed TGA. The artwork contract asks for PNG with alpha. The decoder is
used exclusively for GUI artwork shipped with the frontend or supplied through
the documented asset root; it never reads game content.

## Fetched at configure time (not vendored)

Recorded in `../cmake/Dependencies.cmake` with SHA-256 pins:

| Component | Version | Source | License |
| --- | --- | --- | --- |
| RmlUi | 6.3 (`ba95ffe8bfb6370efb2cdcca927eaad4710c5413`) | GitHub release archive | MIT |
| SDL3 | 3.2.28 (same archive and checksum as the engine) | libsdl.org | zlib |
| FreeType | system package | not fetched | FTL / GPLv2 |
