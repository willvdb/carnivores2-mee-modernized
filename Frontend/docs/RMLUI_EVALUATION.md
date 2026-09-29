# RmlUi evaluation slice (frontend GUI)

Bounded evaluation of **RmlUi 6.3 + SDL3 + OpenGL** as the toolkit for the
player-facing frontend, linked directly to the native backend. It proves the
risky interactions (illustrated lodge layout, a practical Expedition Console,
text input and folder selection, mouse/keyboard/controller focus navigation,
direct core integration, responsiveness) without implementing the hunting or
save-management workflow. It is not the product.

Branch `frontend/rmlui-evaluation`, based on `main` at `0733556`. Evidence in
this record was produced against code commit `a601c61` (the last commit before
this document); the SHAs of the whole series are listed in the handoff.

## Screen flow

```
Lodge ──Expeditions──▶ Expedition Console ──Demo preview──▶ modal dialog
  │  ◀──── Back ─────────────┘                    ◀── Close / Back ─┘
  ├──Profile / Setup──▶ Setup ──Confirm/Cancel/Back──▶ Lodge
  └──Exit
```

- **Lodge**: a 16:9 stage fitted inside the window (letterboxed with neutral
  fill). Destinations Expeditions, Profile / Setup and Exit are focusable
  hotspots positioned in stage percentages; Trophies and Statistics are shown
  as visibly unavailable. A context panel shows the viewed hunter, the data
  source and the store status. Returning restores focus to the destination
  used to leave.
- **Expedition Console**: scrollable expedition list (stable identities,
  ellipsized long labels), details pane (identity, locator, mode, preview
  artwork slot, local hunter viewing selection incl. duplicate names, demo
  loadout dropdowns, enabled *Demo preview*, disabled *Launch* with its
  reason as plain text). Entering replaces the whole content surface; Back
  returns to the unchanged lodge.
- **Demo preview**: a modal document summarising the selection; Close/Back
  dismisses it and restores focus to the control that opened it. Nothing is
  validated, prepared, launched or written.
- **Profile / Setup**: hunter name and content folder text fields, native
  folder dialog (`SDL_ShowOpenFolderDialog`), existence check off-thread,
  validation messages, Confirm/Cancel. In-memory evaluation state only, and
  the screen says so.

## Architecture

```
RML/RCSS (Frontend/gui/assets)
  └─ Screens (app/screens.*): documents, data models, focus, back semantics
       └─ PresentationModel (model/): screen state, selected IDs, demo edits,
          load/error/empty state, request identity      ─┐
            └─ read_store_snapshot (model/source.*) ──────┴─▶ c2_frontend_core
App (app/app.*): SDL3 window + GL 3.3 core, RmlUi lifetime, event loop,
  Worker (model/worker.*), GamepadInput (app/gamepad.*), folder dialog
```

- `c2_frontend_gui_model` is always built and has no SDL/OpenGL/RmlUi types.
  Its tests run in the ordinary CLI configuration.
- Backend reads use `Store::read()` and typed `Manifest::hunters()`,
  `expeditions()`, `active_hunter()`; no CLI spawning, no JSON parsing, no
  second manifest parser. No core API was changed.
- RmlUi's SDL platform layer and GL3 renderer are compiled unmodified from
  the pinned archive; the only renderer glue is a `LoadTexture` override that
  decodes PNG through vendored stb_image.
- Worker: one owned thread and a UI-drained completion queue with a wake
  callback into the SDL event loop. Store reads and folder checks run there.
  Completions carry a request id; the model ignores stale ones. Native
  folder-dialog callbacks are marshalled through the same queue; a shared
  sink lets a late callback find no application without touching freed
  memory. The worker joins on shutdown and discards queued completions.

## Data modes and safety

| Mode | How | Writes |
| --- | --- | --- |
| Demo (default) | `create_demo_store()` writes the authored `lodge.json` into a uniquely owned `c2-frontend-gui-demo-*` directory under the temp directory; removed on exit | only inside that directory |
| Supplied | `--store DIR` opens an existing lodge read-only | none |

The demo manifest is authored fixture metadata (four hunters including a
duplicate display name, a non-ASCII name and an archived hunter; three
registered expeditions with posix, nt and deliberately long locators). The
demo loadout values are labeled samples, never catalog data. A failed read
shows the core's error text; an existing directory without `lodge.json`
shows the core's empty default manifest and says nothing was created; an
empty store shows an empty state. Selecting a hunter is a local viewing
selection. `store_tests` verifies manifest bytes are unchanged after reads and
that missing/malformed/unsupported inputs are errors, not fakes.

## Build, run, test

CLI configuration (unchanged; GUI off, model tests included):

```sh
cmake -S Frontend -B build/frontend-tests -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug \
  -DPython3_EXECUTABLE=/absolute/path/to/python3.12
cmake --build build/frontend-tests --parallel
ctest --test-dir build/frontend-tests -R frontend-gui --output-on-failure   # model, store, worker
```

GUI on, pinned SDL3 + RmlUi (FreeType from the system on Linux/macOS, pinned
source on Windows):

```sh
cmake -S Frontend -B build/gui -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DC2_FRONTEND_GUI=ON
cmake --build build/gui --parallel
ctest --test-dir build/gui -R frontend-gui --output-on-failure           # + self-test, help
cmake --install build/gui --prefix build/gui-stage
cd /somewhere/else && build/gui-stage/bin/c2-frontend-gui                   # demo store
build/gui-stage/bin/c2-frontend-gui --store /path/to/lodge                  # read-only
```

Options: `-DC2_FRONTEND_GUI_SYSTEM_SDL3=ON` (installed SDL3 ≥ 3.2),
`-DC2_FRONTEND_GUI_SYSTEM_FREETYPE=OFF` (pinned FreeType source anywhere).
Python-free production configure: add `-DBUILD_TESTING=OFF
-DC2_FRONTEND_REFERENCE_TOOLS=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE`.

Headless evidence (real RmlUi documents, real focus navigation, virtual
controller, framebuffer captures):

```sh
SDL_VIDEODRIVER=offscreen build/gui/gui/c2-frontend-gui --self-test --capture /tmp/gui-captures
```

Executable flags: `--store DIR`, `--assets DIR`, `--size WxH`, `--self-test`,
`--capture DIR`, `--help`. Asset root resolution order: `--assets`,
`C2_FRONTEND_GUI_ASSETS`, `<exe>/../share/c2-frontend-gui`, then the build-time
source directory as a development fallback. Missing essential resources
(documents, stylesheet, font) abort with a diagnostic naming each file and a
message box; missing optional artwork shows a labeled placeholder.

## Dependency pins

| Component | Version / revision | Verification | License / notice |
| --- | --- | --- | --- |
| RmlUi | tag 6.3, commit `ba95ffe8bfb6370efb2cdcca927eaad4710c5413` | GitHub archive SHA-256 `d977298b…dede`, archive diffed file-for-file against the tag | MIT, staged as `licenses/LICENSE-RmlUi.txt` |
| SDL3 | 3.2.28 (same archive and checksum as `cmake/SDL3.cmake`; engine display patches not applied) | SHA-256 `13306712…4211` | zlib, staged when built from source |
| FreeType | system package, or 2.13.3 source (default on Windows) | SHA-256 `05503506…3289` | FTL, staged when built from source |
| stb_image | v2.30, commit `f58f558c…bb2c`, `Frontend/gui/third_party/stb/` | header SHA-256 recorded in `third_party/README.md` | MIT alternative, staged |
| DejaVu Sans | 2.37, `Frontend/gui/assets/fonts/` | copied from the distribution package | Bitstream Vera license, shipped beside the font |

OpenGL: RmlUi's GL3 renderer needs an **OpenGL 3.3 core profile** context and
loads functions with its own bundled glad. The engine's requirements are
unchanged; the GUI is a separate process and does not link engine code.

## Input model

One semantic action set: navigate (up/down/left/right), confirm, back, plus
focus next/previous. Mouse, keyboard and controller all end in the same
dispatch so RmlUi's real focus navigation is used everywhere.

| Source | Mapping |
| --- | --- |
| Mouse | clicks, wheel scrolling, text selection and caret placement (RmlUi via `RmlSDL::InputEventHandler`) |
| Keyboard | Tab / Shift+Tab (RmlUi tab order); arrows → navigate; Enter/Space activate; Escape → back; Ctrl+A/C/X/V, Home/End, Shift-selection inside text fields |
| Controller | D-pad or left stick → navigate (enter 0.55, release 0.35, repeat after 400 ms every 120 ms); South → confirm; East/Back → back; shoulders → focus previous/next |

Rules: arrows inside a text field move the caret and never leave the field
horizontally (Up/Down do leave a single-line field; Tab or shoulder buttons
are the reliable exit for controller users). Back dismisses an open dropdown,
then the modal, then leaves the screen; on the lodge it does nothing. A modal
document contains interaction; clicks on the console behind it are ignored.
Window focus loss clears held controller state. All controllers connected
are opened; a disconnect clears the direction that device held.

Cross-pane navigation on the console is explicit: RmlUi's spatial `nav: auto`
searches only inside the focused element's scroll container, so Right from a
list row focuses the details pane's first control and Left from the details
pane (when nothing further left exists) returns to the selected row.

## Scaling and layout

- Window pixel size drives the renderer viewport and context dimensions;
  `SDL_GetWindowDisplayScale` drives RmlUi's dp ratio; all RCSS sizes are in
  `dp`. OS DPI scaling and the lodge aspect fit are separate transforms.
- Lodge: stage size = largest 16:9 rectangle inside the window (in dp),
  centered, neutral fill around it; hotspots use stage percentages. Verified
  at 800×900, 1600×500, 1600×500 at 2× dp, 1280×720, and hotspot offset =
  68 % of stage width.
- Console: flex layout, list and details panes scroll independently.
  Documented minimum window: **800 × 520** (enforced with
  `SDL_SetWindowMinimumSize`).
- Frame pacing: the loop waits on events (≤ 100 ms visible, ≤ 250 ms
  minimized/hidden, shorter when RmlUi requests an update or a controller
  direction is held), renders after each iteration, skips rendering while
  minimized, and the worker wakes it with a user event.

## Test results (code commit `a601c61`)

Host: CachyOS Linux 7.2.6, GCC 16.2.1, CMake 4.4.3, Mesa OpenGL 4.6, native
Wayland session (Hyprland). Everything below ran on this host.

| Evidence | Result |
| --- | --- |
| GUI off, `BUILD_TESTING=ON`, Debug: `frontend-gui-model`, `-store`, `-worker` | 3/3 pass; no RmlUi/SDL3/FreeType in the cache |
| GUI off, Release, Python disabled production configure/build/install | builds and installs; no RmlUi/SDL3/FreeType in the cache |
| GUI on, Debug, system SDL3 3.4.16 + system FreeType 2.14.3: all five `frontend-gui-*` tests incl. `self-test` (offscreen) | 5/5 pass |
| GUI on, Release, Python disabled, pinned SDL3 3.2.28 from source: build, staged install, self-test from an unrelated directory with `PATH=/usr/bin:/bin` | pass; stage contains `bin/` and `share/c2-frontend-gui/{rml,rcss,fonts,images,licenses}` only |
| GUI on, pinned FreeType 2.13.3 from source (the Windows default) on Linux | builds, links the static pinned FreeType (no `libfreetype.so` in `ldd`), stages `LICENSE-FreeType-FTL.txt`, self-test passes |
| Existing frontend suite (48 tests) with the host's CPython 3.14 | 42/48 pass. The 6 failures (`frontend-native-catalog`, `frontend-profile-unicode-oracle`, `frontend-native-profile-files`, `frontend-schema-unicode-oracle`, `frontend-schema-oracle`, `frontend-native-cli`) fail identically on unmodified `main` `0733556` with the same interpreter; the documented verification baseline is CPython 3.12 / Unicode 15, which this host lacks. CI runs 3.12. |
| Live native Wayland run (`wayland` SDL driver) | window created, lodge rendered, letterboxing and focus ring observed in a screenshot (`build/gui-captures/00-live-wayland-lodge.png`) |
| Windows | not built or run locally; CI job builds with pinned deps, runs model tests, installs and checks `--help` |

What the self-test covers (all through the app's own dispatch path, real
documents, offscreen GL): store load off-thread; lodge focus and arrow/Tab
navigation skipping unavailable hotspots; setup entry focus, typing through
the widget (Ctrl+A + text incl. non-ASCII), caret keys staying in the field,
Tab order, cancel restoring focus and discarding edits, confirm with a real
folder existence check on the worker, folder error state; console list
focus, Enter selection without rebuilding rows, no wrap at the end,
Right/Left cross-pane moves, duplicate hunter rows with distinct identities,
select two-way binding and Back closing an open dropdown without leaving,
modal preview focus in/out and a click behind the modal being ignored,
disabled Launch skipped by Tab with its explanation rendered as text; aspect
fit and 2× dp scaling; virtual controller D-pad, dead zone, repeat, focus-loss
clearing, confirm/back; store reload keeping identities; exit. Captures of
each stage are written with `--capture`.

## Interactive checklist

Legend: **S** verified by the headless self-test (virtual events, real
RmlUi); **L** observed on the live Wayland session by screenshot; **U**
untested by a human in this pass, with the manual step given.

| Check | Status | Manual step |
| --- | --- | --- |
| Lodge → Console → Back keeps the lodge unchanged, focus restored | S | Click Expeditions, then Back; expect focus ring on Expeditions |
| Lodge → Setup → Cancel/Confirm | S | Open Profile / Setup; edit; Escape (cancel) and Confirm |
| Text selection with mouse drag and Shift+arrows, Ctrl+C/X/V with the system clipboard | S (Ctrl+A + typing), U (mouse drag, real clipboard) | In Setup, drag-select the name, Ctrl+C, click the folder field, Ctrl+V |
| Native folder dialog: success, cancel, error | U | Browse…, pick a folder (expect "Folder exists…"), Browse… then cancel (text unchanged), run with a broken portal to see "Folder dialog failed: …" |
| Modal dialog blocks the console, Close/Escape restores focus | S | Demo preview, click Back behind the dialog (nothing), Escape |
| Dropdown: mouse open/select, keyboard Up/Down/Enter, Escape closes | S (open/close by Back, value binding) | Open Area with the mouse, Up/Down, Enter |
| Mouse wheel scrolls the list and details | U | Shrink the window to 800×520 and scroll both panes |
| Keyboard ↔ mouse ↔ controller switching without stuck state | S (virtual) | Move focus with a pad, then click with the mouse, then arrows |
| Long list and long labels | S (3 rows, ellipsis), U (scrolling with many rows) | Supply a `--store` with many instances |
| DPI change and aspect changes | S (dp ratio 2×, narrow/tall, ultrawide) | Move the window between monitors with different scales |
| Alt-Tab / focus regain clears held input | S (synthetic focus-lost) | Hold a pad direction, Alt-Tab away and back |
| Pending work at shutdown | S (worker tests) | Close the window while a folder check is pending (large network path) |
| Missing optional artwork | S (code path), U (visual) | Delete `share/c2-frontend-gui/images/lodge_background.png`; expect the labeled placeholder |
| Physical controller | U | An Xbox controller was connected during the runs but no human pressed it |
| Windows desktop, IME, Wayland portals | U | Not available in this pass |

## Limitations and workarounds (RmlUi)

1. **No default stylesheet.** Every element starts `display: inline`; the
   theme declares `div`, `p`, `label` etc. as block. Small, but easy to miss.
2. **Spatial navigation stops at scroll containers.** `nav: auto` searches
   only within the focused element's nearest scroll container. Cross-pane
   Left/Right on the console is implemented in the binding layer.
3. **The GL3 sample renderer decodes only TGA.** PNG comes from a
   `LoadTexture` override using vendored stb_image.
4. **Escape does not close an open `<select>`.** The binding detects the open
   box through the `:checked` pseudo-class on its `selectbox` child and
   toggles it with `Click()`.
5. **`SetValue` on a text input does not fire `change`** (like HTML). Tests
   and bindings go through the widget (Ctrl+A + text input).
6. **Rebuilding a `data-for` list destroys its elements and any focus.** Only
   membership changes dirty the arrays; selection is a class toggle on rows.
7. **Backend files are samples, not a library**, and RmlUi's install rules
   land in a consumer's stage. Compiled from the pinned archive and added
   `EXCLUDE_FROM_ALL`.
8. **Up/Down leave a single-line text field, Left/Right do not.** Acceptable,
   but controller users need Tab (shoulder buttons) to leave a field
   horizontally.
9. **Glyph coverage.** DejaVu Sans covers Latin/Greek/Cyrillic and more, not
   CJK; the demo hunter name with CJK characters renders missing-glyph boxes
   (visibly distinct from corruption, which becomes U+FFFD). Fallback fonts
   and IME are not evaluated. RmlUi supports `lang`/`dir` and a HarfBuzz
   engine if needed later.
10. **Physical controller and Windows** were not exercised by a human here.

None of these is a blocker; each workaround is a few lines and stays inside
the binding layer or the theme.

## Verdict

**Proceed with RmlUi for the hunt-loop milestone.** It fit cleanly: HTML-like
documents with a flex layout and dp scaling, data binding for text and
lists, working text input (selection, clipboard, IME hooks), dropdowns, modal
documents, tab order plus spatial navigation with `:focus-visible`, a reusable
SDL platform layer and GL renderer, and a small static footprint (RmlUi +
SDL3 static link, one shared FreeType on Linux). The workarounds above were
small and local. The evaluation kept the boundaries the next milestone
needs: backend identities only, no policy in UI code, no acceptance or
launch behaviour, and a worker/completion pattern that a hunt supervisor can
extend without a job framework.

What the next milestone should add first: the hunt-loop operations on the
presentation model (plan/prepare/run/inspect/preview/accept as explicit
states with their diagnostics), a per-operation request identity like the
load path, and a cancellation event for the run worker, all behind the
existing headless-test pattern.
