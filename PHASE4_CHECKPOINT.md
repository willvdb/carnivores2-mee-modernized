# Phase 4 implementation checkpoint

Worktree: `/home/willvdb/code/games/carnivores2-phase4-persistence`.
4e branch `port/display-persistence`, completed implementation at
`527762b31b1d85be04794bc78617647e712b0c4f`, based on
`a2cfec8ef3590c6c8d56d16c7a69e4c0e5327f5a`.
4f branch `port/display-configuration`, stacked on 4e; this commit is the implementation milestone.
No main merge authorized; frontend checkout untouched. Deadline 2026-09-19 22:35 UTC.

4e implements Windows registered monitor-interface identity (device path, not
physical serial), conservative native/SDL rectangle association, explicit Linux
unsupported persistence, manual opt-in config, asset-free discovery, session
overrides, retained intent and primary/automatic fallback. Source evidence and
full contract are in docs/DISPLAY_CONFIGURATION.md.

4e validation:
- Exact-SHA CI all 12 jobs green: https://github.com/willvdb/carnivores2-mee-modernized/actions/runs/35403091566
  Windows native+SDL x86/x64 Debug/Release, SOFT, Menu; Linux Debug/Release incl X11 regression.
- Local GCC Debug/Release and Clang game builds and 14 active CTest checks pass
  (15 total, ordinary opt-in X11 skipped); portable GCC/Clang strict ASan/UBSan/LSan 66/66.
- MSVC x64/Wine Platform/MenuConfig/boundary pass. Asset-free -list-displays exits
  0 under dummy and creates only logs, no config/profile.
- Virtual X11: 20 cycles/display, zero SDL allocations, both modes restored.
- Mutation removing saved-target failure refresh clearing fails its regression.
Evidence /tmp/carnivores-phase4e; Debug logs /tmp/phase4-{build,tests}.log.

4f implementation in progress: single GameDisplay::Configuration authority,
startup/profile/config/CLI adapters, narrow legacy projections, preserved
actual-client-size/Alt+Enter state flow. Characterized {0,0} missing-profile
startup sentinel retained. No parser syntax or profile format change.
GCC Debug/Release and full Clang builds/tests pass. New portable configuration
tests cover source composition, unmatched ordinals, SOFT, actual size and toggle.
4f validation before push:
- Strict GCC/Clang portable ASan/UBSan/LSan: 73/73 each.
- MSVC x64/Wine Platform/MenuConfig/boundary: pass.
- Ten copied-profile virtual full hunts: normal exits, expected targeting/refresh,
  saved missing/unknown identity -> primary automatic, CLI index/primary overrides,
  Alt+Enter, oversize borderless, config/CLI unmatched OptRes difference and
  malformed-option isolation. Every desktop restored; .sav/.sab sizes 1660/7176
  and 68 keybinding bytes unchanged. No host display/config/profile changes.
- Fully instrumented SDL/engine CTest: 13 active passes, known SDL_video.c:1341
  dummy null-source memcpy UBSan failure, one opt-in X11 skip. No suppression.
- Fully instrumented virtual X11 mode regression: 20 cycles/display, zero SDL
  allocations, both desktop modes restored.
Next: push/inspect exact-SHA hosted matrix and independent review corrections.
Instrumented full-game hunts can provide additional focused evidence.
Evidence /tmp/carnivores-phase4f; Debug /tmp/carnivores-phase4f-{build,tests}.log.

Known limits: unsupported Linux persistence (not faked from names/EDID/coordinates);
no physical Windows multi-monitor acceptance; Hyprland/Xwayland exclusive limit;
independent SDL X11 init/teardown leak and dummy null-source zero-length memcpy
UBSan issue. Do not suppress these or claim full sanitizer/physical acceptance.
