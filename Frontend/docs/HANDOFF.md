# Frontend backend foundation handoff — 2026-09-18

The working deliverable is an independent CLI/backend, with JSON hunter and
instance persistence, conservative catalog projection, lossless native-state
inspection/import, structured dry-run launch requests, and simulated return
refresh. Native files remain authoritative and are never rewritten by this code.

## Branch and concurrency boundary

* Published branch: `frontend/backend-foundation`. The original frontend worktree
  at `/home/willvdb/code/games/carnivores2-frontend` was left at `800e104`.
  The focused review used `/home/willvdb/code/games/carnivores2-frontend-review`
  on local branch `frontend/backend-foundation-review`, pushed to the published
  frontend branch without changing anyone else's checkout.
* Original base: `ba556538ac97cfe8ec3035bed46740ce70d13737`; pre-review head:
  `800e104f51c284b622fd306588c143fde83aa63c`.
* Current main synchronized into this branch:
  `a2cfec8ef3590c6c8d56d16c7a69e4c0e5327f5a`, via merge `fd85a10`.
  Fetches before implementation and synchronization confirmed the same remote
  heads. No rebase or force push: the published frontend stack was preserved,
  including the SHAs in the existing frontend checkout. Main was not updated.
* Initial open PR list was empty. Active/recent branch diffs, worktrees and
  commits identified `port/display-targeting` and `fix/sdl-x11-mode-leak` as active
  engine work. Remote status/PR overlap was rechecked before implementation phases
  and before delivery; no frontend overlap was found.
* All frontend changes are under **`Frontend/`**. Outside it, the merged tree is
  byte-identical to current main: recent engine work was retained in full, with
  no conflicts. No existing engine, Menu, codec, root build/CI or test files were
  edited by this pass. The original shared worktree and its
  untracked game logs were left alone. Use a merge-base comparison such as
  `git diff origin/main...frontend/backend-foundation`, not a two-tip comparison
  that also shows engine work landed after this branch's base.

The final delivery head and complete commit SHAs are recorded in the response;
`git log --first-parent --oneline ba556538..origin/frontend/backend-foundation`
reproduces the frontend sequence, including the main synchronization merge.
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

## Focused review follow-up

The three fixes are separate commits, preserving the original stack:

1. `c66e473` frontend: separate content recognition from engine evidence
2. `e06b721` frontend: preserve managed ownership across managed relocations
3. `d72a1f9` frontend: retain engine evidence reviews across relocations
4. `fd85a10` Merge current main into frontend backend foundation
5. This follow-up handoff documentation follows validation.

Coherent content without a bundled engine now registers and projects catalogs;
engine absence remains an advisory diagnostic and separate capability. Existing
script/menu/non-trophy MAP/RSC checks remain in force. Recognition does not
certify that a map decodes or an expedition can run. HUNTDAT fingerprinting is
unchanged; presentation-only versus semantic revision remains future design work.

Managed registration persists `managed_root: {path, path_flavor}` independently
of installation location. Explicit moves within that existing canonical root
preserve management; outside it relinquish management; registered installs stay
registered. Missing/foreign/retargeted roots and older records lacking context
fail conservatively without guessed ownership. No reconciliation command exists
yet for these cases. Root locators are not portable machine/filesystem identities.

Relocation still requires exact content equality. Different destination engine
evidence, including engine removal/addition, appends `engine_relocation_reviews`
with `status: required`, both evidence sets, both locations and time. Original
`engine_evidence` stays intact. Pending reviews survive refresh and later moves,
even back to matching engine bytes; they block candidate argv. Accepting a new
engine baseline is deferred. All process launches remain disabled.

Post-merge validation:

* **55/55 tests** under GCC 16.2.1 Debug and Clang 22.1.8 ASan/UBSan, with leak
  detection and halt-on-error enabled; no skips or sanitizer findings. The
  original 34 tests are retained; 21 tests cover the review findings.
* Standalone `Frontend/` configure/build, `c2-frontend` CLI help target, Python
  3.14.7 byte-compilation and `git diff --check`: passed.
* All 19 edition reports exactly match the previous local corpus replay.
  All 26 discovered partial audit roots still fail coherent recognition.
* Nine packaged native files again round-trip exactly through the real codec.
  Read-only Genesis inspection retains the 344-file, 768,073,101-byte revision
  below and all catalog counts. Dry run remains blocked and simulated return
  reports unchanged state. All 11 checked native files retain identical hashes;
  the previous smoke-test store also remains byte-identical.
* All 22 files changed on main since the original base were inspected for scope;
  local/remote engine branch comparisons and current worktree status have no
  frontend overlap. Open PR lists were empty at both checks. Engine tests were
  not rerun: their files are exactly current main, and this is a standalone target.

Follow-up artifacts are local, not redistributed content:
`/tmp/carnivores-frontend-review-gcc-tests.log`,
`/tmp/carnivores-frontend-review-clang-tests.log`,
`/tmp/carnivores-frontend-review-corpus.json`,
`/tmp/carnivores-frontend-review-native-report.json`, and
`/tmp/carnivores-frontend-review-overlap.json`.

Changed follow-up files: `README.md`, `docs/STATE_MODEL.md`, `docs/HANDOFF.md`,
`lodge/discovery.py`, `lodge/store.py`, `lodge/launch.py`,
`tests/test_discovery.py`, `tests/test_launch.py`, and new
`tests/test_relocation.py`, all under `Frontend/`.

Ready for human review toward merging the foundation. No merge to main, session
adapter, process launch or UI work is part of this pass. Native Windows frontend
execution is still unvalidated; path-flavor rules have portable unit coverage.

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

Initial implementation: **34 focused Python tests passed**, with the actual C++ codec helper supplied by
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
# Subsequent session milestone

The foundation below is now merged through PR #10. The bounded follow-up is
documented in [SESSION_HANDOFF.md](SESSION_HANDOFF.md): controlled synthetic
process sessions, durable return candidates, and a pinned Genesis observer
policy blocked on a precisely described engine seam.
