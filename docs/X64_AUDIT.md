# x64 portability audit

Audit date: 2026-09-16. Audited revision: `27fb932fbe2dc72278b1da14b636b98d387ab806`
(`port/x64-audit`); engine/build sources are identical to upstream baseline
`ca6aebf`. Requirements: [AGENTS.md](../AGENTS.md), [PORTING.md](PORTING.md),
and [X64_AUDIT_TASK.md](X64_AUDIT_TASK.md).

This is an analysis/documentation deliverable. No engine, build, dependency,
format, or gameplay changes were made. Proposed changes below are future work.

## 1. Executive summary

Windows x64 OpenGL is blocked by explicit x86 runtime-size assertions and has a
confirmed window-procedure ABI defect hidden by a function-pointer cast. The
allocation wrapper also restricts native allocation sizes to `DWORD`, and its
checked-multiplication helper is not safe for arbitrary 64-bit `size_t` inputs.
These are the first implementation priorities, together with an explicit x86/x64
build matrix and compatibility tests.

The inspected binary content does **not** require widening its records. Model
faces, vertices, object records, animation samples, maps, trophies, and profile
options have fixed byte contracts. Pointer-owning `TModel`, `TAni`, `TPicture`,
`TSFX`, and `TCharacterInfo` are constructed in memory; the loaders do not dump
those entire runtime objects to disk. Some assertions describe a supposed save
contract for gameplay types for which no corresponding serialization was found.
Do not preserve a four-byte runtime pointer on the strength of those comments.

The software renderer's assembly is already guarded by `_soft`, including the
entire `renderasm.cpp` translation unit. OpenGL does not need an assembly rewrite.
GPU vertex sizes and the 240-byte frame uniform buffer are intentional interfaces
with the shaders; the uniform-block layout needs driver validation (G04). WGL, GDI overlays/text, Win32 input, file access, memory backing,
audio loading/threading, and WinSock are separate Linux portability boundaries.

An x64 OpenAL runtime must match the engine process. Existing x86 audio DLLs
cannot be loaded into it. OpenAL load failure currently disables sound rather
than aborting boot, so missing audio is a functional/package blocker, not by
itself a demonstrated first-frame crash.

### Scope and evidence

- Searched tracked engine, menu, tests, build/CI, scripts, shaders, and vendored
  dependency sources. Reviewed matches for size assertions, casts, Win32 types,
  allocation/I/O, packing, callbacks, assembly, paths, compiler extensions, and
  dynamic libraries, then traced the relevant definitions and consumers.
- Findings group repeated occurrences sharing a contract; locations identify
  the affected files and symbols. Commented code is distinguished from active
  code. This is a source inventory, not a claim that every possible runtime
  defect has been proved absent.
- The audit host is Linux x64 with `g++`. `cmake`, MSVC, Clang, MinGW cross
  compilers, and Wine were not found on PATH. No full engine build, repository
  test suite, Windows ABI execution, gameplay capture, or asset smoke test was
  run. No original assets were added or used as fixtures.
- A temporary C++17 GCC probe compiled selected record definitions extracted
  from these sources. Windows scalar aliases were represented by their fixed
  widths and the runtime smart pointers by `std::unique_ptr` with empty deleters.
  It checked the sizes reported below and executed the unchanged
  `CheckedBytes2` body. This is evidence about those definitions and arithmetic,
  **not** an MSVC/STL build or a portable engine build. Probe files were outside
  the repository and removed.

### Classification and priority

Each finding has one primary category. Cross-references describe secondary
effects. Abbreviations in the tables map to the task's categories exactly:

| Code | Category |
| --- | --- |
| RT | runtime memory layout |
| DISK | disk/file-format layout |
| NET | network protocol layout |
| GPU | renderer/GPU layout |
| WIN | Win32 platform/API dependency |
| SOFT | software-renderer/x86 assembly |
| DEP | third-party/dependency assumption |
| OPEN | unknown / requires investigation |

P0 means a confirmed x64 build blocker or fundamental callback ABI defect;
P1 means compatibility, arithmetic, or validation work needed before declaring
Windows x64 supported; P2 means Linux/platform work or bounded hardening that
need not delay a controlled Windows x64 first boot. “Contract” describes an
external disk, wire, OS, DLL, shader, or user-visible interface, not merely an
internal struct layout. Confidence distinguishes source facts from untested
reachability or ABI predictions.

## 2. Build/toolchain assumptions

| ID / category / priority | File and symbol; current assumption | External contract | Windows x64 impact | Linux x64 impact | Proposed remediation; confidence / open questions |
| --- | --- | --- | --- | --- | --- |
| B01 / SOFT / P0 | [CMakeLists.txt](../CMakeLists.txt), `MSVC` pointer-size warning, `RENDERER`, `CORE_FILES`: says all MSVC builds “MUST” be x86; only warns. `renderasm.cpp` is in core, though internally guarded. | Build support matrix; no content bytes. | Misleading for GL; SOFT can still be selected in an unsupported configuration. The warning itself does not prevent GL compilation. | Windows libraries/resources and headers remain even for GL. | Make Windows x86/x64 GL explicit; reject unsupported SOFT compiler/architecture combinations and select its assembly only there. Preserve x86 SOFT. **High**; no configure run here. |
| B02 / DEP / P1 | [CMakePresets.json](../CMakePresets.json), `base`, `ogl-*`, `soft-*`; [.github/workflows/build.yml](../.github/workflows/build.yml), `build`: Ninja presets inherit whatever compiler/architecture the environment supplies; CI uses `windows-latest` without explicit MSVC environment/architecture setup. | Reproducible reference build and release artifacts. | No reliable x86 reference versus x64 distinction; same preset paths can retain incompatible caches. No explicit x64 job. | No Linux job/toolchain coverage. | Separate binary directories and architecture-specific compiler environments/jobs; verify compiler and pointer width. Keep x86 GL checks. **High** source evidence; current CI success is unverified. |
| B03 / DEP / P2 | [CMakeLists.txt](../CMakeLists.txt), optimization flags and test block; `ogl-pgo-*` presets: `/MP /permissive`, `/GL /LTCG /arch:SSE2 /fp:fast`, MSVC CRT choices, and MSVC PGO switches. Tests exist only under `if(MSVC)`. | Compiler/build policy, not serialized layout. | `/arch:SSE2` is an x86-specific tuning choice; review supported x64 flags. `/permissive` can obscure standards issues. | GCC cannot use the MSVC switches in PGO presets; no native tests are configured. | Scope flags by compiler/architecture, check IPO support, expose portable tests independently. **High**; exact compiler diagnostics require the target compiler. |
| B04 / WIN / P2 | [CMakeLists.txt](../CMakeLists.txt), `add_executable(... WIN32 ...)`, `AppRes.rc`, manifest, linked libraries; [Menu/CMakeLists.txt](../Menu/CMakeLists.txt), unconditional menu target. Engine links `kernel32 user32 gdi32 winmm ddraw opengl32`; WinSock links also come from `#pragma comment` in `Hunt.h` and `Game/Config.cpp`. | Windows subsystem/resource/link interfaces. | Windows SDK libraries have native x64 counterparts; these names are not proof of x86 binaries. | Unconditional Windows targets, resources and libraries block build; menu is built even when only engine GL is wanted. | Scope platform dependencies, make the menu optional for Linux engine bring-up, and express required platform links in CMake. **High**. |
| B05 / DEP / P1 | [tests/smoke_test.ps1](../tests/smoke_test.ps1), defaults and process termination; CMake `SmokeTest`; CI smoke/unit steps; `tools/c2_capture_*.ps1`. Smoke defaults to `E:\Games\CarnivoresLegacy` and GL `v_gl.exe`, whereas packaging uses `v_gl.ren`; it can kill after a timeout. | Test deployment/working directory and supplied assets. | A passing timed launch does not prove the new x64 artifact entered a hunt, saved compatibly, or exited cleanly. CTest's default may test a deployed binary instead of the build artifact. | PowerShell/Windows capture and window automation are not native gameplay validation. | Pass explicit executable, working directory, architecture and user-owned asset path; require entered-hunt evidence plus graceful shutdown and save verification. Keep proprietary assets out of CI/repository. **High**. |

The current test suite covers arithmetic, arena behavior, key bindings, fog,
projectiles, trophy-mode logic, UI layout, and other helpers. It does not establish
cross-architecture `.CAR`/`.3DF` parsing, save/profile byte equivalence, or wire
equivalence. `test_load_validate.cpp` tests helpers and a Win32 short read, not a
complete model/save round trip. `test_memory_arena.cpp` includes the x86 assertion
through `Memory.h`. Fog/UI tests include Windows-coupled production headers.

## 3. Runtime layout and pointer-width assumptions

| ID / category / priority | File and symbol; current assumption | External contract | Windows x64 impact | Linux x64 impact | Proposed remediation; confidence / open questions |
| --- | --- | --- | --- | --- | --- |
| R01 / RT / P0 | [Hunt/Memory.h](../Hunt/Memory.h):309, `static_assert(sizeof(void*) == 4)`. | None; process implementation only. | Guaranteed compile failure. | Same failure after header dependencies are addressed. | Remove the process-width restriction for supported GL targets; retain meaningful smart-pointer ownership/size properties. **High**. |
| R02 / RT / P0 | [Hunt/Hunt.h](../Hunt/Hunt.h):65–98, `TAni`, `TVTL`, `TPicture`, `TBMPModel`, `TModel` exact x86 sizes; `TObject` and `TCharacterInfo` range checks. | No whole-object disk/wire dump found; underlying records are D01–D03. | Exact assertions fail as pointers/alignment grow; range checks need separate evaluation, not automatic removal. | Same pointer growth, with different STL/debug layouts possible. | Separate runtime ownership/layout checks from disk checks; use native object allocation and avoid packed/four-byte pointers. **High** failures for exact pointer-bearing types; exact MSVC debug sizes unmeasured. |
| R03 / RT / P0 | [Hunt/Core/GameTypes.h](../Hunt/Core/GameTypes.h):693,697,711,721, `TCharacter == 344`, `TPack == 8`, `TLevelDef == 200`, release `TWeapon == 88248`. | No active raw serialization found for these types, despite some assertion messages. | Pointer-bearing objects grow; unconditional first three and release weapon check block compile. | Same; `BOOL` must retain deliberate semantics when removed. | Reclassify these as runtime types; keep disk records separate. Audit callers before altering assertions. **High** on members and I/O search; no proprietary external memory-patching compatibility promised. |
| R04 / RT / P1 | [Hunt/Memory.h](../Hunt/Memory.h), `make_heap_object`, `make_heap_array`, `_HeapAlloc`, `_HeapAllocImpl`; [Core/EngineAPI.h](../Hunt/Core/EngineAPI.h):269,281; [Loaders/Resources.cpp](../Hunt/Loaders/Resources.cpp), `AllocDispatch` and all overloads: allocation byte argument is `DWORD`, unlike native `HeapAlloc`'s size parameter. | None for runtime sizes; API flags remain Windows `DWORD`. | `sizeof`/`size_t` calls narrow to 32 bits. Array factory explicitly rejects over `MAXDWORD`, so that path is bounded rather than silently truncated. | Wrapper is both Windows-only and width-limited. | Carry `size_t` through declarations, definitions, debug forwarding, factories and statistics; validate before multiplying. Keep file/API transfer limits separate. **High**; existing bounded loaders do not prove a current normal-content overflow. |
| R05 / RT / P1 | [Hunt/Loaders/LoadValidate.h](../Hunt/Loaders/LoadValidate.h):88, `CheckedBytes2/3`: multiplies two `size_t` values in `unsigned long long` before checking against `0xFFFFFFFF`. | Current accepted byte-count limit is loader policy, not permission to widen on-disk fields. | The “wide” intermediate is no wider than x64 `size_t`; sufficiently large operands wrap before the check. | Same. | Check using division before multiplication against the intended limit; distinguish native allocation overflow from legacy/I/O bounds. Add 64-bit boundary cases first. **High**: exact-body probe accepted `(size_t(1)<<63, 2)` with output zero. Current model/animation/dimension/count checks restrict real callers; this does not demonstrate an exploitable asset. |
| R06 / RT / P1 | [Hunt/Loaders/ModelLoader.cpp](../Hunt/Loaders/ModelLoader.cpp), `AllocateMemoryForModel`, `CorrectModel`, mipmaps/animation; [CharacterLoader.cpp](../Hunt/Loaders/CharacterLoader.cpp), scratch arrays; [ScriptParser.cpp](../Hunt/Loaders/ScriptParser.cpp), `ReadAreaTable`; picture loaders: mixed checked byte products, signed counts, `sizeof` products and `DWORD` casts. | Element counts and file byte widths remain D01–D05; runtime buffer sizes are internal. | Wrapper widening alone does not widen an earlier signed multiplication or validate narrowing. Model counts are capped at `1<<20`; snow total is also capped at `1<<20`. | Same arithmetic constraints after OS removal. | Review every allocation family when changing the interface; use checked native products for storage and separately bounded transfer lengths. Preserve established content caps initially. **High** on sites/caps; no blanket claim that all casts currently overflow. |
| R07 / RT / P2 | [Hunt/Core/GameState.h](../Hunt/Core/GameState.h):376, `HeapAllocated/HeapReleased`; [Memory.h](../Hunt/Memory.h), arena diagnostics; [Resources.cpp](../Hunt/Loaders/Resources.cpp), `PrintMemoryLeaks`: totals are `DWORD` or printed after `(unsigned)` casts. | Diagnostic output only. | Aggregate byte counters/logs can wrap/truncate even when allocations individually fit 32 bits. | Same unless types/formatting are changed deliberately. | Use suitable native/cumulative counters and `%zu`/fixed-width formatting; pointer logging already uses `%p`. **High**. |
| R08 / RT / P2 | [Hunt/Memory.h](../Hunt/Memory.h), `MemoryArena::Allocate/Contains`, smart-pointer equality checks; [EngineInit.cpp](../Hunt/Game/EngineInit.cpp):516; [GLModel.cpp](../Hunt/Renderer/GLModel.cpp):432; [Audio_DLL.cpp](../Hunt/Audio/Audio_DLL.cpp), `bufferCache`. | None; internal address identity. | Reviewed arena arithmetic uses `size_t` and `uintptr_t`; GL sorting uses `uintptr_t`; caches retain actual pointer keys. No 32-bit truncation found at these sites. | Address representation is already appropriate; backing allocation and synchronization still need portability work. | Preserve these designs; keep equality-to-`sizeof(void*)` checks where intentional and validate real STL builds. Do not replace pointer keys with `DWORD`. **High** positive finding. |
| R09 / RT / P2 | [Hunt/Game/EngineInit.cpp](../Hunt/Game/EngineInit.cpp):1022, `LoadConfig` GPU mask; [GLShader.cpp](../Hunt/Renderer/GLShader.cpp):30, `ReadTextFile`; [GLSky.cpp](../Hunt/Renderer/GLSky.cpp):699, diagnostic accumulators. | GPU mask text is a 32-bit setting; shader text and diagnostics are not struct dumps. | Windows `long` stays 32 bits; shader `ftell` retains its signed-long limit. | LP64 `long` grows to 64 bits; `strtoul` can accept values that then truncate to `uint32_t`. Shader size range grows; bounded sky sums do not need 64-bit disk fields. | Range-check mask parsing against `UINT32_MAX`, preserve documented behavior, and use deliberate stream-size conversion. No blanket replacement of every `long`. **High** source facts; invalid-mask policy needs a test. |

### Assertion disposition and measured layout

The following values are **selected native GCC x64 probe results**, not a table
of certified MSVC layouts. Runtime smart-pointer aliases used empty deleters;
STL debug overhead can change containing types.

| Runtime type | Current x86 assertion | Probe size | Disposition |
| --- | ---: | ---: | --- |
| `TAni` | 48 | 56 | Runtime pointer; exact x86 check blocks x64. |
| `TVTL` | 16 | 24 | Same. |
| `TPicture` | 12 | 16 | Same. |
| `TBMPModel` | 52 | 56 | Same. |
| `TModel` | 52 | 88 | Same; native allocation already uses `sizeof(TModel)`. |
| `TObject` | 300–400 | 344 | Probe still passes; not a serialized whole object. |
| `TCharacterInfo` | 3000–8000 | 5936 | Probe still passes; confirm MSVC debug/release independently. |
| `TSFX` | greater than 8 | 32 | Pointer-owning vector; current assertion does not demand x86. |
| `TCharacter` | 344 | 352 | `pinfo` is a pointer; no state-file dump found. |
| `TPack` | 8 | 16 | `leader` is a pointer. |
| `TLevelDef` | 200 | 208 | `lpMapImage` is a pointer. |
| `TWeapon` | 88248 (non-debug) | 119152 | Contains pointer-owning arrays; runtime only. |

The remaining fixed-size assertions in `Core/GameTypes.h` were also inventoried.
Actual disk contracts are `TTrophyRoom=1516`, `TTrophyItem=56`,
`TTrophyRoom2=7176`, `TTrophyItem2=56`, `TStats=16`, `TObjInfo=64`, and
`TWaterEntity=16` (see section 4). Preserve these byte layouts.

Pointer-free gameplay checks are `TBullet=96`, `TDinoInfo=13800`,
`TWeapInfo=468`, `TTrophyType=580`, `TDinoKill=32`, `TWind=20`,
`TElements=1072`, `TBloodP=20`, `TBTrail=10244`, `THitBox=32`, `TBag=32`,
`TShip=96`, `TDemoPoint=20`, `TSpawnGroup=1568`, `TSpawnInfo=8`,
`TSpawnRegion=16`, `TAIInfo=92`, `TPackType=532`, `TPackMember=8`,
`TPackMember2=8`, `TElement=32`, `TSnowType=28`, and `TSnowElement=20`.
They have no identified whole-object file/wire consumer in this checkout.
Their fixed sizes are not inherently x86-only: `int`, float, and Windows `BOOL`
remain 32 bits on Windows x64. Retain useful checks until there is a reason to
change them, and correct misleading serialization comments in a later PR.
`TSnowType::addr` is an array index, not a truncated machine address.

### Scalar and handle rules

| Type family | Audit treatment |
| --- | --- |
| `int`, `short`, float | Current file readers assume 4, 2, and 4 bytes respectively, with IEEE-754 floats and little-endian host representation. Assert or decode these explicitly at file boundaries. |
| `long` / `unsigned long` | Windows x64 uses 32 bits; Linux x64 LP64 uses 64 bits. Active protocol helpers are N02; other active uses are R09. The `long_data` experiment in `Hunt.cpp` is commented out. |
| `DWORD`, `WORD`, `BYTE`, `BOOL` | Windows widths remain 32, 16, 8, and 32 bits. They are valid Windows API types, but are not portable header dependencies. Do not map `DWORD` to Linux `unsigned long`, or serialized `BOOL` to C++ `bool`. |
| `HANDLE`, `HMODULE`, `HWND`, `HDC`, `HGLRC`, `LPVOID` | Native handles/pointers, not 32-bit quantities. Reviewed storage uses native types. Do not widen them through integer aliases or serialize them. |
| `WPARAM`, `LPARAM`, `LRESULT`, `DWORD_PTR`, `SOCKET` | Pointer-sized Windows ABI types. P01 is a real mismatch; menu callbacks and network socket members use the correct native types. |
| `uint32_t`, `GLuint`, `GLsizei`, `ALuint`, `ALsizei` | Fixed-width format/API quantities where used intentionally. Native pointer growth does not justify widening GL/AL API fields. Check conversions into them. |

## 4. File-format/serialization contracts

| ID / category / priority | File and symbol; current assumption | External contract | Windows x64 impact | Linux x64 impact | Proposed remediation; confidence / open questions |
| --- | --- | --- | --- | --- | --- |
| D01 / DISK / P1 | [Hunt/Loaders/ModelLoader.cpp](../Hunt/Loaders/ModelLoader.cpp), `LoadModel`, `LoadModelEx`, `LoadCharacterInfo`, `AllocateMemoryForModel`, `CorrectModel`, `fp_conv`; [Core/ModelTypes.h](../Hunt/Core/ModelTypes.h): faces 64 bytes, vertices 16, object records 48, header integers 4. Allocations and `FCount << 6` copy also encode these sizes. | **Yes**, `.3DF`, embedded `.rsc` models and `.CAR` model records. | The inspected pointer-free records retain sizes; changing `sizeof(TModel)` is not a file-format change. Raw face UV bytes initially hold integers even in the float-based GL type. | Sizes remain in the native probe, but raw parsing requires little-endian integers/IEEE floats and exact member offsets. | Lock serialized sizes/offsets and sample fixtures first; introduce explicit disk records/decoders, preserving integer UV conversion and reserved bytes. Never read faces as floats directly merely because GL runtime UVs are floats. **High** source evidence; full legacy/mod corpus untested. |
| D02 / DISK / P1 | Same loader, `LoadAnimation(TVTL&)`, `LoadCharacterInfo`: animation header fields are 4-byte values, names 32 bytes, XYZ samples three 16-bit integers (6 bytes/vertex/frame); optional `Anifx` tail is 64 × 4 bytes. | **Yes**, object animation and `.CAR` animation/sound associations. | Native `TAni/TVTL` growth is safe if only individual fields/sample storage are read as now. | Same record widths must be decoded independently of runtime pointers. | Fixtures must distinguish object stored-frame-count-plus-one behavior from `.CAR` one-frame duplication; preserve absent/short `Anifx` fallback to `-1`. **High** on current readers; validate peculiar frame convention with representative user-owned assets before changing it. |
| D03 / DISK / P1 | [Hunt/Loaders/Resources.cpp](../Hunt/Loaders/Resources.cpp), `LoadResources`: raw `TObjInfo` 64, `TFogEntity` via `sizeof` (20), `TRD[16]` via `sizeof` (256), water entries 16, `FadeRGB/TransRGB` native `int[3][3]` (36 each). Sound lengths/counts/volume fields are 4 bytes. | **Yes**, `.rsc` tables. | These records have no pointers and need no widening. `TFogEntity`/`TRD` contracts lack the disk assertions already present for object/water records. | Portable parsing must preserve widths, byte order, offsets and 16-bit `REnvir/Flags`. | Fixed-width disk definitions and offset/byte tests; do not serialize entire `TObject`, `TAmbient` or `TSFX` after refactoring. **High**. |
| D04 / DISK / P1 | [Hunt/Loaders/Resources.cpp](../Hunt/Loaders/Resources.cpp), `.map` portion of `LoadResources`; [Core/GameState.h](../Hunt/Core/GameState.h):105–125: bulk `sizeof(array)` reads and fixed light-map seeks. | **Yes**, legacy `.map` ordering and dimensions. | Stable while 1024² arrays and `WORD` widths remain fixed. | Replacing `WORD` incorrectly or changing runtime map dimensions silently changes read lengths. | Define file-plane sizes separately from runtime containers; preserve byte/16-bit planes and day/night selection. **High**; `MapLoader.cpp` processes these arrays but is not a new binary container parser. |
| D05 / DISK / P1 | [Hunt/Loaders/ModelLoader.cpp](../Hunt/Loaders/ModelLoader.cpp), `LoadTexture/LoadSky`; [PictureLoader.cpp](../Hunt/Loaders/PictureLoader.cpp), BMP/TGA readers; [Resources.cpp](../Hunt/Loaders/Resources.cpp), `SaveScreenShot`; [Menu/Targa.h](../Menu/Targa.h), `TARGAINFOHEADER`; [Menu/Resources.cpp](../Menu/Resources.cpp), `ReadTGAFile/LoadPicture`. | **Yes**, texture pixels, BMP/TGA headers, menu art. | Windows BMP headers are fixed records; TGA uses explicit 8/16-bit fields packed to 18 bytes. Pointers in pictures must grow normally. | Cannot replace Windows BMP structs with arbitrary native structs; packed native TGA reads still assume little endian. | Preserve 16-bit 555/565 pixel semantics, BMP header sizes/padding and TGA origin handling; add independent disk-header assertions/decoders. **High** layout intent; no complete image-format validation performed. |
| D06 / DISK / P1 | [Hunt/Loaders/SoundLoader.cpp](../Hunt/Loaders/SoundLoader.cpp), `LoadWav`; model/resource sound readers; [Menu/Resources.cpp](../Menu/Resources.cpp), `LoadWave`: 4-byte lengths and 16-bit PCM. Engine rounds sample storage up for odd byte counts; menu allocates `length / sizeof(int16_t)` and reads `length` bytes. | **Yes**, WAV/embedded sound bytes; runtime vector/pointer is not part of the file. | Normal fixed-width sound data survives. Menu's odd-length allocation can underallocate by one byte; pre-existing, not caused by x64. | Same parsing contract; endian/Win32 removal and menu validation needed. | Preserve sample rate/channel interpretation, enforce lengths and short reads; add odd/truncated length fixtures before sharing readers. **High** arithmetic/source evidence; unusual files not exercised. |
| D07 / DISK / P1 | [Hunt/Game/Trophy.cpp](../Hunt/Game/Trophy.cpp), `LoadTrophy/SaveTrophy`, `LoadTrophy2/SaveTrophy2`; [Core/GameTypes.h](../Hunt/Core/GameTypes.h), trophy structs: whole-object `ReadFile/WriteFile(sizeof(...))`. | **Yes**, `.sav` 1516-byte trophy prefix and `.sab` 7176-byte record. | Structs contain no pointers, `long`, or `size_t`; sizes should stay fixed. Widening ordinary integers would break files. | Current scalar sizes match x64 probe; explicit little-endian records and checked I/O still needed. | Preserve field order, 56-byte items, reserved fields and version fields; add golden-byte and truncated-file tests. Raw read/write return values are mostly unchecked. **High** on layout and operations. |
| D08 / DISK / P1 | [Hunt/Game/Trophy.cpp](../Hunt/Game/Trophy.cpp):392–520; [Core/GameState.h](../Hunt/Core/GameState.h), `KeyMap`; [Menu/Resources.cpp](../Menu/Resources.cpp), `TrophyLoad/TrophySave`; [Menu/Hunt.h](../Menu/Hunt.h), `Profile`, `TKeyMap`, `Options`: profile prefix plus fieldwise 4-byte options and raw key map. Menu explicitly converts stored `int32_t` booleans to/from `bool`. | **Yes**, shared engine/menu profile and virtual-key schema. | Fixed profile prefix is expected to match; do not replace 4-byte option reads with `sizeof(bool)` or dump `Options`. | OS key meanings must be translated while preserving saved numeric values and byte positions. | Assert engine/menu equivalence and all offsets, use explicit 32-bit disk bools; keep engine's intentional skip over four saved mode fields. Test reads/writes in both directions. **High**; current KeyMap is 17 × 4 = 68 bytes, `_iceage` menu variant adds a field and is outside C2 support. |
| D09 / DISK / P2 | [Hunt/Loaders/ScriptParser.cpp](../Hunt/Loaders/ScriptParser.cpp), resource readers; [EngineInit.cpp](../Hunt/Game/EngineInit.cpp), config; [Menu/Resources.cpp](../Menu/Resources.cpp), config and `LoadC2Maps`: text parsed into runtime structures. | **Yes**, text syntax, setting semantics, paths and `.c2map` descriptors; not compiler struct layouts. | Runtime configuration structs may grow without changing the text format. | Path/case handling and CRT parsing differ; see F02/F03 and C01. | Preserve syntax and field semantics, test text fixtures; do not infer a binary `TDinoInfo`/`TWeapInfo` format from assertion comments. **High**. |
| D10 / OPEN / P1 | Model/resource/profile consumers collectively: there are no supplied golden binary fixtures or executable compatibility results in this audit. Native struct reads imply little-endian, IEEE-754, default alignment. | **Yes**, compatibility with existing vanilla/MEE/mod content. | Source/probe checks do not prove all content variants load or saves round-trip. | Same, plus new compiler and path semantics. | Obtain an x86 reference run with user-owned assets and create original synthetic fixtures for the repository; record source provenance and compare bytes rather than changing formats to satisfy native layouts. **High** validation gap; exact variant coverage remains open. |

### Fixed byte inventory to carry into tests

| Record/buffer | Established size/encoding from source |
| --- | --- |
| `TPoint3d` | 16 bytes: three float32 coordinates, two 16-bit values. |
| `TFace` | 64 bytes: three 32-bit indices, six 32-bit integer UV values on disk, two 16-bit flags, three 32-bit fields, 12 reserved bytes. GL converts UVs after reading. |
| `TObj` / `TObjInfo` | 48 / 64 bytes. |
| `TFogEntity`, `TRD`, `TWaterEntity` | 20 / 16 / 16 bytes; these and the preceding model records matched the native probe. |
| Animation payload | Signed 16-bit XYZ triplets, 6 bytes per vertex per stored frame. |
| Resource texture/sky | `LoadTexture` reads 128 × 128 × 2 bytes; sky selects one of three 256 × 256 × 2-byte images; sky map is 128 × 128 bytes. Generated mip levels/runtime metadata are not additional serialized `TEXTURE` fields. |
| `.map` planes in read order | HMap 1 MiB; TMap1 2 MiB; TMap2 2 MiB; OMap 1 MiB; FMap 2 MiB; three LMap planes 1 MiB each (select one); WMap 1 MiB; HMapO 1 MiB; FogsMap 256 KiB; AmbMap 256 KiB. Total consumed/skipped layout: 13.5 MiB. |
| `.sav` trophy prefix / menu `Profile` | 1516 bytes = name[128] + three int32 + two 16-byte stats + 24 × 56-byte items. Native probe matched both definitions. |
| C2 `.sav` option suffix | 10 four-byte values + 68-byte KeyMap + 9 four-byte values = 144 bytes; current writer totals 1660 bytes with the prefix. Offsets must remain stable even when fields are ignored at load time. |
| `.sab` | 7176 bytes = two int32 + 128 × 56-byte items. Native probe matched. |
| BMP / TGA | Windows BMP file/info headers are expected 14/40 bytes; menu TGA header explicitly packs fixed-width fields into 18 bytes. Confirm bytes/offsets in fixture tests. |

No general structure packing directive applies to all engine records. Do not add
one to force pointer-bearing runtime structs back to x86 sizes. Endianness is an
external contract even though both initial x64 targets are little-endian.

## 5. Renderer/platform boundary

| ID / category / priority | File and symbol; current assumption | External contract | Windows x64 impact | Linux x64 impact | Proposed remediation; confidence / open questions |
| --- | --- | --- | --- | --- | --- |
| G01 / WIN / P2 | [Hunt/Renderer/GLRenderer.cpp](../Hunt/Renderer/GLRenderer.cpp), `CreateContext/DestroyContext`, GL presentation; [GLUtils.cpp](../Hunt/Renderer/GLUtils.cpp), `glad_get_proc`; [GLRenderer.h](../Hunt/Renderer/GLRenderer.h), `HWND/HDC/HGLRC`: Win32 pixel format, temporary WGL context, WGL 3.3 creation and `opengl32.dll`. | OS/driver ABI, not game content. | Native handle/function-pointer storage is appropriate; `opengl32.dll` name is not evidence of a 32-bit process. | WGL and those handles do not exist. Vendored GLAD alone does not port this path. | Put context creation, proc lookup and buffer swap behind the platform layer; retain Windows implementation for Phase 1. **High**. |
| G02 / WIN / P2 | [Hunt/Loaders/Resources.cpp](../Hunt/Loaders/Resources.cpp), `CreateVideoDIB`; [Game/Interface.cpp](../Hunt/Game/Interface.cpp); [Renderer/GLHUD.cpp](../Hunt/Renderer/GLHUD.cpp), [GLUI.cpp](../Hunt/Renderer/GLUI.cpp), [UIText.cpp](../Hunt/Renderer/UIText.cpp): GDI DIB allocation, fonts, `TextOut`, measurements, blits and HDC state underpin the GL HUD/loading UI. | Visual/text/pixel behavior, Windows drawing ABI. | GDI supports native handles; reviewed `lpVideoBuf` operations retain pointers. | Linux GL first frame/UI needs a replacement for DIB/text dependencies, not just a new context. | Separate CPU pixel buffers and text measurement/rendering from GDI; preserve 555/565 conversions and overlay alpha rules. **High**. |
| G03 / GPU / P1 | [Hunt/Renderer/GLRenderer.h](../Hunt/Renderer/GLRenderer.h), `TerrainVertex`, `ModelVertex`, `ModelInstance`, `StaticMeshVertex`, `ExactShade`; GL terrain/model attribute setup and [shaders](../shaders): fixed strides/offsets. | **Yes**, shader/vertex attribute interface; not a legacy disk record. | Native probe retains 32, 32, 176, 52, 8 bytes respectively. | Same, subject to supported scalar types/compiler layout. | Preserve size checks; add offset checks where useful. `reinterpret_cast<void*>(offsetof(...))` passes a GPU buffer offset, not a truncated CPU pointer. **High**. |
| G04 / GPU / P1 | [Hunt/Renderer/GLRenderer.cpp](../Hunt/Renderer/GLRenderer.cpp), `EnsurePerFrameUBO`, `UpdatePerFrameUBO`, `kUBOBytes=240`: CPU packs 60 float slots at fixed offsets described as `std140`, but the tracked shaders declare bare `uniform PerFrame` blocks without `layout(std140)`. Block declarations also differ in trailing members between programs; no offset/block-size queries were found. | **Yes**, shader uniform layout, distinct from pointer width. | CPU buffer size is stable, but assumed driver offsets are not established by those shader declarations. This is a pre-existing renderer portability risk, not a proved x64 regression. | New drivers can expose the same assumption. | Verify actual offsets/block sizes for every consuming program and make the intended layout explicit in a separate tested fix; preserve intended values/offsets and avoid unvalidated native struct uploads. **High** on source mismatch; **open** driver-specific impact (no context available). |
| G05 / GPU / P2 | [Hunt/Renderer/GLModel.cpp](../Hunt/Renderer/GLModel.cpp), `UploadStaticMesh`, `EnsureStaticMeshCapacity`, instancing/draw calls; [GLTerrain.cpp](../Hunt/Renderer/GLTerrain.cpp), streaming capacities: `size_t` byte capacities, `GLsizeiptr/GLintptr`, but `uint32_t` indices/cursors and `GLsizei` counts. | **Yes**, GL index/count types and buffer APIs. | Larger address space does not remove 32-bit index or signed draw-count limits; unchecked cumulative cursor increments and casts deserve bounds checks. No ordinary-content failure established. | Same. | Keep GPU indices/API types fixed, bound cumulative counts, byte products and conversions; split/reject oversized batches. **High** sites, **medium** practical reachability under current asset caps. |

The `ModelInstance` comment says “16-byte aligned,” but the type is an array of
floats without `alignas(16)`; its 176-byte stride is a multiple of 16. The inspected
upload path does not establish a need for 16-byte **CPU object** alignment. Keep
GPU offset requirements separate from CPU alignment requirements.

## 6. Input/window/timing dependencies

| ID / category / priority | File and symbol; current assumption | External contract | Windows x64 impact | Linux x64 impact | Proposed remediation; confidence / open questions |
| --- | --- | --- | --- | --- | --- |
| P01 / WIN / P0 | [Hunt/Game/Hunt.cpp](../Hunt/Game/Hunt.cpp):1049, `LONG APIENTRY MainWndProc(HWND, UINT, UINT, LONG)`; `CreateMainWindow`:1504 casts it to `WNDPROC`. | **Yes**, Windows callback ABI. | `wParam`, `lParam` and result are 32-bit while their callback ABI types are pointer-sized. Message payloads/default processing can truncate; the cast hides the mismatch. Window creation sends messages before gameplay. | Window callback must be replaced/isolated. | Use `LRESULT CALLBACK (..., WPARAM, LPARAM)` and assign without a cast; review default forwarding and message-specific low-bit extraction. Test window creation/focus/default messages. **High** confirmed type mismatch; no Windows crash reproduced. |
| P02 / WIN / P2 | [Hunt/Game/Hunt.cpp](../Hunt/Game/Hunt.cpp), `WinMain`, `CreateMainWindow`, `MainWndProc`; [Interface.cpp](../Hunt/Game/Interface.cpp), fullscreen/window setup; [EngineInit.cpp](../Hunt/Game/EngineInit.cpp), resolution enumeration: message loop, Win32 focus/priority, DPI lookup, display modes and system metrics. | OS events and user display/focus behavior. | Native Win32 calls can remain after P01. `Get/SetWindowLong(..., GWL_STYLE)` accesses a 32-bit style, not pointer user data; no pointer-slot misuse found there. | No native entry/event/window/display implementation. | Thin platform abstraction after Windows x64 validation, retaining event and focus semantics. **High**. |
| P03 / WIN / P2 | [Hunt/Game/Controls.cpp](../Hunt/Game/Controls.cpp), `CaptureMouse`, keyboard polling; [PlayerMovement.cpp](../Hunt/Game/PlayerMovement.cpp), cursor deltas; [Core/KeyBindings.h](../Hunt/Core/KeyBindings.h); `MainWndProc`: `GetKeyboardState`, `GetKeyState`, VK codes, `ClipCursor`, `ShowCursor`, `Get/SetCursorPos`, key-message bits. | Saved Windows virtual-key values and input behavior; see D08. | Key codes and key-message low 32 bits legitimately remain fixed; current callback width still needs P01. | Input backend and relative-motion/focus behavior unavailable. | Translate backend keys to the legacy binding schema initially; preserve saved keys and repeat/sided-modifier behavior. **High**. |
| P04 / WIN / P2 | [Hunt/Game/EngineInit.cpp](../Hunt/Game/EngineInit.cpp), `ProcessSyncro`, `SubmitDinoScore`; [Hunt.cpp](../Hunt/Game/Hunt.cpp), frame limiter; [Interface.cpp](../Hunt/Game/Interface.cpp), busy delay; [Resources.cpp](../Hunt/Loaders/Resources.cpp), message expiry: `timeGetTime`, signed `int RealTime`, `QueryPerformanceCounter`, `Sleep`, `SYSTEMTIME`. | Gameplay timing and trophy date/time encoding; clock handles are not serialized. | 32-bit millisecond wrap/signed conversion remains a pre-existing long-session issue. QPC uses `LARGE_INTEGER`, not a truncated pointer. | Replace timing/sleep/calendar APIs while retaining gameplay units. | Use monotonic native clocks and explicit wrap-safe conversions; test elapsed-time boundaries and preserve trophy date/time fields. **High** dependencies; long-session behavior not executed. |
| C01 / WIN / P2 | Engine/menu throughout: `sprintf_s`, `strcpy_s`, `strcat_s`; [EngineInit.cpp](../Hunt/Game/EngineInit.cpp), `sscanf_s`, `_stricmp/_strnicmp`; [RenderTypes.h](../Hunt/Core/RenderTypes.h) and [GLUtils.h](../Hunt/Renderer/GLUtils.h), `__forceinline`; [Debug/Assert.h](../Hunt/Debug/Assert.h), `__debugbreak`; pragma links/macros. | Parsing/bounds behavior; not binary ABI except OS-facing code. | Mostly accepted by MSVC x64; no reason to suppress pointer warnings. | GCC/standard libc lack several spellings/semantics, and secure scanf takes extra buffer-size arguments. | Use small portability helpers or standard equivalents with preserved checks; do not mechanically rename `sscanf_s` while retaining extra arguments. **High**. |

`GLPerf.cpp` already has `_WIN32`-guarded `localtime_s/localtime_r`, but its enabled
branch still includes `windows.h` and logs the working directory with
`GetCurrentDirectoryA`. Its `GL_PERF_HOOKS`-off stubs can remain during initial
bring-up. `#pragma once` itself is not an x86 blocker.

## 7. Filesystem/path dependencies

| ID / category / priority | File and symbol; current assumption | External contract | Windows x64 impact | Linux x64 impact | Proposed remediation; confidence / open questions |
| --- | --- | --- | --- | --- | --- |
| F01 / WIN / P2 | [Hunt/Loaders/LoadValidate.h](../Hunt/Loaders/LoadValidate.h), `ReadExact(HANDLE, void*, DWORD)`; model/resource/picture/sound loaders; [Game/Trophy.cpp](../Hunt/Game/Trophy.cpp); [Game/EngineInit.cpp](../Hunt/Game/EngineInit.cpp), config writing; [Loaders/Resources.cpp](../Hunt/Loaders/Resources.cpp), screenshots/logging: `CreateFile`, `ReadFile`, `WriteFile`, `CloseHandle`, low-word `GetFileSize/SetFilePointer`. | Windows I/O ABI; file bytes are D01–D08. | Handles are native, but transfer lengths stay `DWORD` and seek/stat code uses 32-bit interfaces/limits. `strlen`/`sizeof` to `WriteFile` can narrow; current small log/header writes are bounded in practice. | APIs unavailable. | Centralize exact reads/writes with native buffer lengths, explicit transfer chunking and wide offsets; preserve file records and intentional limits. Retain valid negative relative seeks. **High**. |
| F02 / WIN / P2 | [Hunt/Loaders/CharacterLoader.cpp](../Hunt/Loaders/CharacterLoader.cpp), `LoadCharacters` and weapon/call paths; [ScriptParser.cpp](../Hunt/Loaders/ScriptParser.cpp), `LoadResourcesScript`; [CommandLine.cpp](../Hunt/Game/CommandLine.cpp); [Menu/Resources.cpp](../Menu/Resources.cpp), `LoadC2Maps`; shader loading: mixed slash/case paths, relative HUNTDAT, executable-relative config plus cwd fallback. | **Yes**, existing mod directory names, path strings, launch working directory. | Windows usually resolves existing legacy paths; pointer width alone changes nothing. | Backslashes and mismatched filename case break opens; `std::filesystem` does not supply Windows semantics. `LoadC2Maps` already compares extension to lowercase `.c2map` exactly, even on Windows. | Central resolver: normalize separators, define asset roots, case-insensitive component lookup and deterministic collision policy; preserve config lookup/write precedence. Test mixed case/separators and executable versus cwd launches. **High**. |
| F03 / DISK / P2 | [Hunt/Loaders/LoadValidate.h](../Hunt/Loaders/LoadValidate.h), `PathBasename`, `RewriteExternalProjectAlias`; [ScriptParser.cpp](../Hunt/Loaders/ScriptParser.cpp), project/area filtering: fixed character positions in legacy project names, plus `external` → `area6` logical alias. | **Yes**, data-driven area selection and legacy project naming. | Existing logic remains sensitive to path shape, not pointer size. | Absolute paths/new roots or a normalizer can change offsets and select wrong script sections even if file lookup succeeds. | Separate logical project/area identity from resolved OS paths; retain alias behavior and area-filter tests. **High** identified dependency; broad path semantics need fixtures. |
| F04 / WIN / P2 | [Hunt/Audio/Audio_DLL.cpp](../Hunt/Audio/Audio_DLL.cpp):5 includes `"hunt.h"`, whereas tracked umbrella is `Hunt/Hunt.h`; [Menu/Hunt.h](../Menu/Hunt.h) includes `<Windows.h>`. | None; source/build paths. | Case-insensitive Windows filesystems mask the engine include mismatch. | Case-sensitive source lookup fails independently of asset resolution; Windows SDK headers must also be isolated. | Correct source include spelling and audit header isolation during Linux preparation; keep this distinct from runtime asset path compatibility. **High**. |
| F05 / WIN / P2 | [Hunt/Memory.h](../Hunt/Memory.h), arena; [Loaders/Resources.cpp](../Hunt/Loaders/Resources.cpp), heap dispatch/free; [Game/EngineInit.cpp](../Hunt/Game/EngineInit.cpp), `HeapCreate` and shutdown. | Memory ownership/backend behavior; no file bytes. | Windows heap/virtual memory APIs already accept native pointers; R04 is the width restriction in the wrapper. | No `VirtualAlloc/VirtualFree` or process/private heap API. | Preserve allocator/deleter pairing, zero-fill, lifetime tags and arena reset tracking behind a portable backing allocator. **High**; arena alignment logic itself is already native-width. |

No engine-wide case-insensitive asset resolver was found. Basename helpers and
menu path normalization in selected paths do not solve general component casing,
shader lookup, or source include casing. Preserve original assets without
requiring users to rename or convert their mods.

## 8. Audio dependencies

| ID / category / priority | File and symbol; current assumption | External contract | Windows x64 impact | Linux x64 impact | Proposed remediation; confidence / open questions |
| --- | --- | --- | --- | --- | --- |
| A01 / DEP / P1 | [Hunt/Audio/OpenAL_Loader.cpp](../Hunt/Audio/OpenAL_Loader.cpp), `LoadOpenAL/UnloadOpenAL`; [OpenAL_Loader.h](../Hunt/Audio/OpenAL_Loader.h): loads `openal32.dll` with `LoadLibraryA/GetProcAddress`; maintains handwritten AL/ALC/EFX declarations. Shared by engine and menu. | **Yes**, external library ABI. | DLL must have x64 machine type. Filename alone does not tell bitness. Opaque pointers/native function pointers are not truncated in the inspected loader. | Windows loader/header dependency blocks compilation/loading. | Supply and verify an x64 OpenAL Soft runtime for Phase 1; later use platform-neutral discovery/linking and official headers or validated declarations. **High** source dependency; no DLL binaries were available to inspect. |
| A02 / DEP / P1 | [Hunt/Audio/Audio_DLL.cpp](../Hunt/Audio/Audio_DLL.cpp), `LoadLegacyAudioBackend`, `InitAudioSystem`, `LegacyAudio*Fn`: optional `a_ds3d.dll` with `WINAPI`, handles, data pointers and geometry callbacks; fallback to OpenAL. | **Yes**, optional legacy code ABI, not content format. | Existing x86 DLL cannot load in x64; legacy failure already falls back. Missing OpenAL disables sound. | DLL path cannot be portable as written. | Keep legacy backend for compatible Windows x86; make x64 fallback/support status explicit, verify OpenAL. No requirement to port third-party x86 engine extensions. **High**. |
| A03 / WIN / P2 | [Hunt/Audio/Audio_DLL.cpp](../Hunt/Audio/Audio_DLL.cpp), `ProcessAudioThread`, initialization/shutdown, `AudioCS`, `g_AudioShutdown`: Win32 threads, critical sections, sleep and volatile `BOOL` control flag. | Internal synchronization, audio timing/context behavior. | Callback correctly uses `DWORD WINAPI(LPVOID)` and native handles. `volatile` is not a portable inter-thread synchronization guarantee. | Requires thread/lock/stop implementation and review of context use across threads. | Use deliberate synchronization and preserve orderly join/resource teardown and audio semantics. **High** APIs; races/context behavior need runtime tests. |
| A04 / DEP / P2 | [Hunt/Audio/Audio_DLL.cpp](../Hunt/Audio/Audio_DLL.cpp), `GetBuffer`; [Menu/Audio.cpp](../Menu/Audio.cpp), `UploadSound`: AL object IDs and signed byte lengths remain `ALuint/ALsizei`, PCM pointers stay native. | **Yes**, AL buffer API and 16-bit PCM. | No need to make AL IDs/payload sample widths pointer-sized; bound lengths before AL calls. | Same API limits with a portable OpenAL backend. | Retain API widths and checked byte lengths; use official types. **High** positive finding. |

## 9. Networking/threading dependencies

| ID / category / priority | File and symbol; current assumption | External contract | Windows x64 impact | Linux x64 impact | Proposed remediation; confidence / open questions |
| --- | --- | --- | --- | --- | --- |
| N01 / WIN / P2 | [Hunt/Network/NetworkManager.h](../Hunt/Network/NetworkManager.h), socket members/accessors; [NetworkManager.cpp](../Hunt/Network/NetworkManager.cpp), startup/connect/send/receive/thread lifecycle; [Game/Network.cpp](../Hunt/Game/Network.cpp), wrappers. Uses WinSock, `SOCKET`, `INVALID_SOCKET`, Win32 thread handles/procs, `Sleep`. | OS socket/thread API. | Sockets remain `SOCKET`, not narrowed `int`; `send/recv` results and lengths legitimately use the Windows `int` API. `this` passes via `LPVOID`. | Requires socket/error/close/thread abstraction. Network code is in core even for single-player builds. | Preserve native handle types on Windows; later port or feature-gate network lifecycle for Linux single-player. **High**. |
| N02 / NET / P1 | [Hunt/Network/NetworkManager.cpp](../Hunt/Network/NetworkManager.cpp):16–51, `putInt`, `putInt2`, `putFloat`, `readInt`, `readInt2`, `readFloat`: `long` input but explicitly emits 1, 2, or 4 bytes; 2/4-byte order is high byte first. `putFloat` takes integer bits, not a float; reader aliases `int*` as `float*`. | **Yes if used**, established helper wire encoding; active peer interoperability is unverified. | Windows `long` still 32 bits, so x64 alone does not expand packets. Signed shifts/overflow and aliasing remain issues. | LP64 input width changes, but helpers still emit only low 32 bits; strict-aliasing optimizations can expose the float read defect. | Use explicit unsigned fixed-width bit containers, `memcpy` float bit transfer, and golden-wire tests. Establish existing caller/peer expectations before changing helper semantics. **High** on encoding, **open** actual protocol use: no helper writer call sites were found in this checkout. |
| N03 / OPEN / P2 | [Hunt/Network/NetworkManager.cpp](../Hunt/Network/NetworkManager.cpp), `RecvPacket/SendPacket`, `ServerThread/ClientThread`: fixed 189-byte receives/10-byte sends, reused receive buffer; init compares one-byte `readInt` result with 43981; no complete game-state serializer found. | Existing multiplayer interoperability, presently unproven. | Pre-existing incomplete/suspect protocol logic; not evidence of raw `sizeof(TCharacter)` packets. TCP short reads/writes are not fully handled. | Same issues would carry into a port. | Capture/reference working peers, specify framing/payloads and test partial transfers before claiming compatibility; do not invent a new protocol in Phase 1. **High** code facts; behavior and intended packet schema remain open. |
| N04 / WIN / P2 | [Hunt/Network/NetworkManager.cpp](../Hunt/Network/NetworkManager.cpp), `StopServer/StopClient`, worker loops; [NetworkManager.h](../Hunt/Network/NetworkManager.h), `m_haltThread`, `m_commsThreadID`. Stop flag is an unsynchronized bool; stop waits before closing sockets that may block in `accept/recv`. Thread ID storage is a null `LPDWORD` optional output. | Internal threading/lifecycle only. | Potential race/hang exists independently of bitness; null optional thread-ID output is not pointer truncation. | Needs deliberate cancellation/join ordering and synchronization. | Test blocked shutdown, wake/cancel socket operations, synchronize stop state; retain correct native thread callbacks. **High** source pattern, **medium** reproduced impact (not executed). |

No raw `sizeof(runtime_struct)` socket send/receive was found. Do not classify
`TCharacter`, `TPack`, or `TPlayer` as wire structs solely because they represent
multiplayer state. Keep protocol hardening after single-player bring-up unless
it blocks compilation or shutdown of that target.

## 10. Menu/launcher dependencies

| ID / category / priority | File and symbol; current assumption | External contract | Windows x64 impact | Linux x64 impact | Proposed remediation; confidence / open questions |
| --- | --- | --- | --- | --- | --- |
| M01 / WIN / P2 | [Menu/Hunt2.cpp](../Menu/Hunt2.cpp), `WinMain`, `WindowProcedure`, `ErrorDialogProc`, `LaunchProcess`; [Menu/Menu.cpp](../Menu/Menu.cpp), interface/font/input/sound-device functions; [GUI.cpp](../Menu/GUI.cpp), [Resolutions.cpp](../Menu/Resolutions.cpp); resources. GDI/DIBs, WinMM queries, shell/process APIs and Windows dialogs. | Windows UI/launcher ABI and visual/input behavior. | Menu callbacks correctly use `LRESULT`, `WPARAM`, `LPARAM`, `INT_PTR`; WinMM callback uses `DWORD_PTR`. Profile compatibility is D08. | Entire launcher platform implementation unavailable. | Make menu independently selectable; port/rebuild after engine Linux health. Retain native Windows menu while auditing its own x64 build. **High**. |
| M02 / WIN / P1 | [Menu/Menu.cpp](../Menu/Menu.cpp), hunt/trophy launch command construction; [Menu/Hunt2.cpp](../Menu/Hunt2.cpp), `LaunchProcess`; package naming: `v_gl.ren` and `v_soft.ren` launched with `CreateProcess`, inherited cwd. | **Yes**, executable names, command-line parameters, working directory, shared profiles/config. | `.ren` is a separate process, not an in-process renderer DLL; x86 launcher can start x64 engine. Mixed architectures sharing one `OpenAL32.dll` directory cause an audio deployment conflict. | `CreateProcess` and Windows executable files require replacement; argument/path behavior must stay compatible. | Preserve launch arguments and give architecture-specific packages/dependency resolution. Test menu → engine → menu return. Direct engine launch can unblock first boot without porting the menu. **High**. |
| M03 / OPEN / P2 | [Menu/Hunt.h](../Menu/Hunt.h):193, `Color16`; [Menu/Menu.cpp](../Menu/Menu.cpp), `DrawRectangle`: packed `int` bitfields assumed to represent a 16-bit pixel and used for buffer indexing. | Pixel representation/visual behavior, no identified disk dump of this type. | Integer bitfield allocation is compiler-specific; packing to one byte does not itself establish a two-byte object. Not newly caused by x64. | GCC/MSVC allocation/order can differ, risking wrong pixel stride. | Measure actual compiler layouts/reachability; replace pixel storage with explicit `uint16_t` masks when menu portability is addressed. **High** source assumption, **open** actual MSVC layout and active use. |
| M04 / WIN / P2 | [Menu/Hunt2.cpp](../Menu/Hunt2.cpp):69, `CreateLog`: tests `_W64` rather than target pointer width or `_WIN64`; [Menu/README.md](../Menu/README.md), x86-only status. | Diagnostic/support claims only. | Can misreport target architecture; `_W64` is not the architecture selector. | Same unreliable report plus Windows dependency. | Report `sizeof(void*)` or deliberate build metadata; update support docs when validated. **High**; not a first-boot blocker. |

`Menu/Menu.cpp` also treats `TKeyMap` members as an indexed `int` array in the key
binding UI. The current C2 layout consists of 17 adjacent int32 values and is
shared with saves; use explicit indexing/accessors when separating that runtime
representation, without changing D08's saved order. `GetExitCodeProcess` writes
through `(DWORD*)&exitCode` into a `uint32_t`; both are four-byte values on
Windows, so this is not a pointer-width storage truncation (prefer exact API
storage in a future cleanup).

## 11. Software renderer/x86 assembly isolation

| ID / category / priority | File and symbol; current assumption | External contract | Windows x64 impact | Linux x64 impact | Proposed remediation; confidence / open questions |
| --- | --- | --- | --- | --- | --- |
| S01 / SOFT / P0 for SOFT only | [Hunt/Renderer/renderasm.cpp](../Hunt/Renderer/renderasm.cpp), scanline/model rasterizers; [SoftModel.cpp](../Hunt/Renderer/SoftModel.cpp), model raster paths; [SoftVideo.cpp](../Hunt/Renderer/SoftVideo.cpp), `_FillMemoryWord`, `FillMemoryWord`, `_memcpy`: MSVC inline assembly, 32-bit registers/addresses and hard-coded member offsets. | Internal legacy rasterizer ABI and behavior; not portable renderer support. | Cannot build these active paths with MSVC x64. Entire `renderasm.cpp` is under `_soft`; other SOFT source bodies are guarded and selected for SOFT. No active inline assembly was found in GL/common math paths. | Same architecture/compiler restrictions. | Keep legacy x86 MSVC target; enforce source/target isolation instead of rewriting/removing it. **High**. |
| S02 / SOFT / P0 for SOFT only | [Hunt/Renderer/SoftVideo.cpp](../Hunt/Renderer/SoftVideo.cpp):79, `ClearVideoBuf`: `lpVideoBuf` → `intptr_t` → `int`, passed to integer-address fill routine; [Core/ModelTypes.h](../Hunt/Core/ModelTypes.h), `_soft TFace`; [ModelLoader.cpp](../Hunt/Loaders/ModelLoader.cpp), soft UV conversion. | Legacy rasterizer runtime layout and fixed-point texture semantics. | Address truncation if made active on x64; not an OpenGL call path. | Unsupported legacy code. | Leave inside supported x86 boundary; do not “fix” by casts or force float UVs on SOFT. Retain x86 visual regression check. **High**. |
| S03 / WIN / P2 | [Hunt/Hunt.h](../Hunt/Hunt.h), unconditional `ddraw.h`; engine link list includes `ddraw`; shared sources retain `_d3d/_3dfx` conditionals despite supported CMake renderer choices being GL/SOFT. | Windows legacy renderer APIs. | Header/library coupling does not itself establish an x64 GL failure; don't confuse it with active assembly. | Legacy headers/libraries leak into GL build and block portability. | Scope legacy headers/libraries to actual consumers and keep shared declarations platform-neutral later. **High**. |

## 12. Third-party dependencies

| ID / category / priority | File and symbol; current assumption | External contract | Windows x64 impact | Linux x64 impact | Proposed remediation; confidence / open questions |
| --- | --- | --- | --- | --- | --- |
| T01 / DEP / P2 | [deps/glad/glad.c](../deps/glad/glad.c), [glad.h](../deps/glad/glad.h), [KHR/khrplatform.h](../deps/KHR/khrplatform.h): generated source includes Windows and Unix loader branches; Khronos pointer/size types explicitly distinguish `_WIN64` from LP64. | **Yes**, OpenGL ABI. | No identified built-in x86-only type restriction; compiled as source for target. | Loader has `dlopen`/GLX paths, but engine calls its own WGL proc callback. Native loader compilation/linking may require dynamic-loader libraries. | Preserve correct GL types; use platform proc callback for future SDL rather than assuming GLAD replaces window/context code. **High**. |
| T02 / DEP / P2 | [CMakeLists.txt](../CMakeLists.txt), `FetchContent` GoogleTest `release-1.12.1`, static MSVC runtime policy. | Build/test dependency only. | Tests/dependency must compile for same target architecture/runtime. No vendored x86 `.lib` identified. | GoogleTest is gated off by project CMake; Windows-coupled test includes remain. | Keep version changes separate, build test dependency per target and decouple portable tests; run x86/x64 configurations. **High**. |
| T03 / DEP / P1 | [tools/release/make-c2-package.ps1](../tools/release/make-c2-package.ps1), artifact selection and `OpenAlDir`: copies fixed GL/menu/SOFT paths and a supplied `OpenAL32.dll`; README text declares x86. | **Yes**, package layout/runtime architecture and `.ren` launch contract. | No machine-type validation or x64 package split; can mix x64 GL, x86 SOFT/menu, and one incompatible DLL. | Windows-only package/process assumptions. | Verify PE machine type for executables/DLLs; separate supported packages or provide explicit per-process runtime locations. Keep x86 reference package and omit unsupported x64 SOFT. **High**. |

`deps/` contains GLAD/Khronos source headers, not an OpenAL binary. No checked-in
OpenAL, SDL, or proprietary asset bundle supplies a ready x64 runtime. This audit
does not infer bitness or licensing of the externally supplied release DLL.

## 13. Risk-ranked remediation plan

| Rank | Findings | Why this order | Evidence required before moving on |
| --- | --- | --- | --- |
| 1 — establish reference | B02, B05, D10 | A new architecture cannot be judged without a reproducible x86 baseline. | Record compiler/preset/artifact architecture; x86 GL unit results and user-owned vanilla/MEE/mod hunt captures, config and save hashes. Existing CI status is not certified by this audit. |
| 2 — freeze external bytes | D01–D08, G03–G04 | Removing runtime asserts must not weaken actual format guarantees. | Original synthetic little-endian fixtures and record-size/offset assertions; engine/menu profile equivalence; truncated input, integer UV and animation convention tests. |
| 3 — remove true x64 blockers | R01–R03, P01, B01 | Compile restriction and callback ABI are confirmed defects for x64. | Native MSVC x64 Debug/Release GL builds with correct callback assignment; x86 GL still passes; SOFT remains explicitly x86-only. |
| 4 — width-safe memory boundary | R04–R07, R09, G05 | A wider address space makes prior narrowing and overflow assumptions more consequential. | Checked multiplication tests including `(2^63,2)`, 32-bit transfer limits, `SIZE_MAX` products and zero cases; native allocation sizes through debug/release wrappers; bounds at GL/AL interfaces. |
| 5 — prove supported Windows behavior | A01–A02, M02, T03, B05, D10 | Correct DLL architecture, content loading, profile interchange and shutdown are release gates. | x64 GL enters and exits hunts, renders expected HUD/content, plays OpenAL sound, reloads levels, and exchanges profile bytes with x86/menu. Verify module architecture and bounded/no-leak behavior. |
| 6 — platform preparation and Linux | B03–B04, C01, G01–G02, P02–P04, F01–F05, A03, T01–T02 | These are OS/compiler dependencies rather than reasons to widen disk records. | Portable parser/unit builds, source-case checks, platform window/input/GL/text interfaces, compatible asset path tests and Linux single-player hunt. OpenAL integration follows the porting plan; audio stubs/feature selection may be needed for earlier compile stages. |
| 7 — deferred networking/menu work | N01–N04, M01, M03–M04 | Protocol baseline and launcher replacement are separate from single-player engine first boot. | Documented old-peer wire samples, partial-transfer/shutdown tests; later menu/profile/launch compatibility checks. |

No step permits a legacy format redesign, an SDL integration in this audit,
conversion requirements for existing content mods, or replacement of the
portable OpenGL renderer. Preserve the existing x86 target throughout.

## 14. Proposed sequence for the first implementation PRs

1. **Compatibility fixtures and baseline controls.** Record the x86 GL baseline,
   make test executable/asset paths explicit, and add synthetic layout/parser
   fixtures for D01–D08. Keep fixed disk/GPU assertions distinct from runtime
   ones. This PR changes tests/documentation only unless a minimal test seam is
   necessary; it establishes expected behavior before risky changes.
2. **Windows x64 GL build and ABI corrections.** Add explicit architecture
   environments/presets and separate output directories; constrain SOFT;
   replace x86-only runtime assertions with meaningful supported-ABI checks;
   correct `MainWndProc` without a cast. Build x86/x64 GL Debug and Release.
   Changes affect build configuration, runtime layout and OS callback ABI;
   disk/wire/GPU bytes stay unchanged.
3. **Native allocation sizes and checked arithmetic.** Update all wrapper
   declarations/overloads/debug macros together, implement overflow checks that
   work for both size widths, and review call-site products/diagnostics. Keep
   external field sizes and API transfer limits explicit. Add boundary tests
   before or alongside each changed interface; retain arena/deleter ownership.
4. **Windows runtime packaging and first-hunt validation.** Verify an x64
   OpenAL runtime, separate incompatible mixed-architecture packages, exercise
   direct launch and launcher handoff, and compare unmodified content/save
   behavior to x86. Report actual configuration and limitations; a timed process
   launch alone is not the Phase 1 exit criterion.
5. **Serialization hardening in small format-specific PRs.** Replace implicit
   runtime-layout dependencies with explicit readers/writers, starting with
   model/resource records and engine/menu profiles, backed by the fixtures from
   PR 1. Preserve all established bytes, optional tails and field semantics.
   Any format-specific defect discovered during first boot moves into an earlier
   bounded fix rather than being hidden by widening or packing.

Then follow `PORTING.md`: thin platform layer, filesystem compatibility, Linux
single-player, portable OpenAL integration, input/display improvements,
networking, and launcher/menu. Build-time isolation of unavailable network/menu
dependencies may precede their functional ports; it is not a protocol rewrite.

### Windows x64 first boot versus deferred work

“First boot” here means the native GL engine can load unmodified compatible
assets and enter a hunt. Supported release validation additionally includes
sound, save/profile interchange, shutdown and regression checks.

| Blocker / finding | Windows x64 first boot | Can wait until Linux x64 or later? |
| --- | --- | --- |
| Four-byte pointer and x86 runtime assertions — R01–R03 | **Hard compile blocker.** Correct runtime checks while retaining disk/GPU contracts. | No. |
| Incorrect window procedure signature — P01 | **Fundamental runtime ABI blocker.** Fix before attempting a trusted boot. | No. |
| Explicit x64 GL toolchain and SOFT isolation — B01–B02, S01–S02 | Required to produce/review the right artifact. Assembly is already excluded by `_soft` guards in GL. | No build-policy deferral; an assembly port is unnecessary. |
| Allocation narrowing/overflow — R04–R06 | Bounded legacy assets may boot, but width checks are a Phase 1 correctness gate; no normal-content overflow proved here. | Do not defer the verified helper defect/native interface audit past Windows x64 support. |
| Fixed disk/GPU records and profile tests — D01–D08, G03–G04 | Preserve byte sizes; validate before claiming compatibility. These records are not themselves proved x64 blockers. | Full explicit-decoder refactoring can follow first boot; byte guarantees cannot. |
| x64 OpenAL and package architecture — A01–A02, T03 | Required for working sound/support; load failure currently permits a silent boot. | Native Windows DLL selection now; portable OpenAL loader/threading later. |
| Native WGL/GDI/window/input/timing — G01–G02, P02–P04 | Existing Windows implementations can remain after callback fix. | **Yes; Linux engine blockers**, including GDI HUD/text. |
| Win32 heap/file APIs, source casing, CRT and asset-path semantics — B03–B04, C01, F01–F05 | Usable under Windows subject to size checks. | **Yes; Linux engine blockers.** Preserve legacy paths and file bytes. |
| WinSock/Win32 network workers and protocol baseline — N01–N04 | Native handle types already support x64; no multiplayer validation implied by single-player boot. | **Yes; isolate for Linux single-player, port networking later.** |
| GDI launcher replacement — M01, M03–M04 | Direct engine launch suffices; a separate x86 launcher can spawn x64 with compatible deployment. | **Yes; launcher follows engine health.** |
| x64 software renderer | Not a supported initial target. | Remain Windows x86 legacy-only; must not block either GL target. |
