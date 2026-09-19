# Decisions grounded in the supplied compatibility audit

Evidence: the user's comprehensive static audit, current 2026-09-18, covering
20 editions/17 families (19 edition trees inspected), at engine repository commit
`ba556538ac97cfe8ec3035bed46740ce70d13737`. It was supplied in full during this task.
The local script replay is independent confirmation of selected observations,
not a substitute for runtime evidence. No new external research was necessary.
No original assets, native saves, executables or HUNTDAT files are committed.

## Answers to the audit's implementation questions

1. **Initial promise:** conservative catalog observations for classic syntax,
   older MEE and newer MEE; exact C2-1660/MEE-7176 codec candidate inspection.
   Unknown extensions and Ice Age/classic Triassic remain opaque. No edition has
   a new frontend runtime compatibility promise.
2. **Across revisions:** exact fingerprints identify byte revisions, never save
   compatibility. Changes retain association provenance and require review;
   future transfer rules need scoped author evidence plus runtime validation.
3. **Binding:** UUID hunter → UUID association → UUID instance + native filename
   slot + paired-file hashes. Family/release evidence is separate from all IDs.
   Packaged state is never silently adopted. Copies are explicitly independent.
4. **Authority:** native referenced files or independent managed snapshot. Both
   remain read-only here. Frontend metadata writers use an exclusive lock; legacy
   writers do not honor it. Stable double reads cannot certify atomic SAV/SAB
   history. No engine execution before an isolated writable-state/session seam.
5. **Disagreement:** preserve raw native values. Neither menu nor engine policy
   becomes universal by default. In particular, no price debit is invented and
   rank is not recomputed. Current menu has eight slots (0..7).
6. **Uncertain associations:** retain ordered license and raw label/parameters;
   show competing reference candidates and diagnostics. Do not deduplicate AI
   values, split category licenses into invented species, or repair blank labels.
7. **Special modes:** only ordinary hunt/observer intent is modeled. Survival
   keywords or launcher filenames do not enable modes; slot-zero wrappers require
   a separate profile-aware adapter. No external Dinopedia executable is run.
8. **Exhibits:** defer implementation; keep native rooms authoritative and design
   for curated snapshots with raw/revision provenance. Offline snapshots and live
   installed models have different availability. No map/date/weapon history guess.
9. **Statistics:** expose raw codec observations only. “Success” is not certified
   accuracy; no cross-profile sum, currency pooling or survival leaderboard.
10. **Overlays:** incomplete roots remain unregistered. Activated replacements
    change the revision of their installation; optional packs are not merged into
    a universal asset pool. Reordered indices invalidate trophy interpretation.
11. **Supported:** capabilities are independent. Parsing, codec round trip, engine
    compatibility, actual launch, hunt/save return and trophy interpretation never
    inherit one another's status. A Windows executable is fallback evidence only.
12. **Policy location:** lossless codecs stay shared; frontend observations and
    state orchestration are separate from future edition-specific policy adapters,
    process launching and presentation. No GDI/hit-map/navigation code is shared.

## Local script replay

`tools/inspect_corpus.py` ran against the available partial audit extractions.
Those trees contain scripts and presentation material, not full installations;
most physical map files and all native saves are absent there. Accordingly they
are suitable for parser replay, not coherent-root or engine execution validation.

All 19 edition trees produced the audited selectable-license/weapon counts. The
observed families were eight newer MEE, four older MEE and seven classic-syntax
candidates. Classic Triassic cannot be distinguished from C2 syntax alone and
needs its separately evidenced Ice Age hint. “Area” below means positional
advertised price slots, including missing files; it never means playable maps.

| Local edition | Area slots | Licenses | Weapons | Priced accessory slots |
| --- | ---: | ---: | ---: | ---: |
| C+ MEE / TCE | 8 | 10 | 8 | 4 |
| Carnage | 9 | 9 | 6 | 4 |
| Dinosaur Hunter Reborn | 8 | 10 | 8 | 4 |
| Archipelagos | 2 | 8 | 4 | 4 |
| Dangergrounds | 6 | 9 | 9 | 4 |
| Fallen Kings | 7 | 8 | 8 | 4 |
| Far North | 7 | 8 | 7 | 4 |
| Genesis Redux | 8 | 9 | 8 | 4 |
| Legacy | 7 | 10 | 7 | 4 |
| Reloaded | 8 | 9 | 8 | 4 |
| Desperation MEE | 8 | 10 | 8 | 4 |
| JPHL Special Edition | 7 | 9 | 8 | 4 |
| Classic Triassic | 8 | 10 | 8 | 5 |
| Classic Carnivores+ | 8 | 10 | 5 | 4 |
| Classic Mandibles | 8 | 10 | 7 | 4 |
| Mandibles Redux | 6 | 9 | 7 | 4 |
| Sanctuary | 4 | 9 | 6 | 4 |
| Triplex | 3 | 8 | 5 | 4 |
| Triassic MEE | 8 | 10 | 8 | 5 |

Additional parser observations: DHR's extracted `_RES.TXT` reaches EOF inside
`prices`; Dangergrounds has unmatched nesting rooted at `packtable`. The frontend
reports `unclosed-block`, retains parsed observations, and blocks validated launch
selection. These are static balance observations, not a claim about what the
packaged executable tolerates. No repair was made to source content.

## Native state evidence

Nine files were copied **only into a new temporary validation directory** from
five already-local archives. Every known-layout file passed the existing codec's
exact decode/encode comparison in memory; inspection left all extracted bytes
unchanged. No downloaded executable was run.

| Archive | State observed |
| --- | --- |
| Mandibles Redux 1.1.1 | 1660/7176 pair plus orphan `trophy01.sab` |
| Legacy 1.4.5 | 1660/7176 pair |
| Triassic MEE | 1660 save under MODDAT, no invented companion |
| C+ MEE / TCE | 1660 save, no invented companion |
| Triplex 4.1 | 1660/7176 pair |

This proves layout-level lossless inspection, not native ownership, progression,
variant/species identity or cross-version save compatibility. In particular,
`.sab` version words are retained, not promoted to an engine compatibility key.

## Compatibility debt deliberately not fixed

* Current Menu `ReadPrices` can overrun definition vectors on Far North surplus
  prices. New projection bounds-checks its own observations; Menu is unchanged.
* Menu AI-based art/text lookup differs from positional launch masks. The new
  catalog presents both reference candidates and keeps duplicate IDs independent.
* Older MEE split-block engine dispatch remains absent. Recognition reports it;
  no engine parser refactor is attempted.
* Menu/engine rank policies disagree, and the displayed cost has no evidenced
  launch-time subtraction. No progression mutation is introduced.
* Native writers update `.sav`/`.sab` sequentially, menu deletion can orphan a
  room, and filenames diverge beyond single-digit registrations. No slot expansion.
* Equipment descriptions, regional variants, mode availability and menu artwork
  contain semantics the parser cannot infer. No stock grant/meaning is invented.
* `.c2map` resource-path/launch mapping and large-selection transport remain
  unvalidated. Files are observed without claiming support.
