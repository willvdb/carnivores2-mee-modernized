# Task: x64 portability audit

## Objective

Produce `docs/X64_AUDIT.md`, a repository-wide inventory of assumptions that tie MEE Modernized to a 32-bit Windows process. This task is analysis/documentation only: do not change gameplay behavior or begin the platform port.

Read `AGENTS.md` and `docs/PORTING.md` first. Their constraints are binding.

## Classification

Classify every known or suspected issue into one of:

- runtime memory layout
- disk/file-format layout
- network protocol layout
- renderer/GPU layout
- Win32 platform/API dependency
- software-renderer/x86 assembly
- third-party/dependency assumption
- unknown / requires investigation

For each finding, record:

1. file and symbol
2. current assumption
3. whether it is an external compatibility contract
4. likely Windows x64 impact
5. likely Linux x64 impact
6. proposed remediation
7. confidence / open questions

## Required search areas

- `sizeof(void*) == 4` and hard-coded structure-size assertions
- pointer/integer conversions and truncation
- architecture-sensitive uses of `long`, `DWORD`, `HANDLE`, `LPVOID`, `BOOL`, `WORD`, `BYTE`, and `SOCKET`
- allocation interfaces with 32-bit byte counts
- raw `sizeof(struct)` file/network reads and writes
- explicit binary record sizes, packing, alignment, and endianness
- `CreateFile`, `ReadFile`, `SetFilePointer`, `VirtualAlloc`, process heap APIs
- `WinMain`, the Win32 message loop, timers, sleep, focus, keyboard and cursor APIs
- WGL, `HWND`, `HDC`, `HGLRC`, `wgl*`, and `opengl32.dll`
- WinSock and Win32 threading
- `LoadLibrary`, `GetProcAddress`, DLL-name assumptions
- MSVC-only extensions, pragmas, secure CRT functions, and compiler flags
- x86 inline assembly and software-renderer-only paths
- CMake presets/generator settings that assume MSVC, Win32, or x86
- Windows path separators and case-insensitive filesystem assumptions
- launcher/menu dependencies on GDI/Win32

## Important distinctions

Do not assume every 32-bit-looking value should become 64-bit.

Legacy disk and network formats should generally stay fixed-width. The audit must distinguish a legitimate 32-bit compatibility contract from a runtime implementation assumption that prevents a 64-bit process.

Likewise, GPU vertex layouts with explicit byte sizes may be intentionally fixed and portable; document them rather than "fixing" them.

## Non-goals

- no SDL integration
- no Linux build target yet
- no file-format redesign
- no renderer rewrite
- no removal of legacy renderer code
- no broad cleanup refactors

## Suggested output structure

`docs/X64_AUDIT.md` should contain:

1. Executive summary
2. Build/toolchain assumptions
3. Runtime layout and pointer-width assumptions
4. File-format/serialization contracts
5. Renderer/platform boundary
6. Input/window/timing dependencies
7. Filesystem/path dependencies
8. Audio dependencies
9. Networking/threading dependencies
10. Menu/launcher dependencies
11. Software renderer/x86 assembly isolation
12. Third-party dependencies
13. Risk-ranked remediation plan
14. Proposed sequence for the first implementation PRs

End with a concise table of blockers for **Windows x64 first boot** and blockers that can safely wait until **Linux x64**.