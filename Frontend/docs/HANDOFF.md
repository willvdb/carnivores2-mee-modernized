# Frontend backend foundation handoff — 2026-09-18

The working deliverable is an independent CLI/backend, with JSON hunter and
instance persistence, conservative catalog projection, lossless native-state
inspection/import, structured dry-run launch requests, and simulated return
refresh. Native files remain authoritative and are never rewritten by this code.

## Branch and concurrency boundary

* Branch: `frontend/backend-foundation` in its own worktree at
  `/home/willvdb/code/games/carnivores2-frontend`.
* Base: `ba556538ac97cfe8ec3035bed46740ce70d13737`, latest fetched safe `main`
  when the frontend branch was created.
* During work, `origin/main` advanced to
  `a2cfec8` (display-targeting follow-up). This branch retains its original base;
  no engine branch was rebased, merged, rewritten or checked out by this task.
* Initial open PR list was empty. Active/recent branch diffs, worktrees and
  commits identified `port/display-targeting` and `fix/sdl-x11-mode-leak` as active
  engine work. Remote status/PR overlap was rechecked before implementation phases
  and before delivery; no frontend overlap was found.
* All branch changes are additive under **`Frontend/`**. No existing engine, Menu,
  codec, root build/CI or test files changed. The original shared worktree and its
  untracked game logs were left alone. Use a merge-base comparison such as
  `git diff origin/main...frontend/backend-foundation`, not a two-tip comparison
  that also shows engine work landed after this branch's base.

The final delivery head and complete commit SHAs are recorded in the response;
`git log --oneline ba556538..frontend/backend-foundation` reproduces the sequence.
Implementation commits, in order:

1. `b8de3d4` docs: define hunter state and portable frontend boundaries
2. `f01530d` frontend: add versioned hunter identity store and recovery
3. `a707c6c` frontend: add coherent expedition discovery and stable instance registry
4. `542a5e9` frontend: inspect and associate native state sets without mutation
5. `9ec5507` frontend: project ordered legacy catalogs with provenance and ambiguity
6. `c69df97` frontend: add CLI hunt planning and authoritative state refresh
7. `12c7d40` frontend: tighten manifest validation and separate engine evidence
8. `3fd3642` frontend: reject content changes during revision inventory
9. `4cf2d40` frontend: report unknown profile companions before managed import
10. Delivery documentation and local audit replay tool follow these commits.

## Added files

| Files below `Frontend/` | Responsibility |
| --- | --- |
| `frontend.py` | CLI entry point and command orchestration |
| `CMakeLists.txt`, `.gitignore` | Standalone optional build/test setup |
| `profile_probe.cpp` | Read-only JSON adapter over `Shared/LegacyProfile.h` |
| `lodge/__init__.py`, `lodge/store.py` | Versioned manifest, UUID hunters, selection/rename/archive, locking/recovery |
| `lodge/discovery.py` | Coherent roots, nested discovery, stable registration, fingerprints/engine evidence, relocation |
| `lodge/profiles.py` | Native slot/state-set discovery, codec inspection, explicit referenced/managed associations, refresh |
| `lodge/catalog.py` | Ordered raw observations, dialect evidence, projected catalogs and reference diagnostics |
| `lodge/launch.py` | Pure launch intent/validation and simulated return refresh |
| `tests/support.py`, `tests/test_store.py`, `tests/test_discovery.py` | Synthetic fixtures and identity/discovery/recovery tests |
| `tests/test_profiles.py`, `tests/test_catalog.py`, `tests/test_launch.py` | State safety, content ambiguity, launch intent and CLI integration tests |
| `tools/inspect_corpus.py` | Optional read-only replay against user-owned extracted audit material |
| `README.md`, `docs/STATE_MODEL.md`, `docs/BOUNDARIES.md`, `docs/AUDIT_DECISIONS.md`, `docs/HANDOFF.md` | Usage, design contract, audit evidence and handoff |

## Validation and demonstrated behavior

**34 focused Python tests passed**, with the actual C++ codec helper supplied by
CTest. No existing tests were changed or weakened. The engine test suite was not
rebuilt because no engine/build inputs changed; the new target is standalone.

* GCC 16.2.1 Debug C++17 build and complete frontend suite: passed.
* Clang 22.1.8 Debug with AddressSanitizer + UndefinedBehaviorSanitizer and the
  complete frontend suite: passed, no sanitizer findings.
* Optional `c2-frontend` build target / CLI `--help`: passed.
* Python byte-compilation and `git diff --check`: passed.
* CLI integration exercised persisted hunter/instance listing, dry-run selection,
  simulated return, host preferences, and machine-readable failure handling.
* Tests cover schema/version errors, duplicate JSON keys, unknown-field retention,
  corrupted/missing main manifests, explicit backup recovery, stale writer locks,
  failed atomic replacement, malformed references and blocked writable manifests.
* Discovery tests cover nested roots, partial overlays, missing installations,
  clones versus explicit moves, canonical registration, managed ownership bounds,
  content changes, independently changed engine binaries, mutable exclusions,
  traversal/case/symlink rejection and changes during fingerprinting.
* State tests cover SAV/SAB pairing, orphan rooms, non-root packaged candidates,
  missing companions, exact bytes, registration mismatch, unsupported layout and
  Ice Age families, source drift, ambiguous filenames, managed forks, referenced
  exclusivity, path traversal and unclassified companion preservation.
* Catalog/launch tests cover duplicate AI, category licenses, blank/instruction
  labels, source lines, nested scripts, surplus prices, extra physical maps,
  conflicting equipment descriptions, older/newer dialect separation, observer
  empty selection, dry-run argv, revision invalidation and refreshed native score
  after a simulated external update.

Local read-only evidence beyond synthetic tests:

1. Replayed **19 edition trees** from `/tmp/carnivores-audit-materials`; all
   selectable-license and weapon counts matched the supplied audit. They separate
   into eight newer-MEE, four older-MEE and seven classic-syntax observations.
   These are partial audit trees, not newly certified installed expeditions.
2. Copied **nine packaged native files from five archives** into a separate
   temporary directory and inspected them through the real codec. Every known
   layout round-tripped exactly in memory. Orphan Mandibles room and Triassic
   MODDAT save remained explicitly diagnosed. All inspected bytes stayed unchanged.
3. Registered a complete locally owned Genesis validation installation:
   **344 content files, 768,073,101 bytes**, fingerprint
   `9c6fc5221744ad8e9a74689d308ba572b6aefe6cd6c317e030e5774757c2bf65`.
   Projected eight advertised area slots, nine licenses, eight weapons, four native
   priced accessory slots and nine physical maps (including trophy material).
   Created a frontend-only hunter and independent lossless managed save snapshot,
   constructed a blocked dry run, and simulated return to unchanged authoritative
   state. Both original native files had identical before/after hashes. Origin was
   deliberately `unknown`; validation did not claim personal progression.

Local logs/results (not repository assets):
`/tmp/carnivores-frontend-build`, `/tmp/carnivores-frontend-clang`,
`/tmp/carnivores-frontend-tests.log`, `/tmp/carnivores-frontend-corpus.json`,
`/tmp/carnivores-frontend-native-report.json`,
`/tmp/carnivores-frontend-real-report.json`. The real smoke-test store is recorded
in the last report; no user-default frontend store was populated.

## Deliberate prototype boundaries

* No actual hunt/launcher/Dinopedia execution; no frontend hide/resume behavior
  beyond a simulated `resume-and-refresh` result. `process_launch_allowed` is
  always false. Candidate argv is not a certified command.
* No native profile creation, slot allocation, rename/delete, progression writes,
  options normalization, automatic migration or copy synchronization.
* No globally enforceable legacy writer lock. Metadata lock is local to this
  frontend store. Stable double reads do not prove a historic SAV/SAB transaction.
* No automatic edition/family/build certification. Engine bytes are distinct from
  content revision; edition labels and dialect hints are provenance-bearing user
  assertions. No binary-string heuristic turns classic Triassic into ordinary C2.
* No equipment grants or authoritative custom meanings. Priced native slots and
  conflicting description candidates are preserved. Native score is observable;
  rank/unlocks/scoring/settlement policies remain unverified and never mutate state.
* Text parsing is a bounded observation grammar, not a clone of every historical
  substring parser. Unbalanced/ambiguous scripts keep data and diagnostics. Latin-1
  text is a reversible byte projection, not a verified language/code-page choice.
* No `.c2map` execution contract or explicit-area semantic adapter. No universal
  species pool, trophy merge, visual toolkit, artwork generation or final mod
  extension syntax. No native Windows frontend runtime validation yet.

## Shared extractions and overlap avoided

Read-only seams studied: shared lossless codec; Menu profile/price/catalog/launch
logic; engine profile adapters and command-line handling. All existing files were
left in place. Hot engine paths avoided include `Hunt/Game/CommandLine.cpp`,
`Hunt/Game/Interface.cpp`, display-selection/preferences, `Hunt/Core/GameState.h`,
`Hunt/Platform/*`, root CMake/CI and SDL patch/test infrastructure.

Later independent extractions should isolate script observation, edition-specific
price/rank/accessory policy, bounded selection-to-mask mapping, structured process
requests, content-root/state-root separation, session locking/backup/recovery and
revision-aware trophy metadata. They should exclude GDI, Win32 messages, hit maps,
UI globals, navigation and lodge presentation.

Engine/menu debt intentionally untouched is listed in `AUDIT_DECISIONS.md`: Far
North vector bounds, duplicate-AI art mapping, older-MEE dispatch gaps, inconsistent
rank policy, sequential state writes/orphan deletion, slot filename divergence,
`.c2map` resource handling and survival slot-zero assumptions. Additional static
parser balance diagnostics are recorded without modifying third-party content.

## Exact next bounded milestone

**Implement one pinned current-MEE/Genesis session adapter, still backend-only.**
Pin a specific modern-engine build and content revision; encode/test that edition's
selection/equipment/rank/observer policy; define an isolated writable session
workspace and prelaunch/return whole-state journal; exercise child-process
lifecycle with a synthetic process fixture, including failure/partial writes.
Only then validate an observer launch against an independent managed snapshot.
Do not expand to older MEE, Ice Age, survival or arbitrary mod certification in
that milestone. If content/state separation requires touching active engine files,
land it as a separately coordinated engine seam after those agents finish.

Before an actual native launch, human/design review should settle the intended
pinned menu-versus-engine policy when they disagree, approve the session workspace
contract, and choose a disposable validation profile. No further visual or toolkit
decision is needed for that work. Packaged example saves must remain explicitly
classified. Automatic cross-revision state/trophy migration remains out of scope.
