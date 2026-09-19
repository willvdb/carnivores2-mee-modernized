# Engine session seam required before native launch

Inspected main: `7ab7d47c77c5968ae1e501a1dd2cdfaf406edceb`. This document describes
observed behavior and a proposed bounded contract; it does not implement an
engine interface or depend on unmerged display branches.

## Observed blocker

* `Hunt/Game/CommandLine.cpp::ProcessCommandLine` supplies `reg=`, `prj=`, masks,
  time and observer/score arguments. There is no explicit content/profile/output
  root pair. `prj=` selects a map project, not the root for all content.
* `Hunt/Game/Hunt.cpp` and `Hunt/Loaders/Resources.cpp` load many fixed relative
  `HUNTDAT/...` resources. An absolute map project does not relocate those reads.
* `Hunt/Game/Trophy.cpp::LoadTrophy/LoadTrophy2/SaveTrophy/SaveTrophy2` use relative
  `trophy0%d.sav/.sab`. They resolve against cwd through `Platform::OpenFile`.
  SaveTrophy updates runtime rank, writes SAV, then writes SAB independently.
* `Hunt/Platform/Files.cpp` normalizes/case-resolves paths but does not select
  separate content and state domains. `FindShader` also searches beside the module.
* `Hunt/Game/EngineInit.cpp::GetConfigPath` prefers configuration beside the
  executable over cwd. `CreateDefaultConfig` writes beside the executable when
  its module directory is available. Setting cwd to a session therefore does not
  contain all writes, even if content were available there.
* Logs (`Hunt/Debug/Log.cpp`, `Hunt/Game/Hunt.cpp`,
  `Hunt/Loaders/Resources.cpp`), screenshot/debug exports
  (`Hunt/Platform/Screenshot.cpp`, `Hunt/Loaders/Resources.cpp`) and optional
  performance captures (`Hunt/Renderer/GLPerf.cpp`) also need an explicit writable
  destination or disabling in session mode.

Cwd at the installation exposes its native profiles. Cwd at an isolated workspace
cannot locate all existing content. Copying only the executable does not solve
this. No 768 MB asset copy, content symlink/junction, hardlink overlay or temporary
replacement of an installed slot was attempted. The native-launch portion stops
here; a known binary hash alone cannot remove this architectural blocker.

## Smallest recommended portable contract

Consider an opt-in engine **session mode** with two explicit absolute roots:
read-only content and frontend-owned writable session output. Names/flags remain
to be agreed with engine maintainers. The output root can contain a state subdir
and log/config/screenshot subdirs; the exact layout is less important than the
enforced read/write domains.

1. Resolve every content read relative to the selected content root; preserve
   legacy filename casing/separator behavior. Required engine-owned shaders may
   read from a pinned engine resource directory, without module-directory writes.
2. Read and write the selected slot only under the session state root. Reject
   traversal/absolute overrides for project/slot paths and missing/unreadable
   baseline files. Never fall back to an installed profile. Do not create an
   unknown native profile as a substitute.
3. In session mode, use session-local configuration and outputs. Disable the
   module-directory config fallback/default creation. Route or disable all log,
   screenshot, debug and perf writes. Explicitly separate general outputs from
   the state set whose members the frontend reconciles.
4. Leave the existing default launch behavior intact when session mode is absent.
   Publish a contract/version or query capability that can be associated with a
   reviewed engine build hash. A supported flag's spelling alone is insufficient.
5. Return a normal child exit status after closing writable handles. Continue
   using unchanged native SAV/SAB bytes and independent writes; the frontend
   captures the quiescent set and quarantines anomalies. An engine-side atomic
   save redesign is not required for the first candidate-only proof.

Likely edits: engine command-line/init, Trophy load/save, Platform file resolution,
resource readers and output sinks listed above, plus targeted engine tests. Root
CMake/CI may need test integration. These include hot engine files and were kept
read-only. `port/linux-display-behavior` and `port/linux-display-persistence` were
inspected for overlap, not imported. The frontend changes are entirely under
`Frontend/` and require only merged main behavior.

The frontend should continue to produce structured, revision-bound intent. A
future certified adapter translates it to the agreed engine contract, and the
OS runner owns only process lifecycle. Do not turn the fixed synthetic runner
into an arbitrary command launcher. After the seam lands, add a separately pinned
engine adapter and perform one disposable observer session with candidate-only
return reconciliation before designing authoritative state promotion.
