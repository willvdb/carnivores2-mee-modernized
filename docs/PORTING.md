# Carnivores 2 MEE Modernized — Cross-Platform Porting Plan

This document defines the constraints and sequencing for the x64 / Linux portability effort. These requirements are intentionally conservative: the goal is to modernize the engine around the existing Carnivores 2 / MEE content ecosystem, not to force existing mods onto new formats.

## Project goals

1. Preserve compatibility with existing Carnivores 2 and MEE content wherever practical.
2. Preserve legacy on-disk formats such as `.CAR`, `.3DF`, maps, save/profile data, resource/config text, and existing HUNTDAT layouts.
3. Add a native Windows x64 OpenGL build.
4. Add a native Linux x64 OpenGL build.
5. Keep the existing OpenGL renderer as the portable renderer.
6. Use OpenAL as the portable audio backend.
7. Introduce a thin cross-platform platform layer for windowing, input, timing, and OpenGL context creation; SDL3 is the current preferred candidate.
8. Keep the existing Win32 x86 OpenGL build working as a regression reference during the transition.
9. Do not redistribute original Carnivores 2 game assets. Users supply their own HUNTDAT data.
10. Add quality-of-life improvements only after the compatibility-focused port is stable, and do so without requiring legacy mods to convert their data.

## Core compatibility rule

**Runtime representation is not disk representation.**

The 64-bit engine may use native 64-bit pointers, `size_t`, modern C++ ownership, and platform-neutral internal types. Legacy files and network records must remain byte-compatible with their established formats unless a new format is deliberately introduced as an optional extension.

Do not use `sizeof(runtime_struct)` as a file-format contract unless the format has been explicitly proven to match that runtime layout across all supported platforms.

Prefer fixed-width serialized types (`std::uint8_t`, `std::uint16_t`, `std::uint32_t`, `std::int32_t`, IEEE-754 32-bit float) and explicit parsing/writing.

## Mod compatibility

The intended user experience is that ordinary content mods continue to work without conversion.

Expected to remain compatible where possible:

- HUNTDAT content replacements and additions
- maps and `.c2map` content
- `.CAR` / `.3DF` models and animations
- textures and audio
- weapons, dinosaurs, resource text, and menu/config extensions already understood by MEE Modernized
- total conversions whose custom behavior is data-driven

Not expected to work unchanged:

- replacement `.ren` / executable builds containing custom engine code
- DLL injection or process-memory patches
- hard-coded x86 addresses
- custom inline x86 assembly
- modifications that depend directly on the legacy software renderer or DirectDraw internals

For Linux compatibility, the engine should provide legacy-friendly path behavior where practical, including normalization of `\` and `/` and case-insensitive resolution of legacy game assets so Windows-authored mods are not broken merely by filename casing.

## Initial support matrix

| Target | Renderer | Status / intent |
| --- | --- | --- |
| Windows x86 | OpenGL | Preserve as reference / compatibility target |
| Windows x64 | OpenGL | First new runtime target |
| Linux x64 | OpenGL | Primary cross-platform target |
| Windows x86 | Software renderer | Preserve as legacy target where practical |
| Windows x64 | Software renderer | Not required for initial port |
| Linux x64 | Software renderer | Not required for initial port |
| macOS / ARM64 | OpenGL or future backend | Later portability validation, not an initial milestone |

The x86 software renderer's inline assembly must not block x64/Linux work. It may remain isolated to supported legacy configurations.

## Development sequence

### Phase 0 — Baseline and compatibility inventory

- Freeze a known-good upstream revision.
- Keep current Windows x86 OpenGL CI/tests green.
- Identify representative vanilla/MEE/mod configurations for compatibility testing.
- Audit all 32-bit assumptions before making broad changes.

### Phase 1 — Windows x64 OpenGL

- Add a Windows x64 OpenGL build target.
- Exclude the x86 software renderer from x64 builds.
- Fix pointer-width and allocation-size assumptions instead of suppressing warnings.
- Separate runtime structure sizes from serialized structure sizes.
- Preserve x86 OpenGL behavior.

**Exit criterion:** native Windows x64 executable can load existing HUNTDAT and enter a hunt using unmodified compatible assets.

### Phase 2 — Serialization hardening

Audit and test all architecture-sensitive boundaries:

- `.CAR` / `.3DF`
- maps and animations
- save/profile/trophy data
- config/resource files
- multiplayer packets

Replace architecture-dependent serialized types such as `long`, `DWORD`, or raw runtime struct dumps with explicit fixed-width representations where required while preserving established bytes on disk/wire.

### Phase 3 — Platform abstraction

Introduce a small platform layer for:

- application entry / event loop
- window creation and display modes
- OpenGL context and proc loading
- keyboard/mouse/controller input
- relative mouse capture
- high-resolution timing and sleep
- focus and fullscreen behavior

Avoid spreading platform `#ifdef`s throughout gameplay code.

### Phase 4 — Portable filesystem

- Replace Win32-only file APIs at engine boundaries.
- Use normalized portable paths internally.
- Support legacy Windows-style separators.
- Add case-insensitive legacy asset resolution on case-sensitive filesystems.
- Keep original mod directory layouts valid.

### Phase 5 — Linux x64 bring-up

- Add GCC and Clang builds.
- Run parser/unit tests on Linux CI.
- Bring up SDL window + OpenGL context.
- Load existing HUNTDAT.
- Enter and play a hunt.
- Test under SDL's Linux display backends rather than writing separate X11/Wayland implementations.

### Phase 6 — Portable audio

- Replace Windows-specific OpenAL DLL loading with a platform-neutral OpenAL integration.
- Keep the existing OpenAL gameplay/audio logic where possible.

### Phase 7 — Input/display polish

Once the port is stable, add modern QoL features such as:

- arbitrary modern resolutions
- windowed / borderless / exclusive fullscreen modes
- high-DPI support
- widescreen / ultrawide correctness
- FOV and HUD scaling
- VSync and FPS caps
- modern relative mouse input and sensitivity
- controller support
- improved focus / Alt-Tab behavior

These features must not require legacy content conversion.

### Phase 8 — Networking

Port WinSock and Win32 thread dependencies after single-player Linux is stable. Preserve network packet formats where practical and add explicit cross-platform serialization tests.

### Phase 9 — Launcher / menu

Port or replace the Windows GDI menu only after the engine itself is healthy. Preserve the Carnivores visual identity while allowing modern display options, mod selection, per-mod profiles/configs, and better diagnostics.

## Engineering rules for agent-assisted changes

1. Read this document before making portability changes.
2. Prefer small, reviewable PRs over repo-wide rewrites.
3. Each change should state whether it affects runtime layout, disk format, network format, GPU format, or only platform code.
4. Do not silently change established binary formats.
5. Do not remove legacy behavior simply because it looks unusual; first determine whether existing mods rely on it.
6. Keep compatibility work and quality-of-life work separate whenever possible.
7. Add tests before or alongside risky serialization/layout changes.
8. Fix warnings that indicate real portability problems; do not paper over pointer truncation or width mismatches with casts.
9. Use fixed-width integer types for persistent/wire formats.
10. Keep upstreamability in mind: changes should be understandable and useful outside this fork.

## First audit deliverable

The first engineering task is a repository-wide x64 portability audit. It should identify every known or suspected assumption that the process is 32-bit and classify each occurrence as one of:

- runtime memory layout
- disk/file-format layout
- network protocol layout
- renderer/GPU layout
- Win32 platform/API dependency
- software-renderer/x86 assembly
- third-party/dependency assumption
- unknown / requires investigation

For each item, record:

- file and symbol
- current assumption
- whether it is part of an external compatibility contract
- likely x64 impact
- proposed remediation
- confidence / open questions

The audit should not change gameplay behavior. Its output should become `docs/X64_AUDIT.md` and serve as the implementation roadmap for Phase 1.