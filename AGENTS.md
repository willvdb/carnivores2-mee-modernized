# Agent guidance

Before making portability, build-system, serialization, renderer, filesystem, input, audio, or networking changes, read `docs/PORTING.md` in full.

## Binding constraints

- Preserve compatibility with existing Carnivores 2 / MEE content wherever practical.
- Do not change legacy disk or network formats merely to make runtime structures convenient on x64.
- Treat runtime representation and serialized representation as separate concerns.
- Do not require legacy content mods to be converted to new formats for the initial port.
- Keep the existing Windows x86 OpenGL build usable as a regression reference during the transition.
- The portable engine target is OpenGL. The x86 software renderer and its inline assembly may remain legacy-only and must not block x64/Linux work.
- Prefer fixed-width integer types for file/network formats.
- Do not hide pointer truncation or size mismatches behind casts or warning suppression.
- Prefer small, reviewable, behavior-preserving changes.
- Add tests around architecture-sensitive parsing/serialization before changing established behavior.
- Do not redistribute or commit original Carnivores 2 game assets or HUNTDAT.
- Keep changes suitable for possible upstream contribution to `Tibbee/carnivores2-mee-modernized`.

## Commit and PR discipline

This work is intended to remain easy to review, bisect, cherry-pick, and potentially upstream in pieces.

- Keep each commit focused on one logical change. Do not combine unrelated portability fixes, refactors, formatting, cleanup, or quality-of-life work in the same commit.
- Make prerequisite changes separate commits when they are useful or understandable on their own.
- Keep commits independently reviewable and, where practical, independently buildable/testable.
- Prefer a short sequence of clean commits over one large commit that touches several subsystems.
- Do not perform opportunistic code cleanup while fixing a portability issue unless the cleanup is required for that fix. Put optional cleanup in a separate commit/PR.
- Avoid repository-wide formatting or mechanical churn in functional porting commits; it makes upstream review and cherry-picking harder.
- Preserve behavior in infrastructure/refactoring commits. If behavior must change, isolate it and explain the compatibility impact explicitly.
- Write commit messages that describe the concrete change and its reason, not the agent session or implementation process.
- Before finishing a branch, remove accidental/debug-only changes and organize the history so each commit has a clear purpose. Do not leave temporary/WIP commits in an upstream-ready PR when they can be cleanly consolidated.
- Keep PRs scoped to a single milestone or tightly related set of changes. If a discovered issue can be fixed independently, prefer a follow-up PR rather than expanding the current one.
- When a change is specifically useful upstream, avoid coupling it to fork-only features so it can be cherry-picked into `Tibbee/carnivores2-mee-modernized` with minimal conflict.

## Current task order

1. Repository-wide x64 assumption audit (`docs/X64_AUDIT.md`).
2. Windows x64 OpenGL build while preserving Windows x86 OpenGL.
3. Serialization/layout hardening.
4. Thin platform abstraction, likely using SDL3.
5. Portable filesystem/path compatibility.
6. Linux x64 bring-up.
7. Portable OpenAL integration.
8. Input/display QoL.
9. Networking portability.
10. Cross-platform launcher/menu.

Do not skip directly to SDL/Linux before the x64/layout audit unless explicitly instructed.
