# Resolved huntable presentation

`catalog_presentation.hpp` adds `resolve_huntables(root, projection)` as a typed
semantic view of the selected menu script. The immutable v1 raw projection and
its JSON, identities, reference candidates, Python oracle and launch-plan schema
remain unchanged. Consumers displaying a roster should use this resolved view,
not choose an arbitrary raw candidate or derive an asset number from AI.

Huntables (AI >= 10) retain file order, including repeated AI. Their thumbnail is
`HUNTDAT/MENU/PICS/DINO<one-based roster position>.TGA` and description is
`HUNTDAT/MENU/TXT/DINO<position>.TXM`. Nonempty `pic` overrides only the thumbnail.
The portable resolver handles spelling, case ambiguity and unsafe paths. Stock
Iguanodon/Carnotaurus/T-Rex therefore resolve slots 8/9/10 despite AI 17/17/18.

`legacy_integer(Attribute)` uses the same valid, representable numeric-prefix
semantics as the engine's `ParseScriptIntStatus`: e.g. `17L` -> 17 and `100.0f` ->
100. It applies only when a consumer explicitly requests an engine integer
interpretation. The raw parser is not weakened. Malformed/overflowing AI leaves
subsequent slot positions unresolved; missing AI remains non-huntable. Prices
follow upstream roster order starting at the first AI 10 (or the first huntable),
and malformed, negative or surplus prices produce diagnostics. No recovery value
is used to grant a selection or purchase.

This view is for presentation, not purchase/launch authority. The existing frozen
launch planner deliberately remains fail-closed on non-integer raw prices and
raw entries it cannot resolve. Permitting such plans requires an explicit schema
and policy migration; this integration does not silently change that contract.
