# Agent guidance

Before making portability, build-system, serialization, renderer, filesystem, input, audio, or networking changes, read `docs/PORTING.md` in full.

## Binding constraints

- Preserve compatibility with existing Carnivores 2 / MEE content wherever practical.
- Do not change legacy disk or network formats merely to make runtime structures convenient on x64.
- Treat runtime representation and serialized representation as separate concerns.
- Do not require legacy content mods to be converted to new formats for the initial port.
- Keep the existing Windows x86 OpenGL build usable as a regression reference during the transition.
- The portable engine target is OpenGL. The x86 software renderer and its inline assembly may remain legacy-only and must not block x64/Linux work.
- Prefer fixed-width integer types for file/network formats.
- Do not hide pointer truncation or size mismatches behind casts or warning suppression.
- Prefer small, reviewable, behavior-preserving changes.
- Add tests around architecture-sensitive parsing/serialization before changing established behavior.
- Do not redistribute or commit original Carnivores 2 game assets or HUNTDAT.
- Keep changes suitable for possible upstream contribution to `Tibbee/carnivores2-mee-modernized`.

## Current task order

1. Repository-wide x64 assumption audit (`docs/X64_AUDIT.md`).
2. Windows x64 OpenGL build while preserving Windows x86 OpenGL.
3. Serialization/layout hardening.
4. Thin platform abstraction, likely using SDL3.
5. Portable filesystem/path compatibility.
6. Linux x64 bring-up.
7. Portable OpenAL integration.
8. Input/display QoL.
9. Networking portability.
10. Cross-platform launcher/menu.

Do not skip directly to SDL/Linux before the x64/layout audit unless explicitly instructed.