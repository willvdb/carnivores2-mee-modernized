# Phase 4 implementation checkpoint

Worktree: `/home/willvdb/code/games/carnivores2-phase4-persistence`
Branch: `port/display-persistence`; base: `a2cfec8ef3590c6c8d56d16c7a69e4c0e5327f5a`.
No main merges authorized. Frontend worktree untouched. Deadline 2026-09-19 22:35 UTC.

4e in progress: owned version/domain/value identity; Windows registered monitor
interface identity; unique rectangle mapping; Linux explicitly unsupported.
Opt-in manual config `display_identity`, asset-free `-list-displays`, session
`-display=primary`, `-display-id=...`; existing index/refresh semantics retained.
No profile format changes. 4f not started. Tests/documentation pending.

Pinned SDL 3.2.28 lacks public HMONITOR/wl_output display properties present in
newer online docs. Source checked locally. Native Windows interface is a device
registration, not a physical serial. Missing/ambiguous identifiers must fall
back to primary+automatic without erasing saved intent.

Next: compile, add production-helper regression tests and exact contract/evidence.

Validation at uncommitted 4e candidate (2026-09-18):
- Linux GCC Debug: game builds; 14 active CTest checks pass, opt-in X11 skipped.
- Portable GCC/Clang C++17 -Wall -Wextra -Werror with ASan/UBSan/LSan: 66/66 each.
  Must run outside sandbox ptrace for LSan (initial sandbox exit is not an engine defect).
- MSVC x64/Wine: Platform, MenuConfig and boundary tests pass.
- Production -list-displays with dummy: exit 0, no assets/window/config/profile;
  only logs, correctly says identity unavailable.
- Release and virtual X11 regression currently running. Hosted exact-SHA matrix pending push.
Evidence: /tmp/carnivores-phase4e and /tmp/phase4-{build,tests}.log.
