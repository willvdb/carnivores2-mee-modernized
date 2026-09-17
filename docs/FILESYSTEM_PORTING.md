# Legacy content path compatibility

Source audit: stable main `5863c4e`, before this slice's production edits.
The scope is existing content lookup, not file formats, platform I/O or writable
user-data destinations. Modernize the engine around legacy content.

## Read/open inventory

The audit searched CreateFile, fopen, streams/open, filesystem probes/iteration,
GetFileAttributes, DLL loading and their callers throughout Hunt and Menu.
Categories: **content**, **user read**, **output**, **runtime**, **historical**.

| Boundary and callers | Category | Disposition |
| --- | --- | --- |
| Resources.cpp `LoadResources`: ProjectName + `.rsc`, then + `.map` | content | Resolve at each open; preserve ProjectName and extension construction. Embedded resource models, animation, textures, sky and PCM share the resolved RSC handle. |
| ScriptParser.cpp `LoadResourcesScript`: `HUNTDAT\\_res.txt` | content | Resolve before fopen. No parsing changes; parser-local external→area6 alias remains distinct from ProjectName. |
| ModelLoader.cpp `LoadCharacterInfo` | content | Resolve CAR open. CharacterLoader constructs creature paths from DinoInfo.FName, weapon/bullet paths from FName/BLName, and MULTIPLAYER avatar paths (Hitbox loads even in single-player). |
| ModelLoader.cpp `LoadModelEx` | content | Resolve standalone 3DF open; Hunt.cpp loads MOON/SUN2, COMPAS and BINOCUL. `LoadAnimation` reads the current RSC handle, with no separate filename open. |
| PictureLoader.cpp `LoadPictureTGA` | content | Resolve once for Interface loading art; Hunt pause/exit/trophy/map/FX art; Resources mapframe, flash, trophy/collection/score art; CharacterLoader call, bullet and chamber pictures (BFName/CFName). |
| PictureLoader.cpp `LoadPicture` (BMP) | historical reader | Retained public loader, no active caller found; use the same resolution boundary and existing synthetic tests. |
| SoundLoader.cpp `LoadWav` | content | Resolve standalone WAVs: Hunt sounds/steps/impacts, CharacterLoader calls and MULTIPLAYER/GUNSHOTS script SFXName. CAR/RSC embedded sounds share already-resolved handles. |
| GLShader.cpp `ReadTextFile` | content | External `shaders/*` text uses the resolver; preserve empty/error fallback. |
| Menu Resources.cpp `LoadResourcesScript` | content | Resolve `_menu.txt`, then existing `_res.txt` fallback. |
| Menu Resources.cpp `MakeOldAreaInfo` | content | Resolve description and MAP probes; thumbnail uses LoadPicture. Keep external.map first for slot 6, area6.map fallback and logical launch names. No new RSC requirement. |
| Menu Resources.cpp `LoadC2Maps` | content | Resolve areas directory and candidate MAP paths. Descriptor streams already receive actual enumerated native filenames. Preserve exact `.c2map` discovery extension filter, parser, candidate ordering, extension appending and duplicate rules. Descriptor pic/text use common loaders; rscfile remains ignored by existing policy. |
| Menu Resources.cpp `ReadTGAFile`, `LoadPicture`, `LoadMenuBackground` | content | Resolve TGA at ReadTGAFile, covering menu backgrounds, creature/hidden/weapon/accessory/area thumbnails and descriptor art. |
| Menu Resources.cpp `LoadText` | content | Resolve area/creature/weapon/accessory descriptions, observer/nightvision NFO and descriptor text; preserve missing-file fallbacks. |
| Menu Resources.cpp `LoadWave` | content | Resolve menugo/menuamb/menumov/type/typego WAVs; preserve optional failures. |
| Menu Menu.cpp `MenuEventStart`: RAW hit-map stream | content | Resolve the selected menu hit map. |
| Trophy.cpp LoadTrophy/LoadTrophy2; Menu TrophyLoad and registration header streams | user read | Leave existing relative trophyNN.sav/.sab paths unchanged, paired with current writes. |
| EngineInit.cpp LoadConfig; Menu LoadConfig and SaveConfig's old-line read | user read | Leave executable-directory/cwd lookup and config.cfg preservation unchanged; not legacy asset reads. |
| Trophy.cpp SaveTrophy/SaveTrophy2; Menu TrophySave/TrophyDelete; engine/menu SaveConfig | output | Preserve destinations, creation and deletion policies. |
| Resources.cpp SaveScreenShot/CreateLog; Debug/Log.cpp; GLPerf.cpp; Menu Hunt2.cpp log | output | Screenshots, render/carnivor/menu/performance logs remain native output paths. |
| Engine/menu config GetModuleFileName/GetFileAttributes; OpenAL_Loader, Audio_DLL and GLRenderer LoadLibrary; menu LaunchProcess | runtime | Module/config location, DLL and executable selection retain Windows semantics; no asset resolver. |
| Commented billboard reads, menu slider picture calls, `_iceage` registration/profile branches | historical | Inactive C2 paths; no independent content open requiring migration. |

## Required lookup contract

Both slash styles are separators. Resolve component by component, preserving
actual disk spelling. An exact component wins over case-folded siblings; without
an exact match, accept exactly one ASCII case-insensitive match and reject
collisions deterministically. No extension guesses, punctuation/space trimming,
partial matching, unrelated-directory searches, tree indexing or global cache.

Relative requests use the game's working directory (an explicit native root is
also useful for portable tests). Preserve `.` and `..` traversal, including
ordinary symlinks; do not lexically collapse `link/..`, canonicalize a tree or
impose a content sandbox. Resolution does not mutate the logical project name,
scripts, assets or writable destinations. Windows drive/UNC semantics need only
remain available on Windows; they are not Linux mount mappings.

Errors retain the logical request, failed component and containing directory,
and distinguish ambiguity from missing paths and filesystem errors. The existing
reader still owns open/read behavior and required versus optional failure policy.


## Implemented API and boundaries

`Shared/LegacyPath.h` / `.cpp` expose `LegacyPath::Resolve(logical, root = ".")`.
A successful `Result` contains the existing native path with actual component
spelling. Failure has an empty path, an error category, original request,
component, containing directory and optional filesystem error code; `Message()`
formats these for the caller. The resolver accepts files and directories because
menu discovery needs an existing directory. It neither opens nor creates files.

Each directory is enumerated only for the requested component. Exact native
spelling wins even if case-folded siblings appeared earlier in iteration; a
non-exact match is accepted only after the full scan proves it unique. Comparison
folds only ASCII A..Z. Non-ASCII code units are compared literally; this adds no
Unicode case-folding or Windows narrow-code-page policy. Enumeration on Windows
also recovers the actual name instead of trusting case-insensitive `exists` to
identify exact spelling. Containing directories must permit enumeration. There
is no recursive index, cache, canonicalization or change to directory permissions.
Like ordinary resolve-then-open code, this is not an atomic file identity or
security boundary: concurrent renames can make a later open fail.

Repeated separators work. Empty requests and embedded NULs fail explicitly.
`.` and `..` remain in the returned path so symlinks retain native host traversal;
Windows and POSIX can interpret link/.. differently. An intermediate regular
file cannot stand in for a directory, trailing separators require a directory,
and dangling symlinks fail. The root argument is a native working directory,
not a confinement boundary: parent traversal and absolute requests are allowed.
POSIX absolute paths work. Windows drives, drive-relative names and UNC roots
are left to Windows std::filesystem; Linux reports an unsupported Windows root
instead of inventing a mount mapping. No drive/UNC reference occurs in the
available content; live network-share lookup was not tested. Windows device-name,
8.3 alias and trailing-dot/space emulation are outside this ASCII filename contract.

`Hunt/Loaders/LegacyAssetPath.h` resolves required engine names and calls the
existing DoHalt on resolution failure. The loaders still use CreateFile with
unchanged access/sharing flags, or fopen for scripts. GLShader uses the shared
Result directly and retains its log-and-empty-string error path. The MAP open
error now includes its logical filename even if opening fails after resolution.

`Menu/LegacyAssetPath.h` logs resolution errors and returns an empty native path
on failure; streams/fopen retain their existing failure/fallback behavior. The
original unresolved request is never retried directly. Optional missing art/text
is still optional. `MakeOldAreaInfo` still probes MAP only, tries external before
area6, and retains logical project names. `.c2map` iteration opens the exact
native filename supplied by the resolved directory iterator; it needs no second
case lookup. Its existing lowercase `.c2map` discovery filter, candidate list,
project-name extraction, ignored rscfile and parser behavior are unchanged.

## Native test surface

The `linux-portability-tests` configure/build/test preset sets
`CARNIVORES_PORTABILITY_TESTS_ONLY=ON`. CMake returns after building LegacyPath,
GoogleTest 1.12.1 and the portable shared tests, before any Win32 game/menu sources
or renderer libraries. This is **not a Linux game build**. The existing Windows
preset names/settings and six-row CI matrix are unchanged. A separate Ubuntu CI
job runs the new preset with GCC and Clang.

```sh
cmake --preset linux-portability-tests
cmake --build --preset linux-portability-tests
ctest --preset linux-portability-tests
```

For an offline build, supply `-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=<existing-source>`
at configure time. Local runs reused the Windows build's fetched source tree;
no dependency update was made. Clang and sanitizer builds used separate binary
directories, `-DCMAKE_CXX_COMPILER=clang++`, or
`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"`.

## Validation (2026-09-17)

Native Linux GCC 16.2.1 and Clang 22.1.8: **41/41 pass** each (14 path tests,
27 unchanged shared serialization codec tests). GCC ASan/UBSan including leak
checking: **41/41 pass, no diagnostics**. LeakSanitizer required a rerun outside
sandbox tracing; the initial tracing limitation was not a test assertion failure.
These runs are on CachyOS, not a claim that the new Ubuntu CI job has executed.

Path fixtures create private temporary trees, testing both slashes and mixed /
repeated separators, every directory component's case, exact priority, file and
directory ambiguity, no search-ahead through colliding directories, useful missing
context, dot/parent components, normal symlinks and link/.., dangling links,
spaces/punctuation/digits, literal extensions, relative/default/native absolute
roots, trailing separators, empty/NUL inputs, non-ASCII literal bytes, unsupported
Windows roots on POSIX, and changes between lookups (no stale cache). Colliding
siblings and symlinks are skipped only when the host cannot provide them; all
14 execute without skips on this Linux filesystem.

Three additional tests call production Menu Resources.cpp: external/area6
precedence with mixed-case directory/file names and no new RSC requirement;
_MENU precedence and _RES fallback; and descriptor MAP/text references,
missing-MAP skipping and duplicate retention. Existing production model,
resource/map, image/audio, profile and layout tests remain intact.

Microsoft MSVC 19.44.35229 / SDK 10.0.26100.0 under Wine 11.17, using the six
existing presets and toolchain in [WINDOWS_BUILDS.md](WINDOWS_BUILDS.md):

| Configuration | Full default build | Individual tests: pass / skip / fail |
| --- | --- | --- |
| Windows x86 GL Debug | Pass | 228 / 2 / 3 |
| Windows x86 GL Release | Pass | 228 / 2 / 3 |
| Windows x64 GL Debug | Pass | 231 / 2 / 3 |
| Windows x64 GL Release | Pass | 231 / 2 / 3 |
| x86 SOFT Release | Pass | 228 / 2 / 3 |
| x86 menu Release | Pass | 228 / 2 / 3 |

The only skips are the two case-collision tests because Wine's Windows lookup
reports the fixture filesystem as case-insensitive. Both execute on native Linux.
The only failures are the unchanged UIText LongNameShrinksTheFontToFitThePanel,
SingleLongRowIsClampedEvenWithoutPairing and FiveRowBlockFitsThePanelHeight.
The preserved d62905a baseline was rerun and matches every configuration's
38 versus 38 (twice), 145 versus 144 and 290 versus 288 measurements. CTest keeps
reporting exit 8, 10/11 executables passing; no failures are suppressed. The full
matrix ran before the final three menu tests were added; the changed menu test
target was then rebuilt and all nine tests passed in all six configurations.
PE architectures match the selected targets; no new build warning was emitted.

### Independent casing inventory and corpus probe

Fresh copies outside the repository include both user-owned Genesis Redux trees:
`~/Games/carnivores-genesis-redux/game` and
`backups/before-1.1-20260914-012202/game`. Inventory records actual directory/file
spelling, file sizes and SHA-256 independently of the C++ resolver. Original and
copied hashes were rechecked unchanged. Each copy includes HUNTDAT plus the
MULTIPLAYER content read through the same loaders. Both have 14 directories and
no ASCII case-colliding siblings; ambiguity is covered by synthetic fixtures.

| Kind | Current: original extension casing | Older backup |
| --- | --- | --- |
| RSC | 6 .RSC + 3 .rsc | Same |
| MAP | 6 .MAP + 3 .map | Same |
| CAR | 21 .CAR + 99 .car | Same |
| 3DF | 4 .3DF | Same |
| TGA | 62 .TGA + 28 .tga | 62 .TGA + 26 .tga |
| WAV | 43 .WAV + 30 .wav | Same |
| Other | 7 RAW, 18 TXT/txt, 10 TXM, 10 TXU, 1 NFO | Same except no NFO |
| Total | 351 files | 348 files |

The native probe links the production Shared/LegacyPath.cpp. It checks every
inventoried file using original/lower/upper/alternating case, each with forward,
backward and mixed separators (12 requests per file). It separately extracts
quoted file references from _RES.TXT and prefixes them as CharacterLoader does:
HUNTDAT for creatures; HUNTDAT/WEAPONS for weapon/bullet/picture fields;
MULTIPLAYER/GUNSHOTS for gunshot fields. Production source literals, menu script
prices/AI/weapon counts, project extension construction, flash and call loops
supply additional requests. This is request extraction and lookup validation,
not execution of every conditional script branch or a replacement parser.

| Copy | Inventory requests resolved | Production-derived requests resolved | Expected missing requests | Total |
| --- | ---: | ---: | ---: | ---: |
| Current | 4212 | 3624 | 228 | 8064 |
| Older backup | 4176 | 3588 | 252 | 8016 |
| Case-scrambled current | 4212 | 3624 | 228 | 8064 |

All **24,144** outcomes match the independent inventory, and every success
returns the actual on-disk spelling. Production coverage contains 321 distinct
logical requests for current content and 320 for backup (302/299 exist); these
are expanded through the same 12 spellings. Expected missing paths are the
external.map first probe (area6 exists), hidden dinosaur pictures, equip6 art,
several accessory descriptions and type/typego WAVs; backup additionally lacks
nightvision art/text. They fail rather than resolving a similar file. Existing
menu fallbacks are preserved and covered by production tests/runtime checks.

The scrambled copy alternates capitalization of every directory and filename
component, without changing any file bytes. Current and scrambled trees return
identical content hashes for the same logical requests. Each tree's complete
request batch took roughly 0.47–0.50 seconds locally; no cache was warranted.
This bounded corpus has no BMP, `.c2map`, case collision or independent pristine
stock/other total-conversion installation. Synthetic tests supply those path
edge cases and descriptor behavior. No Genesis-specific resolver rule exists.

### Runtime regression and final scope

Release x86 GL, x64 GL and x86 SOFT ran under headless Gamescope at 1024x768 on
fresh copies with matching existing OpenAL DLLs. All loaded AREA1 RSC/MAP,
creature/weapon CARs, 3DFs, TGA and WAV assets, entered the game loop, initialized
OpenAL, responded to window probes, displayed weapon/HUD/compass and the area
map, evacuated, logged Trophy Saved and TrophyB Saved, shut down audio and exited
0. Compositor captures were inspected; GL and SOFT scenes show intact terrain,
models and art. The save/evacuation path does not emit the WM_CLOSE-specific
normal-shutdown log. Existing copied-profile neutral mouse sensitivity and V
weapon binding were retained; only copied writable user data changed.

The x86 Release menu loaded its profile, scripts, backgrounds, hit maps, icons,
descriptions and WAVs, visited hunt/options screens, launched AREA1 through
`v_gl.ren` with the existing logical `huntdat/areas/area1`, then returned after
both engine saves, reloaded the profile and exited 0. Missing optional assets
listed above remain logged with their normal fallbacks. Menu captures show its
backgrounds, lists and art; the attempted launched-hunt compositor frame still
showed the menu, so direct engine captures provide the gameplay visual evidence.

One pre-existing launcher diagnostic surfaced during this round trip:
Menu/Hunt2.cpp `LaunchProcess` tests and prints the BOOL returned by
GetExitCodeProcess as if it were the child exit code, so a successful query logs
"error code: 1". Stable main has the identical code. No child exit code was
independently sampled in that menu run; direct engine runs independently returned
0. This diagnostic bug is deferred as an unrelated launcher fix, not concealed
as a filesystem regression or fixed opportunistically here.

Native Windows execution, subjective audible quality, exhaustive gameplay/mod
coverage and live UNC shares remain unverified. These are MSVC Windows binaries
executed under Wine; Linux validation covers only shared utilities/codecs. No
asset file was renamed or changed in the original or ordinary runtime copies;
capitalization changes exist only in the separate scrambled validation tree.

A final repeat of the open/probe/caller searches found no unresolved logical
legacy asset read bypass. The only direct content stream is the `.c2map` filename
already supplied by iteration over a resolved native directory. Remaining raw
opens are the user reads, outputs and runtime operations enumerated above:
profiles/registration/config, saves/config rewrite, screenshots/logs, DLLs and
executables. Historical/inactive branches remain characterized rather than
rewritten. ProjectName, external aliasing, script parsing, content formats,
renderer/gameplay, audio, network and user-data destinations are unchanged.

Local evidence is under `/tmp/carnivores-path-validation/`: `evidence/` contains
full Windows logs and test counts, baseline-font comparison, Linux/compiler/
sanitizer logs, actual casing trees and SHA-256 inventories, every corpus request
and result, and runtime/menu logs/captures. `tools/` holds the local reproduction
probes/scripts. The corpus and automation are validation-only and not committed;
this temporary location is not a permanent asset archive.

There is no unresolved filesystem blocker for this slice. The next phase is a
thin platform/file-I/O boundary and native Linux x64 GL compilation bring-up:
preserve these logical lookup rules while isolating Win32 file handles, window/
WGL/GDI dependencies and other platform services. Keep write-location policy,
networking, renderer redesign and menu redesign as separate work.
