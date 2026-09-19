#include "DisplayIdentityLinuxNative.h"
#include "DisplayIdentityLinux.h"
#include "wlr-output-management-client.h"
#include "xdg-output-client.h"
#include <wayland-client.h>
#include <poll.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>

namespace Platform::LinuxIdentity {
namespace {
using Clock = std::chrono::steady_clock;
bool Roundtrip(wl_display* display, wl_event_queue* queue, Clock::time_point deadline)
{
    bool done = false;
    auto* wrapper = static_cast<wl_display*>(wl_proxy_create_wrapper(display));
    if (!wrapper) return false;
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapper), queue);
    wl_callback* callback = wl_display_sync(wrapper);
    wl_proxy_wrapper_destroy(wrapper);
    if (!callback) return false;
    static const wl_callback_listener listener = {
        [](void* data, wl_callback*, std::uint32_t) { *static_cast<bool*>(data) = true; }
    };
    wl_callback_add_listener(callback, &listener, &done);
    bool success = true;
    while (!done && success) {
        if (Clock::now() >= deadline) { success = false; break; }
        while (wl_display_prepare_read_queue(display, queue) != 0) {
            if (wl_display_dispatch_queue_pending(display, queue) < 0) { success = false; break; }
        }
        if (!success) break;
        // prepare_read succeeded: always pair it with read_events or cancel_read.
        if (done) { wl_display_cancel_read(display); break; }
        const int flush = wl_display_flush(display);
        if (flush < 0 && errno != EAGAIN) { wl_display_cancel_read(display); success = false; break; }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        pollfd fd{wl_display_get_fd(display), static_cast<short>(POLLIN | (flush < 0 ? POLLOUT : 0)), 0};
        const int result = poll(&fd, 1, remaining > 0 ? static_cast<int>(remaining) : 0);
        if (result < 0 && errno == EINTR) { wl_display_cancel_read(display); continue; }
        if (result <= 0 || (fd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            wl_display_cancel_read(display); success = false; break;
        }
        if (fd.revents & POLLIN) {
            if (wl_display_read_events(display) < 0 || wl_display_dispatch_queue_pending(display, queue) < 0) success = false;
        } else wl_display_cancel_read(display);
    }
    wl_callback_destroy(callback);
    return success && done;
}
struct Discovery;
struct Head {
    Discovery* owner = nullptr;
    zwlr_output_head_v1* proxy = nullptr;
    Output output;
    std::string make, model, serial;
    bool finished = false;
};
struct OutputName {
    zxdg_output_v1* proxy = nullptr;
    std::uint64_t key = 0;
    std::string name;
    bool done = false;
};
struct Discovery {
    wl_display* display = nullptr; // Borrowed SDL connection; never disconnect it.
    wl_event_queue* queue = nullptr;
    wl_registry* registry = nullptr;
    zwlr_output_manager_v1* manager = nullptr;
    zxdg_output_manager_v1* xdg = nullptr;
    std::vector<std::unique_ptr<Head>> heads;
    std::vector<zwlr_output_mode_v1*> modes;
    std::vector<std::unique_ptr<OutputName>> names;
    bool complete = false, removed = false, managerFinished = false, stopRequested = false;
    ~Discovery() {
        for (auto& name : names) zxdg_output_v1_destroy(name->proxy);
        for (auto* mode : modes) zwlr_output_mode_v1_release(mode);
        for (auto& head : heads) zwlr_output_head_v1_release(head->proxy);
        if (manager) {
            if (!managerFinished && !stopRequested) zwlr_output_manager_v1_stop(manager);
            zwlr_output_manager_v1_destroy(manager);
        }
        if (xdg) zxdg_output_manager_v1_destroy(xdg);
        if (registry) wl_registry_destroy(registry);
        if (display) wl_display_flush(display);
        if (queue) wl_event_queue_destroy(queue);
    }
};
// A timeout can precede delivery of server-created head IDs. Keep that queue
// alive until a later bounded drain receives them; otherwise destroying the
// manager proxy could orphan unreleasable server resources on SDL's connection.
std::unique_ptr<Discovery> retiring;
bool Finish(Discovery& discovery, Clock::time_point deadline)
{
    if (!discovery.queue || !discovery.registry) return true;
    if (!Roundtrip(discovery.display, discovery.queue, deadline)) return false;
    if (discovery.manager && !discovery.managerFinished && !discovery.stopRequested) {
        zwlr_output_manager_v1_stop(discovery.manager);
        discovery.stopRequested = true;
    }
    return !discovery.manager || discovery.managerFinished ||
        (Roundtrip(discovery.display, discovery.queue, deadline) && discovery.managerFinished);
}
struct Snapshot {
    std::unique_ptr<Discovery> owned = std::make_unique<Discovery>();
    Clock::time_point deadline;
    ~Snapshot() {
        if (!Finish(*owned, deadline)) retiring = std::move(owned);
    }
};
static const zwlr_output_mode_v1_listener modeListener = {
    [](void*, zwlr_output_mode_v1*, int, int) {},
    [](void*, zwlr_output_mode_v1*, int) {},
    [](void*, zwlr_output_mode_v1*) {},
    [](void*, zwlr_output_mode_v1*) {}
};
static const zwlr_output_head_v1_listener headListener = {
    [](void* data, zwlr_output_head_v1*, const char* name) { auto& head = *static_cast<Head*>(data); head.owner->complete = false; head.output.name = name; },
    [](void*, zwlr_output_head_v1*, const char*) {},
    [](void*, zwlr_output_head_v1*, int, int) {},
    [](void* data, zwlr_output_head_v1*, zwlr_output_mode_v1* mode) {
        auto& owner = *static_cast<Head*>(data)->owner;
        owner.modes.push_back(mode); wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(mode), owner.queue);
        zwlr_output_mode_v1_add_listener(mode, &modeListener, nullptr);
    },
    [](void* data, zwlr_output_head_v1*, int enabled) { auto& head = *static_cast<Head*>(data); head.owner->complete = false; head.output.active = enabled != 0; },
    [](void*, zwlr_output_head_v1*, zwlr_output_mode_v1*) {},
    [](void*, zwlr_output_head_v1*, int, int) {},
    [](void*, zwlr_output_head_v1*, int) {},
    [](void*, zwlr_output_head_v1*, wl_fixed_t) {},
    [](void* data, zwlr_output_head_v1*) { auto& head = *static_cast<Head*>(data); head.owner->complete = false; head.finished = true; },
    [](void* data, zwlr_output_head_v1*, const char* value) { auto& head = *static_cast<Head*>(data); head.owner->complete = false; head.make = value; },
    [](void* data, zwlr_output_head_v1*, const char* value) { auto& head = *static_cast<Head*>(data); head.owner->complete = false; head.model = value; },
    [](void* data, zwlr_output_head_v1*, const char* value) { auto& head = *static_cast<Head*>(data); head.owner->complete = false; head.serial = value; },
    [](void*, zwlr_output_head_v1*, std::uint32_t) {}
};
static const zwlr_output_manager_v1_listener managerListener = {
    [](void* data, zwlr_output_manager_v1*, zwlr_output_head_v1* proxy) {
        auto& owner = *static_cast<Discovery*>(data);
        owner.complete = false;
        auto head = std::make_unique<Head>(); head->owner = &owner; head->proxy = proxy;
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(proxy), owner.queue);
        zwlr_output_head_v1_add_listener(proxy, &headListener, head.get());
        owner.heads.push_back(std::move(head));
    },
    [](void* data, zwlr_output_manager_v1*, std::uint32_t) { static_cast<Discovery*>(data)->complete = true; },
    [](void* data, zwlr_output_manager_v1*) { static_cast<Discovery*>(data)->managerFinished = true; }
};
static const zxdg_output_v1_listener nameListener = {
    [](void*, zxdg_output_v1*, int, int) {},
    [](void*, zxdg_output_v1*, int, int) {},
    [](void* data, zxdg_output_v1*) { static_cast<OutputName*>(data)->done = true; },
    [](void* data, zxdg_output_v1*, const char* name) { static_cast<OutputName*>(data)->name = name; },
    [](void*, zxdg_output_v1*, const char*) {}
};
static const wl_registry_listener registryListener = {
    [](void* data, wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t version) {
        auto& owner = *static_cast<Discovery*>(data);
        // Version 3 provides explicit head/mode release. Version 2 serials alone
        // would leak server-side objects on repeated snapshots on this connection.
        if (!std::strcmp(interface, "zwlr_output_manager_v1") && version >= 3 && !owner.manager) {
            owner.manager = static_cast<zwlr_output_manager_v1*>(wl_registry_bind(registry, name, &zwlr_output_manager_v1_interface, 3));
            zwlr_output_manager_v1_add_listener(owner.manager, &managerListener, &owner);
        }
        if (!std::strcmp(interface, "zxdg_output_manager_v1") && version >= 2 && !owner.xdg)
            owner.xdg = static_cast<zxdg_output_manager_v1*>(wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, 2));
    },
    [](void* data, wl_registry*, std::uint32_t) { static_cast<Discovery*>(data)->removed = true; }
};
}
void DiscoverWayland(DisplayCatalog& catalog, const SDL_DisplayID* ids, int count)
{
    auto status = [&](const char* reason) { for (auto& output : catalog.displays) output.identityStatus = reason; };
    status("Wayland requires exact SDL wl_output mapping, xdg-output v2 and wlr-output-management v3 with serial metadata");
    const auto deadline = Clock::now() + std::chrono::milliseconds(500);
    if (retiring) {
        if (!Finish(*retiring, deadline)) { status("previous Wayland identity snapshot has not finished"); return; }
        retiring.reset();
    }
    Snapshot snapshot{std::make_unique<Discovery>(), deadline};
    auto& discovery = *snapshot.owned;
    discovery.display = static_cast<wl_display*>(SDL_GetPointerProperty(SDL_GetGlobalProperties(), SDL_PROP_GLOBAL_VIDEO_WAYLAND_WL_DISPLAY_POINTER, nullptr));
    if (!discovery.display) return;
    std::vector<wl_output*> native;
    std::vector<std::uint64_t> keys;
    for (int i = 0; i < count; ++i) {
        auto* output = static_cast<wl_output*>(SDL_GetPointerProperty(SDL_GetDisplayProperties(ids[i]), "SDL.display.wayland.wl_output", nullptr));
        if (!output) return;
        native.push_back(output); keys.push_back(ids[i]);
    }
    discovery.queue = wl_display_create_queue(discovery.display);
    if (!discovery.queue) return;
    auto* wrapper = static_cast<wl_display*>(wl_proxy_create_wrapper(discovery.display));
    if (!wrapper) return;
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapper), discovery.queue);
    discovery.registry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);
    if (!discovery.registry) return;
    wl_registry_add_listener(discovery.registry, &registryListener, &discovery);
    if (!Roundtrip(discovery.display, discovery.queue, deadline) || !discovery.manager || !discovery.xdg) return;
    for (std::size_t i = 0; i < native.size(); ++i) {
        auto name = std::make_unique<OutputName>(); name->key = keys[i];
        name->proxy = zxdg_output_manager_v1_get_xdg_output(discovery.xdg, native[i]);
        if (!name->proxy) return;
        zxdg_output_v1_add_listener(name->proxy, &nameListener, name.get());
        discovery.names.push_back(std::move(name));
    }
    if (!Roundtrip(discovery.display, discovery.queue, deadline) || !discovery.complete ||
        discovery.removed || discovery.managerFinished) { status("incomplete/changing or timed-out Wayland identity snapshot"); return; }
    std::vector<std::pair<std::uint64_t, std::string>> names;
    for (const auto& name : discovery.names) {
        if (!name->done || name->name.empty()) return;
        names.emplace_back(name->key, name->name);
    }
    std::vector<Output> heads;
    for (auto& head : discovery.heads) {
        if (head->finished) continue;
        head->output.identity = WaylandSerial(head->make, head->model, head->serial);
        heads.push_back(std::move(head->output));
    }
    JoinWaylandOutputs(heads, names);
    status("no valid unique make/model/serial or exact enabled head association");
    Associate(catalog, keys, heads);
}
void ShutdownWaylandDiscovery()
{
    // Called immediately before SDL closes its connection. Server resources
    // whose IDs never arrived are then reclaimed by that connection teardown.
    retiring.reset();
}
} // namespace Platform::LinuxIdentity
