# Hunt-loop GUI

## API audit

The public `c2/frontend/play_loop.hpp` calls the existing implementation directly.
It adds no codecs, schemas or CLI dispatch. UUIDs remain identities. Complete
backend evidence accompanies typed fields as display-only text, never parsed by
presentation code. Exceptions retain the backend diagnostic messages.

| Handoff operation | Existing public API | Added boundary |
| --- | --- | --- |
| Hunter / expedition reads | `Store::read`, `Manifest::hunters/expeditions/active_hunter` | reused; viewing selection is local |
| Association selection | none | `play_loop::associations` includes origin, ownership, authority, current generation |
| Catalog | `catalog::project` | reused |
| Plan | pure `planning::Selection`, policy and launch evaluation | `Client::plan` calls fresh store-backed `plan_hunt` |
| History | `Manifest::resolve_generation` | reused |
| Explicit schema upgrade | none | `Client::upgrade` |
| Prepare | none | `Client::prepare` |
| Run | none | `Client::run`, caller-owned atomic cancellation; returns before reconciliation |
| Inspect / reconcile | none | `Client::inspect/reconcile` |
| Preview | none | `Client::preview`, typed allowed/digest/predecessor |
| Accept | none | `Client::accept`, repeats existing locked validation |
| Recovery | none | `Client::recover/recover_acceptance` |

Client defaults to read-only. Mutations require an existing manifest and explicit
writable construction. No read upgrades metadata. Production clients always use
production policies; the private seam is reserved for owned asset-free fixtures.
The CLI is unchanged and continues to use its existing implementation.

## Screen flow and ownership

Lodge → Expedition Console → select an association by UUID → choose catalog area,
license, weapon and time → Validate loadout → Prepare fresh session → Run prepared
session → Return review → Inspect → Preview acceptance → Accept reviewed candidate.
Decline retains the candidate without advancing authority. After acceptance,
Prepare fresh session creates a new UUID pinned to the accepted generation.

The lodge layout and artwork slot filenames are unchanged. The Console retains
its expedition list and local hunter viewing selection. The association selector
joins the two backend UUIDs explicitly; duplicate display names are not identities.
Origin, ownership, authority, current generation, session UUID and pinned generation
remain visible. Catalog labels are escaped; observations and identities bind as
text, never interpreted RML. Backend policies supply eligibility and all native
semantics. The plan is validated intent only, never launch authorization.

Return review is a modal with Back/Close focus restoration. It shows candidate or
quarantine state, raw before/returned score and rank when observed, unavailable
versus observed-empty distinctions, diagnostics, workspace location, and complete
backend evidence as scrollable text. Preview shows its exact predecessor and
candidate digest; acceptance uses that retained preview and revalidates under the
backend writer lock. Process completion never calls acceptance. There is no force
accept and no retargeting of a prepared session after a generation advances.

The retained-session UUID field opens historical or blocked sessions without
launching. It clears the preparation association and displays the session's own
association UUID, so review cannot show another selected association as its owner.
Choose an association explicitly again before preparing from a recovery view. Recover session delegates to the existing no-relaunch/no-PID-signaling
recovery. Recover receipt only repairs a convenience receipt for an already
committed acceptance; orphan generations remain non-authoritative. A schema-1
store displays an explicit upgrade action and the backup filename; the operation's
backend result includes its backup and new G0 identities. Reading never upgrades.

All operations use the single owned Worker thread; model request IDs reject stale
completions. Prepare and run therefore do not block RmlUi. Back can leave a pending
operation safely; it does not imply cancellation or acceptance. Cancel run sets a
shared atomic token, passed both to the supervisor cancellation path and prelaunch
interrupt check. A completed/cancelled run is reconciled for inspection. On close,
the app signals cancellation, stops queued work, joins the worker, discards queued
completions, then destroys documents/SDL and finally removes its owned demo root.
The supervisor retains its existing bounded child termination/drain behavior;
preflight capability queries retain their five-second timeout. Filesystem/codec
work finishes under existing backend bounds; there is no detached thread.

RmlUi details: conditional disabled attributes use `data-attrif-disabled` (ordinary
`data-attr-disabled` retains an attribute even for false). Console and Return review
share the Console data model. Loop fields are dirtied separately from `data-for`
arrays, so regular updates do not rebuild list rows and destroy focus. Dropdown,
modal and screen Back precedence remains intact.

## Data modes

- **Default demo:** authored disposable content, four hunters (including duplicate
  and non-ASCII names), three expeditions, one managed personal fixture association
  and G0. A separately linked demo factory creates a unique root containing sibling
  content, engine, lodge and query-scratch directories. It cannot attach a policy
  double to a supplied store. The demo child compiles the existing native session
  fixture with production Session/Files I/O and codecs; its fixed +7 score mutation
  is authored test data. It ignores the test fixture's environment-driven behavior
  and invocation-marker hooks. All demo writes, including capability-query scratch,
  stay in the owned root, which is removed only after worker shutdown.
- **`--store DIR`:** read-only. Inspection, catalog, validation and acceptance preview
  are available; all mutation paths refuse before creating a lock or workspace.
- **`--store DIR --allow-writes`:** visibly marked WRITABLE. Enables explicit
  preparation, execution, acceptance, recovery and upgrade on an existing manifest.
  No automatic creation, upgrade, repair, import or registration occurs.

The demo is **not a hunt, trophy or gameplay-success claim**. It uses an explicit
fixture policy double; production Genesis policy rejects the fixture revision.
The existing synthetic child remains the backend's separate schema-1 fixture; it
cannot provide managed schema-4 acceptance. The GUI continuation demo therefore
uses the native fixture, not synthetic-session adoption. Neither demo results nor
readable returned bytes certify real gameplay.

## Exact commands

Run these from the repository root. The Python path below is the interpreter found
on the validation host (CPython 3.12.14); substitute a supported 3.12 installation
elsewhere. No Python interpreter is needed by the installed application.

```sh
cmake -S Frontend -B build/hunt-loop-debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DPython3_EXECUTABLE=/home/willvdb/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3.12
cmake --build build/hunt-loop-debug --parallel 6
ctest --test-dir build/hunt-loop-debug --parallel 6 --output-on-failure

cmake -S Frontend -B build/hunt-loop-release -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DC2_FRONTEND_REFERENCE_TOOLS=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE
cmake --build build/hunt-loop-release --parallel 6

cmake -S Frontend -B build/gui-dev -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DC2_FRONTEND_GUI=ON -DC2_FRONTEND_GUI_SYSTEM_SDL3=ON
cmake --build build/gui-dev --parallel 6
ctest --test-dir build/gui-dev -R frontend-gui- --output-on-failure
SDL_VIDEODRIVER=offscreen build/gui-dev/gui/c2-frontend-gui --self-test --capture build/gui-captures/hunt-loop

cmake -S Frontend -B build/gui-fetch -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DC2_FRONTEND_GUI=ON -DC2_FRONTEND_GUI_SYSTEM_SDL3=OFF \
  -DC2_FRONTEND_REFERENCE_TOOLS=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE
cmake --build build/gui-fetch --parallel 6
cmake --install build/gui-fetch --prefix "$PWD/build/hunt-loop-stage"
(cd /tmp && PATH=/usr/bin:/bin SDL_VIDEODRIVER=offscreen \
  /home/willvdb/code/games/carnivores2-mee-modernized/build/hunt-loop-stage/bin/c2-frontend-gui --self-test)
```

Interactive supplied-store example (user-reviewed paths and digest required):

```sh
build/hunt-loop-stage/bin/c2-frontend-gui --store /absolute/path/to/disposable-lodge \
  --allow-writes --probe /absolute/path/to/c2-profile-probe \
  --engine /absolute/path/to/reviewed-engine \
  --trusted-engine-sha256 REVIEWED_SHA256 --experimental-native-hunt
```

Without `--allow-writes`, the same store is read-only. Without the explicit engine,
digest and experimental gate, production prepare/run refuse through the existing
backend. The GUI never discovers or automatically trusts a real engine. Self-test
rejects `--store`, ensuring its scripted acceptance can only target its own demo.

## Limitations and manual checks

Real installation discovery/registration, import/export, trophy browsing,
statistics, theme/plugins, music, on-screen keyboards and filesystem browsing by
controller are out of scope. Setup retains its evaluation-only name/folder fields;
it does not register content. Use the existing CLI for prerequisite registration
and known-personal import. Real engine selection is supplied through explicit
command-line trust options, not a new setup workflow. There is no history browser;
retained sessions are inspected by UUID and complete evidence remains in their
workspaces. Logs are not streamed into the GUI. Demo evidence is disposable.

The backend still limits production hunts to the pinned Genesis revision and
existing supported selection policy. Unsupported content, stale pins, missing or
corrupt snapshots, unclean returns and incomplete observations remain blocked or
quarantined. No policy, disk schema, native codec or network format was changed.

Legend: **S** = automated real-document self-test; **I** = real-core integration;
**U** = untested interactively, manual steps below. No live-session observations
are claimed for this milestone.

| Check | Evidence / manual steps |
| --- | --- |
| Full fixture loop and fresh generation after accept | S, I |
| Decline preserves authority and discards preview permission | S, model tests |
| Stale prepared session refuses launch; blocked preview remains inspectable | I |
| Owned cancellation yields quarantine; preview refuses; shutdown joins | I; model/worker tests |
| Read-only supplied-store bytes unchanged | I (all-file comparison), existing store tests |
| Back while run is pending; modal Back restores focus; dropdown Back precedes screen | S |
| Duplicate hunter identities, three list rows, cross-pane focus, virtual gamepad | S |
| Raw observations, preview digest, receipt and continued generation | S captures, I |
| Physical Xbox controller, mouse wheel, clipboard, live Wayland, Windows | U: run the demo interactively, select by pad/mouse, scroll both panes, open/close review, Alt-Tab, close during a run; repeat on Windows |
| Explicit schema-1 upgrade UI | U: open a disposable schema-1 store read-only; verify disabled action. Reopen with write opt-in, explicitly upgrade; inspect backup and returned G0 IDs |
| Real trusted Genesis normal return and accepted continuation | U: follow PLAY_LOOP_HANDOFF.md's human checklist in a new disposable store, record trusted build/content hashes, normal world entry/controls/return, preview and explicit acceptance, then prepare/run again; compare protected native bytes |
| Recovery after an actual crash | U: retain the disposable workspace, establish owner/child quiescence per backend instructions, then inspect UUID and use the appropriate recovery action; never remove a lock based only on age |

## Verification evidence (2026-09-28)

Final local matrix tested code commit `3c413e1cfafa6f7ebc28f354171c7d3c852963e0`.
Documentation commits follow this code commit. Host: CachyOS Linux, GCC 16,
CMake 4.4, Mesa OpenGL 4.6.

| Configuration / command above | Tested code | Result |
| --- | --- | --- |
| GUI off, Debug, tests on, CPython 3.12.14 | `3c413e1` | 51/51 pass, including existing CLI/reference/differential suites and native workflow sandbox |
| GUI off, Release, Python discovery disabled | `3c413e1` | configure and build pass |
| GUI on, system SDL3, focused `frontend-gui-*` | `3c413e1` | 7/7 pass, including real-core integration and offscreen documents |
| System SDL3 self-test with `--capture` | `3c413e1` | pass; images under `build/gui-captures/hunt-loop/` |
| GUI on, pinned SDL3, Release, Python discovery disabled | `3c413e1` | build and staged install pass |
| Staged pinned build, cwd `/tmp`, `PATH=/usr/bin:/bin`, offscreen self-test | `3c413e1` | pass; resource log identifies staged `share/c2-frontend-gui` |

Local build/configure/test logs are copied to `build/hunt-loop-evidence/` (ignored,
not shipped). Headless captures were visually inspected, including the raw native
100 → 107 fixture score observation and the preview digest. This is **S** evidence,
not live gameplay or human controller evidence. Python 3.12 was found in the local
runtime cache, so the final full-suite result does not rely on waiving the known
host-Python-3.14 oracle failures.

The real-core integration protects installation bytes, compares every supplied
read-only store file before/after, exercises stale prepared refusal and blocked
preview, cancellation quarantine and refused preview, receipt recovery without
commit, digest-less refusal, and joined worker shutdown. Model tests cover pending
and completed stale IDs, changed loadout invalidation, decline/accept, missing
digest, recovery errors and read-only gates. The real-document test drives the
same button callbacks as users, including Back during pending run, explicit
acceptance and subsequent preparation from the accepted generation.


Hosted CI at `393a878b637c83fd85c4c01656ea22338f9aa0b3`:
[Frontend run 36509041480](https://github.com/willvdb/carnivores2-mee-modernized/actions/runs/36509041480)
passed all four jobs: Linux backend **51/51**, Windows backend **66/66**, Linux GUI
(including documents and staged install) and Windows GUI (build, headless tests,
install and help). The [repository-wide Build and Test run](https://github.com/willvdb/carnivores2-mee-modernized/actions/runs/36509041500)
also passed. Windows real-core loop integration passed in 23.43 seconds.

The later retained-session identity fix and development-helper/CI-test-selection
follow-up (`963fc19`, `3c413e1`) change only GUI/model/test/build glue, not the native
backend or its CLI. The local matrix above includes those follow-ups. Their hosted
CI status is tracked on the PR; the earlier hosted results are not attributed to
the later commits. The GUI CI jobs now explicitly include both loop model and
real-core integration tests in their focused selection. Development helper lookup
supports both single-configuration and multi-configuration build layouts; building
the GUI target explicitly also builds its codec and demo helpers.
