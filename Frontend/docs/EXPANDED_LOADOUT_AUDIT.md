# Genesis expanded loadout source audit

Audited checkout `766a6d2`, 2026-09-29. Follow-up to open PR #33, based on
`frontend/hunt-loop`. Source characterization is not interactive gameplay evidence.

- `Menu/Menu.cpp::CalculateDebit`, selection toggles and Hunt/Next: one area,
  any nonempty subset of licenses and weapons, independently toggled accessories.
  Sum all selected prices; removal is unconditional. Launch requires nonzero din
  and wep outside observer mode. No score subtraction occurs at launch.
- `MenuEventStart`: licenses are filtered definition-order AI >= 10 entries,
  including grouped licenses; no species expansion or active rank filter.
- Hunt/Next ORs ordinal bits. `Hunt/Game/CommandLine.cpp` accepts masks 0..1023,
  multiplies din by 1024, and leaves wep unshifted. ScriptParser tests char0..9
  at bits 10..19 and caps weapons at 10. Pinned counts are nine licenses and
  eight weapons: supported maximum masks 511 and 255, with no pre-shift.
- `CharacterLoader.cpp::refillWeapons` initializes every possessed weapon and
  chooses the first as initial target. Double ammo doubles reserve shots for
  reload/no-reload-animation weapons, otherwise adds a spare magazine.
  `Hunt.cpp` number keys 1..9/0 switch by slot when ammunition exists; switching
  waits for animations and respects underwater/harpoon restrictions. Survival
  and trophy modes block that path. No one-weapon restriction exists in hunt.
- `Menu/Resources.cpp::LoadResources` maps the first four accessory prices to
  camouflage, radar, cover scent, double ammo. The installed pinned `_MENU.TXT`
  has exactly four `acces` lines: 10, 100, 20, 50. Catalog IDs equipment:0..3
  retain those positional identities. Generic catalog meanings remain unresolved;
  only the pinned policy may resolve them. No defaults or extra IDs are invented.
- Launch flags are -camo, -radar, -scent, -double. No mutual exclusion or time
  restriction exists for these four. InitEngine disables them initially;
  EngineProfile::ApplyOptions does not restore them from SAV. CaptureOptions
  records native runtime camo/radar/scent/tranq on native save. The frontend must
  neither copy Menu's false-branch option mutation nor alter native bytes.
- SubmitDinoScore multiplies by tranq, radar, scent, camo in that order, then
  truncates the float award to int. Pinned scripts have no accessory overrides;
  defaults are camo .85, radar .70, scent .80, double 1, tranq 1.25, observer 1.
  Double ammo has no multiplier use in scoring. Multiple active modifiers stack.
- Modern Menu also offers night vision and tranquilizers with fallback prices.
  Neither has a catalog identity in this revision's four observed accessory
  slots. They remain unsupported here, as do observer/cheat/survival/multiplayer
  flags, other revisions, unresolved prices, unknown/duplicate IDs and ordinals
  outside the pinned catalog. No synthetic catalog entries or prices are added.

Expanded semantics use `genesis-current-mee-hunt-v2`. Existing v1 pins must be
re-evaluated with the unchanged strict v1 policy for run, reconciliation and
acceptance. New preparations use v2; historical pins are never retargeted.
Policy identifiers use existing fields; codecs, disk/wire formats and store
schema versions remain unchanged. An older frontend must reject v2 provenance.
