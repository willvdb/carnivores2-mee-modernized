# Genesis play loop execution record

## Scope and checkpoints

Starting main: `c4078d63884690144b9061476e7800162fc3b113` (fetched 2026-09-19).
Original main checkout has two untracked logs; untouched. No open PRs at start.
Dedicated worktree; branch A `frontend/genesis-hunt-adapter` from fetched main.
B will branch from A as `frontend/managed-state-continuation`, PR targeting A.
No main writes, merges, force pushes, user-store upgrades or real-save mutation.

- [x] Baseline/ref/worktree audit; contracts and production implementation trace.
- [x] Baseline standalone Debug build and full frontend suite (116 tests, no skips).
- [x] A: versioned pinned normal-hunt policy, shared native lifecycle, CLI, regressions.
- [ ] A: full checks and read-only policy review complete; publish checkpoint and draft PR.
- [ ] B: explicit versioned metadata upgrade; immutable generations; acceptance transaction.
- [ ] B: generation-pinned continuation; failure/recovery/end-to-end tests.
- [ ] B: final review, validation, draft stacked PR and backend/UI handoff.

## Decisions and source evidence

Existing exact Genesis fingerprint remains unchanged (Frontend/lodge/genesis.py).
Menu/Menu.cpp builds license bits from filtered AI>=10 *positions*, weapons from
ordered positions; CommandLine.cpp multiplies din by 1024, consumes wep directly.
ScriptParser.cpp char0..char9 predicates use bits 10..19. No AI identity conversion.
CalculateDebit and hunt selection require summed listed prices <= native score;
launch block does not subtract it. Rank filtering is commented out. Menu rank at
10000 becomes 1000; EngineProfile::UpdateRank caps at 2. Preserve native observations,
never normalize rank or invent fees. Equipment restored from SAV is ignored by
EngineProfile::ApplyOptions. All accessories remain disabled in this assignment.
Policy defaults are the six existing smod values; SubmitDinoScore applies only
active tranq/radar/scent/camo modifiers. Native scoring/saving owns progression.

Keep schema-1 synthetic and schema-2 observer meaning unchanged. A needs a distinct
normal-hunt journal kind/version, still candidate-only. B needs explicit manifest
versioning so old code fails closed and a new generation-pinned session contract.
Fixtures use authored bytes and production codecs/session I/O; any policy double
stays test-only and the real production pin must independently reject fixture content.
No suitable task-only personal Genesis baseline has been explicitly configured;
real gameplay is pending, independently of automated implementation validation.

## Commands and results

Baseline (passed):
```
cmake -S Frontend -B /tmp/c2-play-loop-a -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/c2-play-loop-a --parallel 6
ctest --test-dir /tmp/c2-play-loop-a --output-on-failure --no-tests=error
```
Repeat at each checkpoint; also compileall and git diff --check. Add sanitizer
build for C++ probe/fixture and actual argument-consumption characterization.
Engine/menu/display CI remains enabled; rebuild engine if production engine changes.

## Checkpoint A validation/review

126 frontend tests pass (no skips) with real probe, production Session/Files child,
and actual CommandLine body characterization. Intermediate extraction errors
(local preflight import shadowing, too-strict schema-1 kind persistence) were fixed;
existing tests remain unchanged. A C++ fixture field-name typo was fixed at build.
Read-only reviewer identified generic resource-base observations being wrongly
used as availability gates; corrected and regression covered. No production engine
or native layout change. ASan/UBSan suite rerun after fixes; final result below.

## Next executable action

Publish candidate-only A after final sanitizer result. Then branch B from this
checkpoint and implement manifest-v2 authority plus schema-4 generation-pinned hunts.

A sanitizer validation passed: 126 tests/no skips, Clang ASan+UBSan on the C++
codec, production native I/O fixture and extracted actual argument consumer.
Python is not instrumented; no full graphics hunt is claimed.
```
cmake -S Frontend -B /tmp/c2-play-loop-a-sanitized -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all'
cmake --build /tmp/c2-play-loop-a-sanitized --parallel 6
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /tmp/c2-play-loop-a-sanitized --output-on-failure --no-tests=error
python3 -m compileall -q Frontend
git diff --check
```
