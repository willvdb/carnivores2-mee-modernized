# Portable frontend boundary

Current play-loop extension: [MANAGED_STATE.md](MANAGED_STATE.md) explicitly versions
accepted authority as manifest schema 2 and generation-pinned normal hunts as
journal schema 4. [GENESIS_HUNT.md](GENESIS_HUNT.md) defines candidate-only schema 3.
The historical contracts below remain applicable to their original schemas;
[PLAY_LOOP_HANDOFF.md](PLAY_LOOP_HANDOFF.md) documents the current UI boundary.

Current follow-up: [NATIVE_OBSERVER.md](NATIVE_OBSERVER.md) adds an explicitly gated
native adapter with schema-2 journals against the implemented
[engine session v1 contract](../../docs/ENGINE_SESSION.md). Schema-1 synthetic
behavior and candidate-only authority remain. Historical missing-seam statements
below describe the prerequisite milestone, not current engine capability.

The optional `Frontend/` project is independently configured and run. Root CMake,
engine, legacy Win32 Menu and their build/test behavior remain independent. A
Python standard-library CLI proves orchestration; a standalone C++17 inspector
uses the existing fixed-width codec. This is not a final rendering/toolkit choice.

| Component | Owns | Does not own |
| --- | --- | --- |
| `store` | JSON identity registry, locks, backups, atomic persistence | native progression |
| `discovery` | coherent roots, case-safe references, revisions, relocation evidence | runtime certification, mod deduplication |
| `profiles` + codec helper | raw state-set inventory, hashes, byte-preserving copies, bounded non-mutating inspection | normalization, slot allocation, save writes |
| `catalog` | ordered source observations, provenance, bounded dialect projections | universal species identity, gameplay evaluation |
| `launch` | revision-bound intent, validation, price display, dry-run result, return refresh | currency debit, rank mutation, shell execution |
| `sessions` + `session_io` | pinned evidence, independent copies, durable journal | association mutation, native slot creation |
| `session_runner` + `reconciliation` | fixed synthetic subprocess, argv/cwd, bounded wait/logs, candidates/quarantine | arbitrary executables, native engine launch, state promotion |
| `genesis` | revision-pinned current-MEE observer intent | broad MEE support, engine certification |
| future presentation | lodge/console replacement, navigation, hide/resume | native persistence semantics |

Lossless codecs stay in `Shared/LegacyProfile.h`; the frontend helper calls them
without including engine/menu runtime structures. Recognize exact complete C2
sizes as layout candidates only. Ice Age and unknown extensions stay opaque.
Header-only observations cannot certify a save. Raw trophy words and float bits
are separate from content metadata, which must be revision-bound.

Catalog input is selected per adapter: `_MENU.TXT` observations take precedence
over `_RES.TXT` for the evidenced MEE menu path. Preserve order, raw values,
duplicate assignments, file/line, and ambiguous blank/instruction-like names.
Creature rows are selectable licenses of unresolved cardinality, not species;
AI numbers are attributes, never keys. Physical map inventory is separate from
advertisements (price-list slots, explicit declarations, eventually supported
`.c2map` descriptors). Surplus prices remain diagnostics, not invented entries.
Asset references return found/missing/ambiguous/unsafe with original spelling.
Artwork references are exposed because text parsing cannot recover baked-in
instructions. Legacy descriptions are byte-preserving Latin-1 projections with
an explicit encoding diagnostic, not claims about the original code page.

Dialect recognition and projection are capability-specific. Unknown grammar can
still yield raw observations, but no universal progression policy. MEE generations,
C2, Ice Age/classic Triassic and custom binaries must be distinguished. Rank,
unlock, debit, score multipliers and mode behavior require a pinned semantic
adapter with evidence. Initial price sums are informational; neither association
nor dry-run modifies native score, options or rank.

Structured launch intent names hunter/instance/revision/association, ordered
catalog entry IDs, source slot, time/mode and host overrides. It reports separate
statuses for recognized installation/dialect, console projection, save readability,
association, modern-engine compatibility, launch testing, hunt/save round trip,
trophy interpretation and Windows fallback evidence. Unknown is not false and
static inspection never promotes runtime capabilities.

## Engine integration deliberately deferred

Current `Menu/Menu.cpp` assembles masks/argv and saves options before launch; it does not subtract the displayed selection cost.
`Hunt/Game/CommandLine.cpp` supplies `reg=`, `prj=`, `din=`, `wep=`, `dtm=` but
does not separate content root from writable profile root or provide a state-set
transaction/ownership handshake. It and platform/process/display files are hot
in `port/linux-display-behavior` and `port/linux-display-persistence`; root
CMake/CI/SDL are also hot. All remain read-only for the session milestone.

Do not execute hunts against referenced state or substitute a copied `.sav`
into an installed slot. Native execution stops at a blocked plan until the
[engine session seam](ENGINE_SESSION_SEAM.md) is agreed and implemented. The
[controlled synthetic session](SESSION_MODEL.md) exercises child ownership,
durable journaling and candidate-only reconciliation without certifying a hunt.

Future small extractions after engine work lands: pure menu script observations;
dialect-specific price/rank/accessory policy; selection-to-mask validation;
structured launch argv adapter; native state-root support and prelaunch/return
transaction; revision-aware trophy metadata resolver. Keep these independent of
GDI, Win32 windows/messages, hit maps, lodge rendering and navigation state.
