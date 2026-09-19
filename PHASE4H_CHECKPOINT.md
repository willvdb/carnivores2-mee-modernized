# Phase 4h checkpoint

Worktree: `/home/willvdb/code/games/carnivores2-phase4-linux-persistence`.
Branch: `port/linux-display-persistence`. Exact accepted base:
`857063ad77c6c6c9239d8ae67fc62d60215b7e90`; verified local and GitHub ref.
Main and all predecessor/frontend worktrees remain untouched.

## Design in progress

Support serial-backed native X11 EDID and direct Wayland wlr-output-management
v3 make/model/serial. Keep separate explicit domains; no backend translation.
Reject incomplete, placeholder, invalid and duplicated identity metadata.
SDL mapping remains private, borrowed for discovery only; owned identity copies
cross Platform. Game retains unique selection, primary/automatic fallback and
saved intent. No profile/settings format changes or automatic monitor saves.

The pinned SDL public API does NOT expose per-output native handles. Backport
the current public Wayland wl_output display property; add a small custom X11
connection/output/root property contract in the pinned backend. No private SDL
ABI casts. Modern system SDL can support Wayland via its public property;
unextended system SDL X11 remains unsupported. Both source changes are guarded
by whole-file checksums and separate from the inherited ownership/disconnect fixes.

Wayland uses SDL's own connection, borrowed exact output handles, own xdg-output
v2 objects and wlr manager v3 on an isolated queue. The wlr v2 XML requires enabled
head names to equal xdg-output names. Names are used solely for this unique
same-session protocol association, never persisted. Generic Wayland without
these protocols/serial metadata remains unsupported. XWayland typically has no
EDID and must not borrow metadata from another Wayland session or DRM sysfs.

Primary sources: SDL release-3.2.28 source, current SDL_GetDisplayProperties wiki;
https://wayland.freedesktop.org/docs/html/apa.html (core names not persistent);
https://github.com/swaywm/wlr-protocols/blob/master/unstable/wlr-output-management-unstable-v1.xml
(v2 serial recognition and xdg-output exact-name contract);
https://xorg.freedesktop.org/archive/current/doc/randrproto/randrproto.txt;
https://gitlab.freedesktop.org/emersion/libdisplay-info/-/blob/main/edid.c.

Evidence and commands: `/tmp/carnivores-phase4h`. Dependency sources copied into
its own deps directory; no predecessor build tree is mutated. Next: implement,
validate synthetic/native fixtures, commit/push and obtain final exact-head CI,
then supervisor review. Physical reboot/replug, Windows physical acceptance and
inherited unsuppressed sanitizer limits remain pending. Never advance to 4i.


## Implemented and local validation (pre-review)

Prerequisite commit `b59d43a`: exact SDL native mapping properties/backport,
checksum guards and isolated dependency documentation. Functional implementation
currently staged for the next focused commit; final exact-head CI remains required.

GCC Debug/Release, Clang Debug and installed SDL 3.4.16 Debug full builds/CTest
pass: 15 active suites, 4 opt-in runtime skips (19 total). Native platform source
strict compilation passes GCC/Clang with `-Wall -Wextra -Werror`.
Fully instrumented CTest passes new portable/native protocol suites; only the
inherited pinned SDL dummy null-source memcpy UBSan at SDL_video.c:1341 fails.
No suppression. Logs: `build-*`, `ctest-*`, `strict-*` in the evidence root.

X11 production discovery fixture passes exact serial/output association, fresh
EDID reads, duplicate/checksum/truncation rejection and recovery. Inherited mode
allocation regression passes 20 fullscreen cycles per output, zero SDL-owned
allocations and restored desktops. Nested Weston presentation/disconnect passes.
Local fixtures use read-only extracted test dependencies with module paths only
in copied temporary fixture configs. No host display configuration changes.

Real Hyprland direct Wayland read-only `-list-displays` yields three distinct
serial-backed identities, including the two identical Dell models. The host's
Xwayland endpoint is presently unavailable (no running Xwayland/live X0 socket);
its failed SDL initialization is retained as unavailable evidence, not acceptance.

Six private Wayland protocol-server tests cover no configuration writes, exact
borrowed output association, unsupported v2/absent serial, disabled duplicate,
incomplete native transaction, repeated resource release, 500ms timeout and
late-head cleanup/recovery. A negative-control build that discards the timed-out
queue fails the late-head regression with a protocol error and failed recovery;
the production reader retains the queue and passes. Evidence `negative/`.

Remaining work: final commit/push, exact-SHA 12-job CI and independent
supervisor review. Physical reboot/replug and inherited platform limitations stay
explicit; no 4i work is included.


## Final local evidence

- GCC and Clang portable strict ASan/UBSan/LSan tests: 82/82 each, no diagnostics
  (`portable-*-{build,tests}.log`). Sandbox tracing prevents LSan operation, so
  successful runs used authorized native execution, without suppression.
- Final GCC Debug/Release, Clang and installed SDL build/CTest checks pass.
  Changed policy, Menu preservation and all six native Wayland protocol tests
  pass with full instrumentation (`sanitized-identity-final.log`).
- Instrumented production X11 identity fixture plus both inherited 20-cycle
  allocation/restoration tests pass without ASan/UBSan/LSan reports
  (`x11-sanitized.log`). This does not resolve the separately known real-X11
  initialization lifetime reports on other stacks.
- Instrumented read-only real Hyprland Wayland listing exits zero without
  sanitizer reports (`physical-sanitized/`). Installed SDL 3.4.16's public output
  handle yields byte-identical tokens to bundled SDL (`physical-system/`).
- Production asset-free X11 listing emits two copyable tokens from the fixture;
  no config/profile writes (`x11-runtime-final.log`). No original assets or
  profiles were used or modified in this identity milestone.
- The exact versioned protocol name join history was examined in upstream
  wlr-protocols commit `a5028afbe4a1cf0daf020c4104c1565a09d6e58a`: wording changes
  from xdg-output to the newer core events without a version bump, also allowing
  enabled heads without a wl_output. Missing associations stay unavailable.

Final code/branch SHA and GitHub CI links will be placed in
`/tmp/carnivores-phase4h/FINAL_CI_HANDOFF.md` after the pushed commit is known;
this avoids a self-referential commit hash in its own versioned report.
