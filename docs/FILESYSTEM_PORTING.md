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
| Menu Resources.cpp `LoadArea` | content | Resolve description and MAP probes; thumbnail uses LoadPicture. Keep external.map first for slot 6, area6.map fallback and logical launch names. No new RSC requirement. |
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
