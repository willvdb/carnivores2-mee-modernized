# Windows OpenGL x86/x64 builds

This first implementation slice addresses audit findings B01–B03, R01–R03 and
P01, with record-layout coverage for D01–D08. It enables Windows x64 compilation.
Local Wine smoke checks supplement the builds; native Windows validation of the
first-hunt exit criterion in [PORTING.md](PORTING.md) remains outstanding.

## Build and test

Install Visual Studio 2019/2022 or its C++ Build Tools, the Windows SDK, Ninja,
and CMake 3.21 or newer. Use an **x86 Native Tools Command Prompt** for x86,
and a separate **x64 Native Tools Command Prompt** for x64. Run from the repo root.
Ninja does not switch toolchains based on a preset's architecture metadata;
CMake verifies the active compiler's target macros and pointer width.

In the x86 prompt:

```bat
cmake --preset windows-x86-gl-debug
cmake --build --preset windows-x86-gl-debug
ctest --preset windows-x86-gl-debug
cmake --preset windows-x86-gl-release
cmake --build --preset windows-x86-gl-release
ctest --preset windows-x86-gl-release
```

In the x64 prompt:

```bat
cmake --preset windows-x64-gl-debug
cmake --build --preset windows-x64-gl-debug
ctest --preset windows-x64-gl-debug
cmake --preset windows-x64-gl-release
cmake --build --preset windows-x64-gl-release
ctest --preset windows-x64-gl-release
```

Each preset uses `build/<preset>/`, including its own test dependency build.
The engine is `build/<preset>/bin/Carnivores1_GL.exe`; shaders are copied alongside
it. The default build also compiles the existing menu and tests. Use
`--target Carnivores1` to build just the engine. Test presets run the asset-free
`Carnivores2Tests*` tests, including menu profile layout checks.

Existing `ogl-*`, `soft-*` and `menu-release` presets keep their names and output
paths and now explicitly require x86. SOFT requires Windows x86 with an MSVC
compatible compiler, even without a preset. Its assembly translation unit is
only selected for SOFT. `/arch:SSE2` remains an x86 Release option.
The GL presets share the same Debug/Release settings, including
`GL_PERF_HOOKS=ON` in Release; use `-DGL_PERF_HOOKS=OFF` if desired.

The hidden `base` supplies only Ninja and per-preset build/install paths; it
sets no platform, compiler or architecture. Hidden `windows-x86-base` and
`windows-x64-base` presets add the MSVC architecture metadata and matching
`CARNIVORES_ARCH` check. Hidden `gl-debug-base` and `gl-release-base` presets
hold architecture-independent GL settings. Visible GL presets combine the
appropriate Windows base with those settings; x64 never inherits an x86
preset. Legacy SOFT and menu presets inherit `windows-x86-base`. All visible
names, effective settings and output paths are preserved. No Linux preset or
Linux build support is added by this hierarchy.

CI activates the matching MSVC environment and builds/tests x86 and x64 GL in
Debug and Release, plus the legacy x86 SOFT and menu builds. Hosted runners have
no supplied HUNTDAT, so automated CI runs asset-free tests instead of the old
asset-dependent smoke/leak steps. The existing smoke script remains available
for separate local validation with user-owned data.

## Compatibility boundaries

- **OS callback ABI:** `MainWndProc` uses `LRESULT CALLBACK`, `WPARAM` and `LPARAM`,
  and assigns directly to `WNDCLASS::lpfnWndProc`. Default-message forwarding
  retains the native-width parameters and return value.
- **Runtime layout:** pointers and their owners grow naturally on x64. Checks
  distinguish the Windows x86/x64 sizes; smart-pointer storage remains equal to
  native pointer storage. The STL-dependent `TWeapon` exact size retains only
  its original x86 Release check. Runtime types are not packed or serialized.
- **Disk layouts:** existing trophy/resource assertions remain unconditional.
  New tests check fixed sizes, offsets and trivial copyability of model,
  resource, trophy, profile, key-map and image-header records, plus map planes,
  animation samples and texture widths. Engine and menu independently consume
  the same synthetic little-endian profile fixture and preserve its bytes on
  update. Face fixtures preserve integer UV bits before the existing conversion.
- **Wire, GPU and gameplay:** no definitions or behavior change. Existing GPU
  layout assertions remain in place. No filesystem, audio-loader, networking,
  window architecture, menu logic, parser or serialization implementation changes.

These are record-layout tests, not complete file-loader or profile-option-suffix
round trips. Truncated input, animation frame conventions, full asset
compatibility, wire interoperability and GPU driver validation remain separate
work. Fixtures are original synthetic bytes; no game assets are included.

## Local validation (2026-09-16)

Host: Linux x86_64, CMake 4.4.3, Ninja and Wine 11.17. Installed Microsoft
Visual Studio 2022 build tools locally using
[msvc-wine](https://github.com/mstorsjo/msvc-wine), revision
`514f8ea34842cd6d831804d0e9658d3a32870ae1`:

- MSVC compiler **19.44.35229**, installed toolset directory `14.44.35207`.
- Windows SDK **10.0.26100.0**.
- Actual Microsoft compiler, linker, resource compiler and STL, executed under
  Wine; no substitute Windows declarations or Clang compiler for these builds.
- Dedicated SDK/toolchain directories and Wine prefix outside the repository.
  The toolchain is not redistributed with the source or binaries.

| Configuration | Full build (engine, menu, tests) | Engine architecture | Tests under Wine |
| --- | --- | --- | --- |
| Windows x86 GL Debug | Passed | PE32, Intel i386 | 117/120 passed |
| Windows x86 GL Release | Passed | PE32, Intel i386 | 117/120 passed |
| Windows x64 GL Debug | Passed | PE32+, x86-64 | 117/120 passed |
| Windows x64 GL Release | Passed | PE32+, x86-64 | 117/120 passed |

Each configuration passes all 94 core tests (including the new engine layout
fixtures), the menu profile layout test, and all 14 fog-sampling tests. Eight of
11 UI text tests pass. The same three UI tests fail in all four configurations:

- `UITextTest.LongNameShrinksTheFontToFitThePanel`
- `UITextTest.SingleLongRowIsClampedEvenWithoutPairing`
- `UITextTest.FiveRowBlockFitsThePanelHeight`

These font-dependent assertions were also tested using an untouched archive of
baseline `d62905a0d23adf1d3a11f26b3b40219a7b7290cb`, compiled as x86 Debug
with the same Microsoft toolchain and Wine prefix. **The baseline reproduces all
three failures**, including the same measured values. This establishes that the
failures under this environment predate the implementation; it does not prove
whether native Windows has the same behavior. No UI code or tests were changed
to make them pass, and CTest continues to report the failures.

Earlier host checks also passed all four new layout tests on GCC with 32-bit and
64-bit pointers using Wine's Windows type declarations. Clang configure probes
verified both target architectures, rejection of mismatched presets and x64
SOFT, and exclusion of software/assembly sources from GL. Those limited probes
are now supplemented by the complete MSVC builds above.

### Reusing the installed local toolchain

On this development machine, the installation is under
`~/.local/share/carnivores-msvc`, with a dedicated prefix at
`~/.local/share/carnivores-msvc-prefix`. Local environment and CMake toolchain
helpers are alongside it. No system packages, shell startup files or existing
game prefixes were modified.

From the repository root, use this example for x64 Debug:

```sh
source ~/.local/share/carnivores-msvc-env.sh x64
cmake --preset windows-x64-gl-debug -B build/msvc-wine-x64-gl-debug \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/.local/share/carnivores-msvc-toolchain.cmake"
cmake --build build/msvc-wine-x64-gl-debug --parallel 6
ctest --test-dir build/msvc-wine-x64-gl-debug \
  -R '^Carnivores2Tests' --output-on-failure --timeout 60
```

Use `x86` in the environment command, preset and build directory for x86, and
`release` in the preset/build directory for Release. All four build directories
already exist. The executables are at
`build/msvc-wine-<arch>-gl-<config>/bin/Carnivores1_GL.exe`.
The helper tells CTest to execute Windows tests through Wine. Builds use the
normal presets' debug information (`/Zi` in Debug), runtime selection,
optimization and record definitions. GoogleTest remains version 1.12.1.

Configure/build/test logs are retained as
`~/.cache/carnivores-{configure,build,tests}-<arch>-<config>.log`.
CTest's complete individual-test output is also in each build directory under
`Testing/Temporary/LastTest.log`. The unchanged baseline UI result is in
`~/.cache/carnivores-baseline-ui.log`.

### Runtime smoke checks under Wine

The x86 and x64 Debug and Release engines were also launched against independent copies
of locally owned Genesis Redux 1.1 content, outside the repository. The original
installation and profiles were preserved. Each copy contains the matching menu,
GL engine (also named `v_gl.ren` for menu launch), shaders and the corresponding
Win32/Win64 `soft_oal.dll`, renamed `OpenAL32.dll`, from the official
[OpenAL Soft 1.25.2 Windows binaries](https://openal-soft.org/openal-binaries/openal-soft-1.25.2-bin.zip).
No audio-loader or asset changes were needed.

The automated run used Wine 11.17 inside headless Gamescope at 1024×768, with
hardware OpenGL on an AMD Radeon RX 7900 XT / Mesa 26.2.2. It launched area1
directly in observer mode, using profile 0, dinosaur 1 and weapon 1. All four builds:

- Loaded the existing profile, map, models, textures and sounds and entered the
  game loop; logs report a core OpenGL context and successful shader setup.
- Initialized OpenAL, with the matching local DLL confirmed in loaded modules.
- Returned the window title through `WM_GETTEXT` from a separate x64 probe,
  whose output buffer was above the 32-bit address range.
- Handled minimize/restore and a subsequent window-message probe.
- Exited with code 0 after `WM_CLOSE`, logging both normal engine shutdown and
  OpenAL shutdown. This exercises cleanup, not the evacuation/save path.

Both Debug runs also report `No memory leaks detected.` from the engine's
existing allocation tracker at shutdown; this is not a comprehensive leak audit.

These are bounded smoke checks, not a gameplay certification. Headless screenshot
hotkeys/capture were unsuccessful, so no automated visual comparison is claimed.
The developer subsequently played both isolated Release copies under Wine and
reported that both worked well. This provides an interactive smoke-test result;
it does not establish exhaustive control, audio, save-interchange, repeated-level
or long-session coverage. Existing synthetic layout tests do not replace those
checks.

Local evidence is retained under
`~/Games/carnivores-x64-validation/evidence/`, including per-run logs, module
lists, result JSON and copies of the profiles. None of these proprietary assets
or local automation tools are committed.

### Playing the local validation copies

On this development machine, launch the isolated Release copies with:

```sh
~/Games/carnivores-x64-validation/run.sh x64
# Exit the game, then compare with:
~/Games/carnivores-x64-validation/run.sh x86
```

These local helpers use separate Wine prefixes and copied profiles, and reuse
the existing Gamescope installation for fullscreen presentation. They open the
matching menu; select the existing profile, keep the renderer set to OpenGL,
and start a hunt. The first launch may take longer while Wine initializes its
prefix. Launch logs are `manual-<arch>-launch.log` in the validation directory;
engine logs are `<arch>/render.log` and `<arch>/carnivor.log`.

Compare movement/mouselook, weapon use, terrain/models/HUD, audible sound,
map/binoculars, pause and Alt-Tab. Evacuate back to the menu, start another hunt,
then exit and relaunch to check the saved profile. Test the same area and settings
on both architectures and record any difference.

### Remaining validation

Native Windows CI execution and native Windows gameplay remain unverified.
Allocation-width/arithmetic fixes (R04–R07) and upstream runtime packaging remain
deferred. Matching audio DLLs were supplied only for the local validation copies.
This change does not claim production x64 support or a Linux port.

## Cleanup validation (2026-09-17)

The Phase 1 history was rebased onto audit guidance commit `aeeb560` and split
into serialization tests, Windows build configuration, Win64 runtime ABI/layout
fixes, and documentation. The build commit introduces the x64 targets; compiling
those targets requires the following runtime-fix commit. The tests-only commit
was separately built against the original x86 runtime: all 94 core tests and the
menu layout test passed. No runtime source or test content changed relative to
the original implementation `d9b818b` during this cleanup.

Fresh builds used the same Microsoft compiler/SDK and Wine installation described
above. Each default build included the menu and asset-free tests, plus the engine
where selected:

| Preset | Build | Individual tests under Wine |
| --- | --- | --- |
| `windows-x86-gl-debug` | Passed, PE32 engine | 117/120 passed |
| `windows-x86-gl-release` | Passed, PE32 engine | 117/120 passed |
| `windows-x64-gl-debug` | Passed, PE32+ engine | 117/120 passed |
| `windows-x64-gl-release` | Passed, PE32+ engine | 117/120 passed |
| `soft-release` | Passed, PE32 engine | 117/120 passed |
| `menu-release` | Passed, PE32 menu | 117/120 passed |

For each row, source the local environment helper with the matching architecture,
then run (substitute the table's preset name for `<preset>`):

```sh
cmake --preset <preset> -B build/cleanup-msvc-<preset> \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/.local/share/carnivores-msvc-toolchain.cmake"
cmake --build build/cleanup-msvc-<preset> --parallel 6
ctest --test-dir build/cleanup-msvc-<preset> \
  -R '^Carnivores2Tests' --output-on-failure --timeout 60
```

Each run passed `Carnivores2Tests` (94), `Carnivores2Tests.MenuLayout` (1), and
`Carnivores2Tests.FogSampling` (14); `Carnivores2Tests.UIText` passed 8/11. CTest
therefore reports 3/4 test executables passing and exits with code 8. The three
UI failures listed above remain visible. Rerunning the preserved baseline
`d62905a` x86 Debug UI executable reproduced the same failures and measurements:
38 versus 38 for both font-shrink checks, and 145 versus 144 / 290 versus 288
for the panel-height check. All six fresh configurations matched that baseline.

Additional checks:

- Fresh Microsoft compiler configure probes rejected an x64 GL Debug preset in
  an x86 environment, an x86 GL Debug preset in an x64 environment, and an x64
  GL Release preset overridden with `-DRENDERER=SOFT`. Each stopped at the
  intended architecture validation with configure exit code 1.
- All 12 visible presets resolve to the same effective architecture, cache
  settings and output paths as `d9b818b`. `base` imposes no Windows/x86 ABI.
  CMake accepts all configure/build/test presets.
- Generated GL builds exclude `renderasm.cpp`; SOFT includes it. Release
  `/arch:SSE2` appears only in x86 builds. PE machine types match each target.
- SHA-256 comparison confirmed all 8,348 pre-existing local runtime/evidence
  files, cached validation logs and CTest evidence files remained unchanged.

New logs and local validation scripts are retained under
`~/.cache/carnivores-phase1-cleanup-20260917/`; fresh CTest details are in
`build/cleanup-msvc-<preset>/Testing/Temporary/LastTest.log`. Existing runtime
copies, profiles and evidence were not modified. No new gameplay or asset smoke
test was run for this cleanup; the earlier developer playtest remains the
interactive evidence. Disk, network and GPU formats and gameplay behavior were
not intentionally changed. The deferred work and native Windows validation
limits above still apply.

## Native allocation-width slice (2026-09-17)

The `port/x64-memory-width` slice addresses R04–R07 on top of stable `main`
`67368b1`. It supersedes the allocation-width deferral above; other deferred
platform, serialization, GPU and packaging work remains separate.

### Runtime and transfer contracts

- `CheckedBytes2/3` now check native `size_t` overflow **before** multiplying.
  Three-factor zero products succeed regardless of the other factors; failure
  leaves the output unchanged. `CheckedTransferBytes2/3` additionally enforce
  `UINT32_MAX` for a single legacy/Win32 transfer.
- `_HeapAlloc` (all overloads), `_HeapAllocImpl` and `AllocDispatch` carry
  `size_t` bytes end to end. Heap flags remain `DWORD`. Object factories pass
  native `sizeof(T)`; array factories check `SIZE_MAX / sizeof(T)` before
  multiplying. Debug metadata already stores `size_t` and no longer receives
  a narrowed input. Cumulative `HeapAllocated/HeapReleased` use `uint64_t`,
  including in x86 builds; live leak totals remain `size_t`.
- Model/light storage, face and normal scratch, character scratch, snow arrays
  and mipmaps use checked native products. Allocation-site `DWORD` casts are
  removed. The face copy reuses its checked native byte count.
- Arena and leak diagnostics use `%zu` for native byte/count values. Arena log
  buffers have room for the longer values. Arena generations remain `unsigned`;
  these are epoch identifiers, not byte counts.
- Heap/deleter pairing, zero filling, tags, tracking, arena reset, constructor
  failure cleanup and smart-pointer storage invariants are preserved. Backing
  allocations still use Windows `HeapAlloc` and `VirtualAlloc`.

The final call-site audit intentionally retains these bounded quantities:

| Quantity | Retained limit and reason |
| --- | --- |
| `ReadExact`, `ReadModelExact`, `ReadRscExact`, `ReadFile`/`WriteFile` counts | `DWORD`; unchanged single-transfer Windows API. Animation payloads and resource-table byte products use explicit transfer checks before conversion. |
| Model vertex/face counts | Existing `1 << 20` content caps; also keep downstream signed indexing and fixed 16-/64-byte record reads safe. Object records remain capped at 1024. |
| Character animation storage | Native byte product, separate from the bounded file payload; one-frame duplication stays unchanged. Object animations still read their entire allocation in one bounded transfer. Frame counts retain the existing `INT_MAX / 256` fixed-point limit. |
| Model `TextureSize` | Nonnegative signed 32-bit file field. GL uses the existing 256-row normalization; SOFT rounds down with `>> 9`, then multiplies by 512, which cannot exceed the validated original value. These proven bounded byte values promote safely into the native allocator. |
| Picture pixels | `CheckedPictureBytes` checks native products and retains `INT_MAX` pixels for existing signed pixel indexing. For 16-bit pixels this accepts the same range as the old `UINT32_MAX` byte limit. BMP width remains at most 800; TGA dimensions remain 16-bit. Individual row reads still fit `DWORD`. |
| Snow | Existing nonnegative cumulative `1 << 20` element cap; runtime storage multiplication is now independently checked. Area tables themselves use existing fixed arrays. |
| Audio | Existing 16 MiB length cap and rounded-up 16-bit sample storage; signed OpenAL byte counts and PCM layout unchanged. |
| Fixed objects/textures | Native `sizeof(T)` or small compile-time byte constants; no count narrowing. |
| Fog/water records | Existing 255/256 entry caps and 20-/16-byte disk records, plus explicit transfer checks. |

No disk, profile, network or GPU record definitions, byte layouts, or serialized
layout tests changed. No content conversion or proprietary validation assets are
part of this branch.

### Build and asset-free test results

Validation used the installed Microsoft MSVC 19.44.35229 / SDK 10.0.26100.0
through Wine 11.17, with the same presets and local toolchain helper described
above. All six default builds passed, including their selected engine, menu and
test targets. PE machine types match the requested architectures.

| Configuration | Build | Individual tests |
| --- | --- | --- |
| Windows x86 GL Debug | Passed | 136/139 passed |
| Windows x86 GL Release | Passed | 136/139 passed |
| Windows x64 GL Debug | Passed | 139/142 passed |
| Windows x64 GL Release | Passed | 139/142 passed |
| Windows x86 SOFT Release | Passed | 136/139 passed |
| Windows x86 menu Release | Passed | 136/139 passed |

Every row passes all core tests (99 on x86, 100 on x64), allocator factory tests
in both plain and `MEM_DEBUG` modes (7 each on x86, 8 each on x64), the menu
layout test (1), and fog sampling (14). UI text remains 8/11, so CTest correctly
reports 5/6 executables passing and exit code 8. The three failing test names
are the documented baseline cases above. Rerunning the preserved untouched
`d62905a` baseline UI executable reproduced the same failures and measurements;
all six configurations matched: 38 versus 38 for both font checks, and 145 versus
144 / 290 versus 288 for the height check. UI code/tests were not changed.
The existing `Controls.cpp:309` C4805 warning also remains unchanged.

Arithmetic coverage includes zero and identity products, native and transfer
limits, one byte beyond `UINT32_MAX`, `SIZE_MAX * 2`, `(size_t(1) << 63) * 2`
on x64, both stages of three-factor overflow, normal asset sizes and picture
pixel limits. Factory probes intercept the allocation boundary to verify native
sizes (including values above 4 GiB on x64), flags, tags, debug forwarding,
zero-size ownership, matching frees and constructor exceptions without actually
allocating enormous buffers. Arena tests cover native-width rejection and x64
capacity logging without truncation, alongside the existing reset/ownership
tests. All existing serialized-layout fixtures remain unchanged and pass.

Commands are the cleanup validation commands above, using the same
`build/cleanup-msvc-<preset>` directories. New configure/build/test logs, baseline
comparison and final allocation-search evidence are retained separately under
`~/.cache/carnivores-memory-width-20260917/`. Individual test output remains in
each build directory's `Testing/Temporary/LastTest.log`.

### Bounded runtime validation

New independent copies of the user-owned Genesis Redux 1.1 content and profiles
were prepared under `~/Games/carnivores-memory-width-validation/`; the earlier
validation copies and original installation were preserved. Each received the
newly built engine and matching existing OpenAL DLL. All four x86/x64 GL
Debug/Release runs used headless Gamescope at 1024×768 and loaded area1 in observer
mode, with profile 0, dinosaur 1 and weapon 1. Each run:

- Loaded the hunt, models, textures and sounds and entered the game loop.
- Initialized OpenAL and confirmed its loaded module.
- Responded to the existing window-message and minimize/restore probes.
- Shut down normally through `WM_CLOSE`, including OpenAL cleanup, with exit
  code 0. Both Debug runs reported `No memory leaks detected.` and printed arena
  usage statistics (150 allocations, 5837 KiB used).

Logs, loaded-module evidence and result JSON are in that directory's `evidence/`
subdirectory. Automated screenshot capture again failed; no visual comparison is
claimed. These checks do not establish audible playback, exhaustive gameplay,
save/evacuation interchange, repeated-level runtime behavior, native Windows
execution or successful physical allocations above 4 GiB. Existing arena reset
unit tests do pass. Native Windows playtesting and the other deferred audit
findings remain follow-up work.

## Profile serialization slice (2026-09-17)

The bounded D07/D08 implementation now uses explicit shared C2 `.sav`/`.sab`
codecs with engine/menu adapters. All six builds pass; all serialization tests
pass, with only the same three Wine font baseline failures. The byte contract,
full test counts, controlled x86-menu → x86-engine → x64-engine → menu save checks,
expected runtime byte changes and limitations are recorded in
[PROFILE_SERIALIZATION.md](PROFILE_SERIALIZATION.md).
