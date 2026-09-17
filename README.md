# Carnivores 2 - Modder's Engine v1.11 (Modernized v1.1.8)

> **Original release:** 2000 (Action Forms)
> **Base:** Carnivores 2 Modder's Engine v1.11 (community revival)
> **Project release:** Modernized v1.1.8 — GitHub `v1.1.8-modernized`
> **ModDB download label:** V5
> **Versioning:** upstream MEE v1.11 is the base; modernization releases are numbered separately
> **Modernization:** OpenGL 3.3, OpenAL audio, memory arena, full codebase restructuring

Native Linux x86_64 OpenGL bring-up is available on the stacked port branch;
see [Linux build instructions and validation limits](docs/LINUX.md). The SDL3
Windows base still awaits native interactive acceptance.

## Release Numbering

| ModDB release | GitHub tag |
|---------------|------------|
| V1 | `v1.1.4-modernized` |
| V2 | `v1.1.5-modernized` |
| V3 | `v1.1.6-modernized` |
| V4 | `v1.1.7-modernized` |
| **V5** | **`v1.1.8-modernized`** |

The project title retains **Modder's Engine v1.11** to identify the upstream
base. **Modernized v1.1.8** identifies this fork's release; **V5** is only
its ModDB download label, not an engine version. This identifies the base,
not a guarantee that every mod or newer upstream feature is supported.
Windows executable version resources identify the project release as `1.1.8.0`.
These labels do not change save formats or the upstream engine version.
See [CHANGELOG.md](CHANGELOG.md) for changes since Modernized v1.1.7.

## Overview

This is a comprehensive modernization of Carnivores 2 Modder's Engine, featuring:
- **Native OpenGL 3.3 renderer** - no Glide/D3D wrappers needed
- **External shader files** - `shaders/*.vert/.frag` editable at runtime
- **Modern audio** - OpenAL with EFX reverb via DLL loader
- **Memory safety** - arena allocators, smart pointers, leak detection
- **Clean architecture** - restructured from monolithic files into modular directories
- **Standalone menu** - dedicated `Carnivores2Menu.exe` for settings
- **Expanded gameplay** - new creatures, survival mode, night vision, fog system

## Upstream Credits

This modernization builds on **Carnivores 2 Modder's Edition Engine** by
**[Ornithomimid1 (Oli)](https://github.com/Ornithomimid1)** and the upstream
community. Please credit and visit the original project:

**[Carnivores-CPE — Map-Amb-Demo-2](https://github.com/carnivores-cpe/Carnivores-CPE/tree/Map-Amb-Demo-2)**

The inherited engine and modding foundation are their work; this repository
adds the modernization described above. The linked branch continues to evolve
independently and does not imply that all its latest changes are included here.
See [NOTICE.md](NOTICE.md) for the exact base snapshot, standalone-menu
heritage, and third-party credits. The original Carnivores 2 game is by
**Action Forms**.

## Quick Start

### Prerequisites
- CMake 3.21+ (presets)
- Visual Studio 2019/2022 (or MSVC build tools)
- Python 3.8+

### Building
Run the existing presets below from an **x86 Native Tools Command Prompt**.
For explicit Windows x86/x64 OpenGL Debug/Release builds and validation status,
see [Windows OpenGL builds](docs/WINDOWS_BUILDS.md). The software renderer remains
Windows x86-only.

```bash
# OpenGL release build
cmake -S . -B build/ogl-release --preset ogl-release
cmake --build build/ogl-release --config Release

# Other presets: soft-release, ogl-debug, soft-debug, menu-release, ogl-release-shipping, ogl-pgo-instr/opt
```

The executables will be at:
- `build/ogl-release/bin/Carnivores1_GL.exe` (game engine)
- `build/ogl-release/bin/Carnivores2Menu.exe` (standalone menu)
- `build/ogl-release/bin/shaders/` (GLSL shaders, copied automatically)

> **Release packaging note:** the standalone menu launches the renderer engines
> by their classic MEE names. For a playable release, ship the menu exe as
> `Carnivores2Menu.exe` and copy the built game engine to `v_gl.ren`
> (OpenGL) or `v_soft.ren` (software) in the same folder - the menu resolves
> them from the exe directory or PATH. The `.ren` files are the actual game
> executables under a legacy extension.

### Data Files
Place your Carnivores 2 `HUNTDAT` directory next to the executable, or configure the path in `config.cfg`. The game reads all assets via relative paths at runtime; no original game data is bundled with or distributed by this project.

## Project Structure

```
Hunt/                         ← headers + subdirectories
├── Hunt.h                    ← God header (core structs, globals, declarations)
├── Audio/                    ← OpenAL audio via DLL loader
├── Core/                     ← GameTypes, GameState, EngineAPI
├── Debug/                    ← Logging
├── Game/                     ← All game logic
│   ├── Hunt.cpp              ← WinMain, game loop, drawing
│   ├── Config.cpp            ← config.cfg loading
│   ├── Characters.cpp        ← Character lifecycle
│   ├── CharacterAI.cpp       ← AI behavior
│   ├── CharacterAnimation/   ← Per-dino animation functions (18 files)
│   ├── CharacterCollision.cpp← Placement and collision checks
│   ├── CharacterMovement.cpp ← Movement functions
│   ├── CharacterSpawn.cpp    ← Spawning logic
│   ├── CharacterTargeting.cpp← Target placement
│   ├── CharacterTracking.cpp ← Character tracking
│   ├── Controls.cpp          ← Input handling
│   ├── Effects.cpp           ← Visual effects
│   ├── EngineInit.cpp        ← Engine initialization
│   ├── Interface.cpp         ← Menu/HUD interface
│   ├── PlayerMovement.cpp    ← Player movement
│   ├── Projectiles.cpp       ← Bullets and projectiles
│   ├── Ships.cpp             ← Ship animations
│   ├── Trophy.cpp            ← Trophy display
│   └── ... (TerrainQueries, Ship, etc.)
├── Loaders/                  ← Resource loaders
├── Math/                     ← Vector math, tracing, lighting
├── Network/                  ← Multiplayer support
└── Renderer/                 ← All renderers
    ├── GLRenderer.cpp/h      ← OpenGL 3.3 renderer
    ├── GLShader.cpp/h        ← GLSL shader manager + uniform caching
    ├── GLHUD.cpp             ← HUD overlay
    ├── GLModel.cpp           ← 3D model rendering
    ├── GLNight.cpp           ← Night vision post-processing
    ├── GLPerf.cpp/h          ← Performance metrics
    ├── GLShadow.cpp          ← Shadow rendering
    ├── GLSky.cpp             ← Sky rendering
    ├── GLTerrain.cpp         ← Terrain rendering
    ├── GLUtils.cpp/h         ← GL utilities
    ├── GLWater.cpp           ← Water surface rendering
    ├── GLUI.cpp              ← Legacy-to-GL bridge
    └── renderasm.cpp         ← x86 assembly helpers

shaders/                      ← External GLSL shader files
├── terrain.vert/frag         ← Terrain shaders
├── model.vert/frag           ← Model shaders
├── instanced_model.vert/frag ← Instanced model shaders
├── ui.vert/frag              ← UI/HUD shaders
├── night_desat.vert/frag     ← Night vision desaturation
└── sky.vert/frag             ← Sky sphere shaders

Menu/                         ← Standalone settings menu (Carnivores2Menu.exe)
```

## Refactoring History

The codebase was originally a single `Hunt/` directory with 13 monolithic files. Major restructuring:

| Task | Original | After |
|:-----|:---------|:------|
| Character animation | 1 file, 6,360 lines | 18 per-dino files + dispatcher |
| Characters.cpp | 8,303 lines | 584 lines (+ 3 extracted files) |
| Game.cpp | 3,080 lines | 164 lines (+ 4 extracted files) |
| GL shaders | Inline C++ strings | External .vert/.frag files |
| Old renderers | Hunt/ root | Hunt/Renderer/ |
| GL files | Hunt/ root | Hunt/Renderer/ |

## Documentation

- [Changelog](CHANGELOG.md) - user-facing release history
- [`NOTICE.md`](NOTICE.md) - copyrights, upstream MEE heritage, third-party notices

The full architecture and development documentation corpus lives in a
separate (private) workspace repository and is not part of this public
source release.

## License

Carnivores 2 © 2000 Action Forms. Modder's Engine v1.11 © community maintainers
(see `NOTICE.md`). The modernization source in this repository is © 2026
Tibor Harsányi (StriderTibe), licensed under the MIT License (see `LICENSE`).
No original game assets are included.
