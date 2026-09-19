# Native session isolation checkpoint

Task branch: `codex/native-session-isolation`, independent worktree.
Starting local main: `a2cfec8ef3590c6c8d56d16c7a69e4c0e5327f5a`.
Fetched main/base: `08c219fdb877a1fe30ce431eef8ac7b38a4987b3`.
Prerequisite: `46252588a76db705ed7b7148041f4f91c44897b5` (local and remote).
Normal no-conflict merge: `1219fc54e5c4a2c1cebc9ad55e006e7e2c0ae7e3`.
Original main worktree's untracked carnivor.log/render.log retained untouched.

## Implementation checklist

- [x] Read repository/portability/frontend/display contracts and trace write sites.
- [x] Integrate prerequisite preserving its history.
- [x] Versioned pre-startup policy, strict arguments and independent state/config/output.
- [x] Production file routing, strict profile loads and sticky I/O exit failures.
- [x] Production-path regression harness and actual Linux engine build.
- [x] Standalone frontend Linux/Windows CI with mandatory codec probe.
- [ ] Optional explicitly gated native observer, only after required checks pass.
- [ ] Distinct self-review, final tests and draft PR.

V1 conservative decisions: existing complete SAV/SAB pair required; no substitute
profile or missing companion synthesis. Cwd remains read-only content context.
Absolute existing workspace has state/, config/, output/; no links or shared
file hardlinks. Explicit independent source and baseline roots are mandatory.
Output directory starts empty; performance captures disabled. This is a trusted
engine's I/O contract, not containment of hostile binaries/concurrent writers or
third-party driver/library behavior. No disk format or display-policy change.

## Required checkpoint

Interface and layout: [ENGINE_SESSION.md](ENGINE_SESSION.md).
Baseline: actual GCC Linux Debug engine with pinned SDL built; 17 runnable CTest
checks passed, 5 existing opt-in display checks skipped. Frontend: 90 tests passed,
zero skips with the production probe.

After routing: actual engine built; 24 CTest entries, 19 passed and the same five
opt-in display checks skipped. Includes 22 production C++ session tests and six
Python actual-engine startup tests (five run, Windows-junction case platform-skipped).
Clang ASan/UBSan with leak detection: session production harness and existing core
suite pass. Frontend's same 90 tests pass with an ASan/UBSan production C++ probe;
this does not instrument Python process logic. Logs are /tmp/c2-session-*.log and
/tmp/c2-frontend-sanitizer-*.log. Windows CI definitions added; results pending.

The production tests inventory source/content/baseline fixtures, exercise real
profile/config/log/screenshot code, and hash before/after inventories including
the copied actual executable in subprocess tests. No original user profile or
assets selected. Actual engine subprocesses ran only capability/invalid-startup
and forced platform-failure checks against synthetic files; no Genesis hunt.

Required work is implemented and reasonably validated. Continue with the narrowly
scoped experimental observer adapter, retaining synthetic schema-1 behavior and
candidate-only authority. A separate read-only engine review is in progress.
