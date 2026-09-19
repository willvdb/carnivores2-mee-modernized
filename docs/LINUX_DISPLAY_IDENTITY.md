# Linux saved monitor preferences (Phase 4h)

Current live Linux recovery and dimension semantics: [Phase 4i](LINUX_DISPLAY_RECOVERY.md). Earlier deferred descriptions below are historical.

Linux now supports opt-in serial-backed monitor preference on supported native
backends. Run `-list-displays`, then copy the desired complete `display_identity`
line into modern `config.cfg`. The token is intentionally opaque. No session
index, last-used monitor, connector name or window position is saved automatically.
Absent key or `display_identity primary` retains the default behavior.

The existing session overrides remain `-display=primary`, `-display=N` and
`-display-id=<token>` (slash prefixes also work). Defaults/profile, config, then
final CLI remain the precedence order. Invalid arguments clear the effective
preference and do not reach legacy substring parsing. The real Menu writer keeps
Linux/Windows identities, unknown versions/domains and comments unchanged.
Discovery exits before asset/config/profile loading or game-window creation; it
writes only the existing diagnostic logs plus stdout. Tokens contain monitor
serial metadata, so consider that when sharing a discovery log.

## Supported contracts

| Backend | Identity domain / requirements | Meaning and limits |
| --- | --- | --- |
| Native X11 RandR | `v1:linux-x11-edid-serial`, bundled SDL mapping extension, valid EDID with meaningful serial metadata | Manufacturer/product and supplied numeric/text serial, not connector or X server resource. Conditional on accurate, stable EDID from the panel/driver. |
| Direct Wayland | `v1:linux-wayland-wlr-serial`, SDL exact wl_output property, xdg-output >=2 and wlr-output-management >=3, meaningful make/model/serial | Compositor-reported tuple intended for recognizing outputs between sessions. Conditional on the compositor continuing to report the same accurate metadata. |
| XWayland | Normally unavailable: virtual RandR outputs do not expose the physical EDID | Never borrow host DRM or another Wayland connection's metadata. If an X server supplies genuine validated EDID through an exact mapped RandR output, it is subject to the same X11 contract; virtual/synthetic EDID is not proof of a physical panel. |
| Other Wayland compositors / old output-management versions | Explicitly unsupported when required protocols or metadata are absent | Core wl_output names, descriptions and geometry do not supply a persistent serial contract. |
| Installed SDL | Wayland works if its public native-output property is present; X11 requires the documented project extension | No library is silently modified; no private ABI fallback. |
| Windows native / SDL | Existing `win-monitor-interface` domain unchanged | Windows OS registration semantics remain separate from Linux serial metadata. |

A returned serial is evidence supplied by the display stack, not a cryptographic
hardware identity. No protocol can detect a replacement panel that dishonestly
reuses exactly the same metadata while the original is absent. Never promise
universal physical-panel uniqueness from EDID or compositor serial strings.
Currently connected duplicate tuples, including disabled native outputs, are
rejected together; no first-match selection. Missing serials, common placeholders,
empty, malformed, overlong or truncated metadata remain unavailable. Native
serial strings are conservatively limited to printable ASCII, at most 128 bytes
per field; unsupported encodings are rejected, never normalized or truncated.

The X11 parser requires a complete EDID 1.3/1.4 payload, valid header, valid
manufacturer/product and checksums on every declared 128-byte block. Numeric
zero/one/all-ones placeholders are rejected. A serial descriptor must have valid
printable text and padding, and must occur at most once. Both meaningful serial
forms are retained when present. Mode/color data, EDID extension contents, names,
size and position are excluded from the key. This is not a full EDID validator.

Wayland uses a length-delimited make/model/serial tuple. Case and bytes are
preserved. Core `wl_output.name` explicitly does **not** promise cross-session
persistence. The wlr protocol's serial fields do provide a previous-session
recognition purpose. Its enabled head name must equal the corresponding output
name; historical v2/v3 explicitly specify xdg_output.name. We create our own
xdg-output v2 object for SDL's exact borrowed wl_output on SDL's exact connection.
That name is only a unique same-session protocol join and is never persisted.
Neither friendly SDL names nor matching bounds substitute for this association.

Version 3 is required for explicit head/mode release requests. The reader makes
no configuration/create/apply/test requests. It uses its own event queue with a
500 ms whole-discovery/cleanup deadline and leaves SDL's event dispatch alone.
A timed-out queue is retained until a later bounded drain can release late
server-created objects; additional failed reads do not accumulate queues.
Immediately before SDL disconnect, any remaining reader objects are released.
This bounded metadata query does not change the inherited fullscreen roundtrip
behavior or promise general compositor recovery.

## Selection, stability and fallback

Each video-mode application resolves saved intent against a fresh owned catalog.
An exact unique version/domain/value match selects the target's current unique
bounds and its own eligible refresh modes. Reordering or moving outputs cannot
change the serial key. Missing, ambiguous, unsupported or unusable identities
use primary/default **and automatic refresh**, even if primary supports the old
rate. The saved preference remains unchanged and can recover on a later mode
application or launch. Cloned native CRTCs and duplicate catalog bounds cannot
be targeted as distinct panels.

Replug/reboot, connector/dock/GPU changes may work when the same native metadata
is still reported; these are conditional capabilities, not hardware acceptance
claims. Docks, KVMs, EDID overrides, firmware, compositor/driver/OS changes can
omit or change that metadata. A replacement with different serial data uses safe
fallback until the user explicitly updates the preference. Changing between
X11, Wayland and Windows does not translate domains or guess an equivalent panel.
Returning to the original supported backend can recover the stored preference.

The inherited exactly-one-current-rectangle remapping remains the native
application race boundary; discovery and mode application are not an atomic
hotplug transaction. Active-game topology recovery/scaling is Phase 4i work.
All Phase 4g fullscreen outcome checks, direct Wayland relative input and the
separate SDL disconnect fix remain. No renderer, input-binding, Menu/frontend or
legacy serialization changes: `.sav` 1660 bytes, `.sab` 7176 bytes, signed OptRes
and all 68 keybinding bytes retain their golden fixtures.

## Evidence and remaining acceptance

- Portable synthetic tests cover owned lifetimes, EDID integrity/serial rejection,
  unambiguous native/protocol joins, duplicate disabled heads, reorder/movement,
  target-specific rate, absent/changed backend and primary/automatic recovery.
- A real private Wayland protocol server exercises the production reader on a
  separate test-only connection with exact borrowed SDL properties. Repeated
  reads leave zero fixture metadata objects and issue zero configuration writes.
  Tests include missing/v2 protocol, duplicate serials, timeout and late-head
  cleanup/recovery. This is protocol integration, not physical acceptance.
- A disposable two-output Xorg fixture supplies synthetic EDID and exercises the
  actual SDL mapping extension and production catalog, then mutates duplicate,
  corrupt and truncated metadata and verifies recovery without reinitialization.
  The inherited 20 fullscreen cycles per output still restore desktop modes with
  zero SDL allocations. No host EDID/property/display changes are made.
- Read-only discovery on Allosaurus Hyprland obtains three distinct tokens,
  including two identical-model Dell P2417H monitors with different serials.
  No window/config/profile is created. The current host has no running XWayland
  endpoint, so physical XWayland enumeration cannot be accepted here.
- Physical reboot/replug/dock/GPU transitions, physical Windows multi-monitor
  checks and the earlier Hyprland/XWayland exclusive limitation remain open.
  Synthetic/nested tests do not replace those checks. Existing unsuppressed SDL
  dummy UBSan and X11/standalone-Wayland lifetime reports remain separate.

Exact compiler, sanitizer, runtime and final pushed-SHA CI results are recorded
in `PHASE4H_CHECKPOINT.md`; local evidence is `/tmp/carnivores-phase4h`.

Primary references: [SDL mapping extension](SDL_DISPLAY_MAPPING.md),
[RandR protocol and EDID property](https://xorg.freedesktop.org/archive/current/doc/randrproto/randrproto.txt),
[libdisplay-info EDID parser](https://gitlab.freedesktop.org/emersion/libdisplay-info/-/blob/main/edid.c),
[core Wayland output contract](https://wayland.freedesktop.org/docs/html/apa.html),
[current wlr output-management protocol](https://gitlab.freedesktop.org/wlroots/wlr-protocols/-/blob/master/unstable/wlr-output-management-unstable-v1.xml),
[historical v2 contract](https://github.com/swaywm/wlr-protocols/blob/master/unstable/wlr-output-management-unstable-v1.xml),
[the upstream wording update without a protocol version bump](https://gitlab.freedesktop.org/wlroots/wlr-protocols/-/commit/a5028afbe4a1cf0daf020c4104c1565a09d6e58a).
The licensed protocol XML is vendored under `third_party/protocols`; generated
interface C symbols are prefixed to avoid collisions with SDL's bundled code.
