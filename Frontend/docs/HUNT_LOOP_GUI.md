# Hunt-loop GUI

## API audit

The public `c2/frontend/play_loop.hpp` calls the existing implementation directly.
It adds no codecs, schemas or CLI dispatch. UUIDs remain identities. Complete
backend evidence accompanies typed fields as display-only text, never parsed by
presentation code. Exceptions retain the backend diagnostic messages.

| Handoff operation | Existing public API | Added boundary |
| --- | --- | --- |
| Hunter / expedition reads | `Store::read`, `Manifest::hunters/expeditions/active_hunter` | reused; viewing selection is local |
| Association selection | none | `play_loop::associations` includes origin, ownership, authority, current generation |
| Catalog | `catalog::project` | reused |
| Plan | pure `planning::Selection`, policy and launch evaluation | `Client::plan` calls fresh store-backed `plan_hunt` |
| History | `Manifest::resolve_generation` | reused |
| Explicit schema upgrade | none | `Client::upgrade` |
| Prepare | none | `Client::prepare` |
| Run | none | `Client::run`, caller-owned atomic cancellation; returns before reconciliation |
| Inspect / reconcile | none | `Client::inspect/reconcile` |
| Preview | none | `Client::preview`, typed allowed/digest/predecessor |
| Accept | none | `Client::accept`, repeats existing locked validation |
| Recovery | none | `Client::recover/recover_acceptance` |

Client defaults to read-only. Mutations require an existing manifest and explicit
writable construction. No read upgrades metadata. Production clients always use
production policies; the private seam is reserved for owned asset-free fixtures.
The CLI is unchanged and continues to use its existing implementation.
