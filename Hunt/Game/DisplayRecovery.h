#pragma once
#include "DisplayConfiguration.h"
#include "DisplaySelection.h"
#include <cstdint>

namespace GameDisplay {
// Existing software/readback paths use int dimensions and bounded CPU storage.
// 16M pixels = 32 MiB CPU overlay, 64 MiB RGBA readback. Never allocate from a
// zero/minimized/transient or unbounded compositor size.
inline bool UsableDrawable(Platform::Size size)
{
    return size.width >= 2 && size.height >= 2 && size.width <= 8192 && size.height <= 8192 &&
        std::uint64_t(size.width) * std::uint64_t(size.height) <= 16777216;
}

inline bool UsableWindow(const Platform::WindowState& window)
{
    return !window.minimized && window.logical.width > 0 && window.logical.height > 0 && UsableDrawable(window.pixels);
}

inline bool SameRecoveryTopology(const Platform::DisplayCatalog& a, const Platform::DisplayCatalog& b)
{
    if (a.primaryDisplay != b.primaryDisplay || a.displays.size() != b.displays.size()) return false;
    for (std::size_t i=0; i<a.displays.size(); ++i) {
        const auto& x=a.displays[i]; const auto& y=b.displays[i];
        if (bool(x.bounds)!=bool(y.bounds) || (x.bounds && !Platform::EqualDisplayBounds(*x.bounds,*y.bounds)) ||
            bool(x.identity)!=bool(y.identity) || (x.identity && !Platform::EqualDisplayIdentity(*x.identity,*y.identity)) ||
            bool(x.desktopMode)!=bool(y.desktopMode)) return false;
        if (x.desktopMode && (x.desktopMode->size.width!=y.desktopMode->size.width ||
            x.desktopMode->size.height!=y.desktopMode->size.height ||
            Platform::HasRefresh(x.desktopMode->refresh)!=Platform::HasRefresh(y.desktopMode->refresh) ||
            (Platform::HasRefresh(x.desktopMode->refresh) && !Platform::EqualRefresh(x.desktopMode->refresh,y.desktopMode->refresh)))) return false;
    }
    return true; // Ignore temporary current fullscreen modes and diagnostic text.
}

class DisplayRecovery {
    bool pending = false, removed = false, identityApplied = false;
    std::uint32_t first = 0, last = 0;
    std::optional<Platform::DisplayCatalog> attempted;
public:
    void Observe(const Platform::Event& event, std::uint32_t now)
    {
        if (event.type != Platform::EventType::DisplayChanged) return;
        if (!pending) first = now;
        pending = true;
        last = now;
        removed = removed || event.occupiedDisplayRemoved;
    }
    // Quiet period plus a deadline: duplicate bursts cannot starve recovery.
    bool Ready(std::uint32_t now) const
    {
        return pending && (std::uint32_t(now - last) >= 150 || std::uint32_t(now - first) >= 500);
    }
    void Applied(const Configuration& config, const DisplaySelection& selected)
    {
        identityApplied = config.monitor.kind == MonitorPreferenceKind::Identity &&
                          selected.fallback == DisplayFallback::None;
        pending = removed = false;
        attempted.reset(); // Explicit user application permits a new attempt.
    }
    void Recovered(Platform::DisplayCatalog catalog)
    {
        for (auto& display:catalog.displays) {
            display.modes.clear(); display.currentMode.reset(); display.identityStatus.clear();
        }
        attempted = std::move(catalog);
    }
    bool Resolve(const Configuration& config, const Platform::DisplayCatalog& catalog,
                 const Platform::WindowState& window)
    {
        const auto selected = SelectMonitor(catalog, config.monitor, config.size, config.refresh);
        const bool invalidIdentity = identityApplied && selected.fallback != DisplayFallback::None;
        const bool recover = removed || !window.reachable || invalidIdentity;
        pending = removed = false;
        if (invalidIdentity) identityApplied = false;
        // Reconnect/reorder alone never steals a still-reachable window. Explicit
        // application resolves saved intent again; session indices refer only to
        // that fresh catalog and are not secretly bound to names/bounds/SDL IDs.
        if (!recover || (attempted && SameRecoveryTopology(*attempted,catalog))) return false;
        Recovered(catalog);
        return true;
    }
};
} // namespace GameDisplay
