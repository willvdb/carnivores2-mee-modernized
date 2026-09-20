# Genesis play loop execution record

## Scope and completed checkpoints

Starting fetched main: `c4078d63884690144b9061476e7800162fc3b113` (2026-09-19).
The original checkout's local main remained `a2cfec8ef3590c6c8d56d16c7a69e4c0e5327f5a`,
with its two pre-existing untracked logs untouched. Work used a dedicated worktree.
No main writes, merges, force pushes, default-store upgrades or real-save mutation.

- [x] Baseline/ref/worktree audit; applicable instructions, full PORTING and contracts read.
- [x] Production Menu/engine/profile/display trace; baseline 116 tests, no skips.
- [x] A: pinned normal hunts, shared native lifecycle, CLI, candidate-only regression tests.
- [x] A: Debug/sanitizers/read-only review; committed and published draft PR #14.
- [x] B: explicit metadata upgrade, immutable generations, bounded acceptance transaction.
- [x] B: generation-pinned continuation, staleness, failure/recovery and end-to-end tests.
- [x] B: distinct final review, local validation, draft stacked PR #15 and UI handoff.
- [ ] Human native Genesis world-entry/return/acceptance/continuation validation.

Current branch: `frontend/managed-state-continuation`.

| Checkpoint | SHA | Review boundary |
| --- | --- | --- |
| A candidate-only hunt | `cecac030b4d8a08b6c5966e6c742cb8c32ff93ad` | `frontend/genesis-hunt-adapter`; [draft #14](https://github.com/willvdb/carnivores2-mee-modernized/pull/14) → main |
| B metadata prerequisite | `13737fe2babe785bed15e43dbe4f9ec02b073a48` | Independently built/tested upgrade/history commit |
| B acceptance/continuation code | `1fce7dac551b68aeef8bd88ffbc64aaa568be344` | [draft #15](https://github.com/willvdb/carnivores2-mee-modernized/pull/15) → A; depends on #14 |

A remains independently candidate-only and unmerged. B's remaining publication
commit only updates this execution record. Both draft PRs have no auto-merge.

## Decisions and source evidence

The exact Genesis fingerprint in `Frontend/lodge/genesis.py` is unchanged.
`Menu/Menu.cpp` maps filtered AI>=10 license *positions* and weapon positions to
bits. `CommandLine.cpp` multiplies `din` by 1024 once, consumes `wep` directly;
`ScriptParser.cpp` char0..char9 conditions use bits 10..19. Grouped licenses and
repeated AI values do not change selection identity. See precise function/source
references and boundaries in [GENESIS_HUNT.md](../Frontend/docs/GENESIS_HUNT.md).

One area, one license and one weapon, dawn/day/night, no equipment or extra flags.
All pinned catalog choices are supported within those bounds. `CalculateDebit`
checks summed displayed prices against native score; the launch block does not
subtract it. Rank filtering is commented out. Menu rank at 10000 becomes 1000;
`EngineProfile::UpdateRank` caps at 2. Native observations remain authoritative;
no fees or rank normalization. `EngineProfile::ApplyOptions` ignores saved
equipment settings. Six existing smod defaults are emitted; `SubmitDinoScore`
applies only active tranq/radar/scent/camo modifiers. The engine owns progression.

Schema-1 synthetic/schema-2 observer/schema-3 candidate-only hunt journals retain
their original meaning. Manifest v2 is an explicit backed-up metadata operation;
old A code was directly exercised and rejects it. Schema-4 normal hunts pin the
current generation. Old prepared modes fail closed on v2, with terminal inspection
retained. Return inspection uses the historical baseline; acceptance/preflight
require the unchanged current generation identity, not merely equal hashes.

`lodge.json` is the only authority for current generation and acceptance receipts.
Exact independent bytes are staged, verified and renamed before its atomic
replacement commits head plus receipt. Earlier failure preserves old authority;
later failure preserves new authority. A session receipt file is a reconstructible
copy only. Retry after later acceptance returns the original receipt without
rewinding. Recovery never promotes orphans, queries/launches an engine, signals
stored PIDs, steals locks or deletes evidence. See [MANAGED_STATE.md](../Frontend/docs/MANAGED_STATE.md).

## Exact validation commands and results

Baseline/A: standalone Debug full suite 116 before changes, 126 after A; no skips.
A Clang ASan/UBSan full suite: 126 passed, no skips. The same configure/build/test
commands below used `/tmp/c2-play-loop-a` and `/tmp/c2-play-loop-a-sanitized`.
B's independently checked-out metadata prerequisite passed 134 tests, no skips.

Final B Debug and sanitizer suites each passed **154 tests, no skips**:

```sh
cmake -S Frontend -B /tmp/c2-play-loop-b -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/c2-play-loop-b --parallel 6
ctest --test-dir /tmp/c2-play-loop-b --output-on-failure --no-tests=error
cmake -S Frontend -B /tmp/c2-play-loop-b-sanitized -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all'
cmake --build /tmp/c2-play-loop-b-sanitized --parallel 6
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /tmp/c2-play-loop-b-sanitized --output-on-failure --no-tests=error
python3 -m compileall -q Frontend
git diff --check
```

Real C++ profile codecs, production Session/Files fixture I/O, and the extracted
actual CommandLine consumer are exercised. Sanitizers cover those C++ executables,
not Python or a graphical engine hunt. No production engine/layout changes were
made; engine/menu/display and Linux/Windows frontend CI remain enabled.
A's exact-head hosted Frontend and full Build and Test workflows passed. B hosted
checks are pending at publication; final-head results belong in PR #15 and the
final delivery report, not inferred from A or a preceding head.

## Demonstration, review and resolved failures

Authored disposable fixture loop: G0 → S1 changed native candidate → explicit G1 →
S2 actual source/work/baseline bytes equal G1 → explicit G2. Installation/import,
G1 and session baselines remain unchanged at each applicable stage. Retrying S1
after G2 returns G1's receipt and preserves G2. The production Genesis gate rejects
fixture content; test policy doubles are visibly test-only. No fixture mutation
is described as gameplay, a trophy or a successful hunt.

During development, extraction/preflight scoping, a C++ fixture field typo and an
overstrict schema-1 journal check were fixed. B exposed two existing corrupt/missing
manifest recovery regressions; the guard was corrected without changing those
tests. Final suites have no failures. No dependency pin was changed or warning/test
suppressed.

Separate read-only review found generic catalog resource observations being used
as the wrong availability gate, and accepted history permitting mutually missing
execution evidence. Both were fixed with regressions and re-reviewed. Final
self-review traced authority/commit point, native reads/writes, all fallback paths,
stale prepared/returned sessions, duplicate acceptance, unsafe aliases, exact byte
preservation, receipt failure/recovery and old-kind separation. Complete diff
contains no proprietary assets, real saves, private paths or unsanitized logs.

## Remaining evidence and next executable action

No suitable personal Genesis baseline was explicitly configured for a new task
store. No unrelated personal directories/default store were searched or modified.
Real normal-hunt world entry, controls, evacuation/native save changes and G1/G2
continuation remain unvalidated. The limitation does not block the implemented
asset-free backend loop. There is no unresolved policy decision for the supported
subset; multiple loadouts, equipment and other editions remain disabled.

Check B's hosted final-head CI after this documentation commit and report exact
results. Then review both drafts and run the disposable-store human native
checklist in [PLAY_LOOP_HANDOFF.md](../Frontend/docs/PLAY_LOOP_HANDOFF.md). That
handoff documents actual module/CLI calls and JSON for the next thin Expedition
Console: selection/loadout, launch/status/cancel, native before/after review,
explicit candidate/predecessor acceptance and managed-history refresh. The GUI
must consume backend eligibility and authority rules rather than duplicate them.
