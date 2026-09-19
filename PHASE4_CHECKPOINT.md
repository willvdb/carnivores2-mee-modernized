# Phase 4 implementation handoff

Worktree: `/home/willvdb/code/games/carnivores2-phase4-persistence`.
Base main: `a2cfec8ef3590c6c8d56d16c7a69e4c0e5327f5a`.
No main merge authorized or performed. Frontend checkout untouched.
Deadline for new work: 2026-09-19 22:35 UTC. Implementation finished early;
remaining action is exact-tip CI/independent review, then Will's merge decision.

## Review order and branches

1. `port/display-persistence`: `23070c70911682ba588bb78bd21c076b39c49f85`.
   Core 4e is `527762b31b1d85be04794bc78617647e712b0c4f`; the final commit rejects
   incomplete native bounds discovery and adds native/SDL regression coverage.
2. `port/display-configuration`: implementation `9ba9579d2df971e7f04506f6600d6397d156b9aa`,
   integrated with the 4e correction at `ebb16d0ad89c1c8702c2bc18004cbd1b3513e2a7`.
   This final handoff commit changes documentation only. `git rev-parse HEAD`
   identifies its exact tip; no history was rewritten.

4e is implemented with an explicitly partial platform capability: Windows
registered monitor device-interface identity, **not physical serial identity**;
Linux persistence is unsupported rather than guessed from names/coordinates.
Opt-in manual config, asset-free `-list-displays`, session primary/index/identity
CLI overrides, immutable saved intent, unique association and primary/automatic
fallback are implemented/tested. Native discovery now fails closed when any
monitor rectangle cannot be read. No new UI or automatic preference saving.

4f is implemented: one engine configuration owns dimensions, window mode,
monitor and rational refresh; narrow legacy projections serve renderer/SOFT.
Defaults -> legacy adapter -> config -> final CLI precedence, unmatched config
versus CLI OptRes behavior, {0,0} unspecified pre-profile startup, actual-client
size and Alt+Enter flow are retained. No legacy formats change.
Full source evidence, contracts and requirement trace: docs/DISPLAY_CONFIGURATION.md.

## Validation

- Core 4e exact-SHA matrix: all 12 jobs green,
  https://github.com/willvdb/carnivores2-mee-modernized/actions/runs/35403091566
- Core 4f exact-SHA matrix: all 12 jobs green,
  https://github.com/willvdb/carnivores2-mee-modernized/actions/runs/35403898799
- Corrected 4e exact-SHA matrix:
  https://github.com/willvdb/carnivores2-mee-modernized/actions/runs/35404193186
- Integrated correction matrix:
  https://github.com/willvdb/carnivores2-mee-modernized/actions/runs/35404194632
  The final handoff reports completion/current exact-tip run; follow-up runs
  include this documentation checkpoint without changing validated production code.
- Matrix covers Windows native/SDL GL x86/x64 Debug/Release, SOFT, Menu; Linux
  Debug/Release, boundary check, golden serialization and virtual-X11 regression.
- Local GCC Debug/Release and Clang: full game builds and 14 active CTest checks
  pass (15 total including an ordinary opt-in X11 skip). Core 4f has 329 GoogleTests;
  correction adds one Linux SDL test and two additional Windows helper tests.
- Portable strict GCC/Clang C++17 -Wall -Wextra -Werror, ASan/UBSan/LSan: 73/73 each.
- MSVC x64/Wine: native identity/configuration, Menu writer and boundary pass.
- Asset-free discovery: dummy exits 0, only logs, no assets/window/config/profile.
- Actual production Menu SaveConfig body preserves identity, exact refresh,
  unknown keys/versions and comments through two rewrites, without Menu edits.
- Mutation removing saved-target failure refresh clearing fails its regression.
- Ten full-game hunts in disposable X11: expected placement/refresh, saved
  missing/unknown identity -> primary automatic even with supported primary
  refresh, CLI primary/index overrides, Alt+Enter, oversize borderless,
  config/CLI unmatched ordinal differences and malformed-option isolation.
  All normal exits; both desktops restored; .sav/.sab 1660/7176 bytes and all
  68 keybinding bytes retained in copied profiles. Host state untouched.
- Two fully instrumented full-game hunts (secondary exact refresh and missing
  saved target, both Alt+Enter out/back): exit 0 with ASan/UBSan/LSan enabled.
- Fully instrumented X11 regression: 20 cycles per display, zero outstanding
  SDL allocations and both desktop modes restored. No leak suppression.
- Full instrumented CTest: 13 active passes, one known SDL_video.c:1341 dummy
  null-source zero-length memcpy UBSan failure, one opt-in skip. This is NOT
  claimed as a wholly green sanitizer suite.

Evidence: `/tmp/carnivores-phase4e/`, `/tmp/carnivores-phase4f/` (validation.json,
CI JSON/logs, runtime traces/results, sanitizer logs), plus
`/tmp/carnivores-phase4f-{build,tests}.log`. Test profiles/assets remain outside git.

## Remaining limits / next safe action

No implementation blocker remains within the declared partial-platform contract.
Inspect exact-tip hosted CI and review both feature branches. Apply any in-scope
review correction as a new focused commit; do not merge main. Physical Windows
multi-monitor acceptance remains outstanding. Hyprland/Xwayland exclusive
secondary targeting is not accepted; direct Wayland is not certified. Linux
persistent identity is unsupported. Existing SDL X11 initialization/teardown
leak and dummy UBSan dependency issues remain unsuppressed. Do not label all
physical/platform acceptance complete or broaden into Phase 5/frontend work.
