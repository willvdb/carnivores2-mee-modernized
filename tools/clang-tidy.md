# Clang-Tidy

The checked-in `.clang-tidy` configuration applies to project-owned C/C++
translation units. Vendored `deps/`, generated build trees, install trees, and
GoogleTest sources are excluded by `tools/run_clang_tidy.ps1`. The runner reads `CARNIVORES_ARCH` from the selected build cache to preserve
the x86 or x64 ABI. Linux compilation databases retain their native target.

Configure a database with the matching MSVC environment (or a Linux preset):

```powershell
cmake --preset windows-x64-gl-debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Analyze selected changed sources:

```powershell
powershell -ExecutionPolicy Bypass -File tools/run_clang_tidy.ps1 `
    -BuildDir build/windows-x64-gl-debug `
    -Files @('Hunt/Game/Hunt.cpp', 'Hunt/Loaders/ScriptParser.cpp')
```

Analyze every project translation unit explicitly:

```powershell
powershell -ExecutionPolicy Bypass -File tools/run_clang_tidy.ps1 `
    -BuildDir build/windows-x64-gl-debug `
    -AllProjectFiles
```

MSVC warnings use an explicit project-owned `/W4` baseline. Clang-tidy keeps
the full configured check set advisory except for the high-confidence lifetime
and memory checks listed under `WarningsAsErrors`; this is the current measured
legacy baseline. `/WX` is available through `-DC2_WARNINGS_AS_ERRORS=ON`, but
remains opt-in. `/permissive` is still retained for compatibility and remains
target-scoped; replacing it with `/permissive-` is a later focused cleanup.

The upstream diagnostic count is not a baseline for this fork. Run against the
selected platform database and record actual diagnostics; no broad cleanup is
part of this integration.
