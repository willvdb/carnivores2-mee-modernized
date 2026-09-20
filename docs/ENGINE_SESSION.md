# Engine session isolation v1

This opt-in contract separates an engine's writable files from its content
installation. It preserves the native 1660-byte SAV, 7176-byte SAB and all
keybinding bytes; the pair is still saved independently, not transactionally.

Query the explicitly trusted build with `--session-capabilities` as its only
argument. It emits JSON (`contract: c2-engine-session`, `version: 1`,
`layout: state-config-output-v1`, `state: sav-sab-pair`,
`performance_capture: false`) and exits without platform/graphics, logging,
configuration or profile initialization. Pin the executable hash alongside this
response; an arbitrary binary's response is not a trust or sandbox certificate.

Launch with the content installation as cwd and these exact, case-sensitive,
nonrepeatable arguments (each `=value` remains one argv element):

```sh
/path/to/trusted/Carnivores1_GL \
  --session-contract=1 --session-slot=0 \
  '--session-root=/absolute/session/work' \
  '--session-source=/absolute/imported snapshot' \
  '--session-baseline=/absolute/session/baseline' \
  'prj=HUNTDAT\AREAS\AREA1' din=0 wep=0 dtm=1 -observ
```

All directories already exist. No shell interpolation is required by the API.
The root contains exactly `state/`, `config/`, `output/`. `state/` and the
independent immutable `baseline/` contain exactly `trophy0N.sav` and
`trophy0N.sab`, for slot N=0..7. Source contains those same complete files (and
may contain other existing files). The three state copies must have identical
bytes and distinct file identities, with matching embedded SAV registration.
V1 deliberately requires a complete pair, even for legacy profiles for which
SAB could otherwise be optional. It never generates a missing member or repairs
one. The existing codecs validate structure/length, not gameplay semantics or
atomic pair history; their opaque words and room version remain opaque.

`config/` is empty or contains only `config.cfg` (at most 1 MiB). The engine
creates its existing default there when absent. It never reads or creates
configuration beside the executable or in cwd in session mode. Existing
config/profile/CLI display precedence is retained. `output/` starts empty.
Structured logs, render/debug logs, screenshots and ordinary file exports route
there. GL performance logging/captures, multiplayer and Windows legacy audio
DLL selection are disabled in v1; OpenAL remains available. stdout/stderr belong
to the caller's process capture. No config or output joins the reconciled pair.

The policy is established at the beginning of `RunGame`, before platform init
and both logs. Session options are removed before the legacy substring parser;
argv[0] is not interpreted in session mode. `reg=` is rejected in favor of the
strict session slot. Project names must be bounded, relative and traversal-free.
Content reads retain legacy case/separator resolution and module shader fallback.
Without session arguments all original launch/config/profile/display paths remain.

Writable roots cannot be equal to, ancestors of, or descendants of cwd, module,
source or baseline directories. Source and baseline must be independent. Session
paths reject traversal, links (including Windows reparse points/junctions), special
files and files with multiple hardlinks; selected file identities are compared.
Session-owned output paths are flat names or their exact assigned absolute paths;
arbitrary absolute/nested output overrides and other profiles are rejected.
Checks repeat at file-open/config/profile boundaries. Missing profiles after
startup fail instead of falling back or being recreated. Writable file open,
short write, flush and close failures latch a nonzero result and prevent subsequent
profile saves. Setup errors use stderr and exit 2 before logs; I/O failures cannot
turn a zero shutdown into success (exit 3). Fatal runtime errors exit nonzero in
session mode after closing logs and normal shutdown services.

This is engine-owned path isolation for trusted code on a quiescent local
filesystem. It is not an OS sandbox, hostile-file race defense, disk durability
transaction, or containment of third-party libraries/drivers and arbitrary mod
binaries. Do not mutate mounts/files/ancestors or run another writer during a
session. Driver caches and external library diagnostics are outside this engine
contract. Frontend candidates still require whole-set inspection after process
ownership has established quiescence; never infer success from exit code alone.

Validation and acceptance limits: [NATIVE_SESSION_HANDOFF](NATIVE_SESSION_HANDOFF.md).
