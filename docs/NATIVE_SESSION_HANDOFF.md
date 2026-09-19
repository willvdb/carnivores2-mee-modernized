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
- [ ] Versioned pre-startup policy, strict arguments and independent state/config/output.
- [ ] Production file routing, strict profile loads and sticky I/O exit failures.
- [ ] Production-path regression harness and actual Linux engine build.
- [ ] Standalone frontend Linux/Windows CI with mandatory codec probe.
- [ ] Optional explicitly gated native observer, only after required checks pass.
- [ ] Distinct self-review, final tests and draft PR.

V1 conservative decisions: existing complete SAV/SAB pair required; no substitute
profile or missing companion synthesis. Cwd remains read-only content context.
Absolute existing workspace has state/, config/, output/; no links or shared
file hardlinks. Explicit independent source and baseline roots are mandatory.
Output directory starts empty; performance captures disabled. This is a trusted
engine's I/O contract, not containment of hostile binaries/concurrent writers or
third-party driver/library behavior. No disk format or display-policy change.

Baseline builds/tests started before functional changes. Results to be recorded.
