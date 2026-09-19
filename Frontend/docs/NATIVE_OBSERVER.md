# Experimental native observer validation

The separate `native-observer` command prepares journal **schema 2** for the
[engine session v1 contract](../../docs/ENGINE_SESSION.md). General native launch
remains disabled. Existing synthetic sessions keep schema 1, their fixed Python
fixture, five-second default, runner and recovery behavior. Static
`genesis-observer-plan` remains a nonlaunching plan.

Preparation requires an explicit reviewed engine path, its independently selected
SHA-256 and `--experimental-native-observer`. This authorizes the capability query
against that exact binary. A query response alone does not make an unknown binary
safe: do not supply an unreviewed executable's hash as a substitute for trust.
No expedition executable is automatically selected. The engine is queried again
before launch, and its path/hash, exact capability object, policy output, argv,
content revision, selected area/time/slot, codec, baseline/source and fixed config
are pinned. The selected engine hash and reconstructed execution specification are also
rechecked on return and recovery, without executing an engine query during recovery. Changing any
pin requires a new session, never editing the journal into an execution script.

Native preparation retains the existing managed-personal association requirement
and exact Genesis revision/catalog gate. V1 additionally requires a complete
existing SAV/SAB pair. No native profile, ownership, rank, migration, equipment
or progression is invented. A missing SAB blocks rather than being synthesized.
The original managed snapshot stays authoritative; source installation state is
never substituted into an installed slot or synchronized after a return.

## Layout, execution and recovery

The schema-2 directory contains the existing `journal.json`, `baseline/`,
`returned/` and bounded process `logs/`. `work/` contains exactly:

- `state/`: independent complete native pair, the only reconciled state members;
- `config/config.cfg`: pinned developer settings (800x600 windowed, 60 FPS,
  performance logging off), validated unchanged before and after execution;
- `output/`: engine logs/screenshots, initially empty. Only flat `render.log`,
  `carnivor.log` and platform screenshot names are admitted on return: Linux
  `HUNT` plus at least four digits plus `.BMP`; Windows `HUNT` plus exactly four
  digits plus `.BM` (the existing filename buffer truncates `.BMP`). Unexpected files, directories, links or over-limit captures quarantine
  the session and retain its evidence. Nothing is silently ignored or deleted.

Cwd is the content installation; all writable engine files use `work/` under the
versioned contract. Root relationships and alias checks run in both layers.
The selected executable is passed separately with an argv list and `shell=False`.
The developer validation timeout defaults to **900 seconds**, accepts 30..3600,
and is independent of synthetic timeouts. This is a bounded validation command,
not a policy for unrestricted interactive hunts. Ctrl-C cancels and reaps the
owned child; timeout/nonzero exit/capture failure can never yield a clean candidate.
The engine and fixture are trusted not to spawn descendant processes; this does
not add a general process supervisor or an OS sandbox.

`session run` cannot run schema-2 journals; the separate native run command
requires explicit engine/hash/gate again. Native run cannot run synthetic journals.
`session inspect`, `reconcile` and `recover` understand both versions. Unknown
versions or incompatible version/kind/capability combinations fail safely.
Recovery of launching/running journals records interruption, without signaling
journal PIDs, relaunching or capturing possibly live state. Durable returned
sessions can resume candidate-only reconciliation.

A completed process sets `engine_process_executed` and native lifecycle status.
It does **not** automatically set `observer_session_launched`, modern-engine
compatibility or hunt/save-round-trip validation: an exit code and readable SAV
cannot prove a world rendered or that Genesis behaved correctly. A native result
never upgrades synthetic capabilities or permits promotion.

## Reproducible asset-free smoke

No game assets or personal profiles are needed. The CTest runner requires the
real C++ profile probe and a separate compiled native test child. That child uses
the production engine path contract/file APIs and authored disposable state;
only the Genesis structural-policy gate is visibly doubled in adapter tests.
The unmodified policy is separately tested to reject this fixture's actual hash.
The existing Python synthetic fixture and all of its regressions remain intact.

```sh
cmake -S Frontend -B /tmp/c2-frontend -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/c2-frontend --config Debug
ctest --test-dir /tmp/c2-frontend -C Debug --output-on-failure --no-tests=error
```

The engine's separate asset-free acceptance suite compiles actual trophy/config/
command-line/log/screenshot/performance/termination functions and executes the
actual game binary's earliest startup paths. See the task handoff for commands.
Neither suite certifies Genesis. They select no existing personal profile.

## Human native acceptance with supplied content

Prerequisites: the exact already-owned revision in [GENESIS_POLICY](GENESIS_POLICY.md),
a reviewed task-built engine, the codec probe, and an existing eligible immutable
managed association containing both native members. Close external writers. No
assets are downloaded and the adapter refuses other revisions. The commands below
operate on disposable session copies of that explicitly selected association;
they are not the asset-free smoke or automatic progression approval.

```sh
python3 Frontend/frontend.py --store /path/to/lodge --probe /path/to/c2-profile-probe \
  native-observer prepare ASSOCIATION_UUID --area areas:0 \
  --engine /path/to/reviewed/Carnivores1_GL --trusted-engine-sha256 REVIEWED_SHA256 \
  --experimental-native-observer --timeout 900
python3 Frontend/frontend.py --store /path/to/lodge --probe /path/to/c2-profile-probe \
  native-observer run SESSION_UUID \
  --engine /path/to/reviewed/Carnivores1_GL --trusted-engine-sha256 REVIEWED_SHA256 \
  --experimental-native-observer
```

Inspect actual world entry/observer controls, normal evacuation/close, native
save byte/keybinding behavior, session output locations, and before/after source,
installation and baseline hashes. Test native Windows and Linux independently.
Keep any output candidate-only. No actual Genesis observer acceptance was run
for this task because assets/baselines were not explicitly supplied to it.
