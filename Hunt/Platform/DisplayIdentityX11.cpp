#include "DisplayIdentityLinuxNative.h"
#include "DisplayIdentityLinux.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <memory>

namespace Platform::LinuxIdentity {
namespace {
// Xlib reports disappearing resources through its error handler. Discovery is
// main-thread only; trap errors on SDL's exact borrowed connection and forward
// errors on other connections. Never change the server or open another session.
class ErrorScope {
public:
    explicit ErrorScope(::Display* connection) : connection(connection) {
        XSync(connection, False);
        previous = XSetErrorHandler(Handle);
        current = this;
    }
    ~ErrorScope() { XSync(connection, False); current = nullptr; XSetErrorHandler(previous); }
    bool Failed() { XSync(connection, False); return failed; }
private:
    static int Handle(::Display* display, XErrorEvent* event) {
        if (current && display == current->connection) { current->failed = true; return 0; }
        return current && current->previous ? current->previous(display, event) : 0;
    }
    ::Display* connection;
    XErrorHandler previous = nullptr;
    bool failed = false;
    static ErrorScope* current;
};
ErrorScope* ErrorScope::current = nullptr;
std::optional<DisplayIdentity> ReadEdid(::Display* connection, RROutput output, Atom edid)
{
    if (!edid) return std::nullopt;
    Atom actualType = None;
    int format = 0;
    unsigned long items = 0, remaining = 0;
    unsigned char* bytes = nullptr;
    const int result = XRRGetOutputProperty(connection, output, edid, 0, 8192, False, False,
                                          XA_INTEGER, &actualType, &format, &items, &remaining, &bytes);
    const std::unique_ptr<unsigned char, decltype(&XFree)> owner(bytes, XFree);
    if (result != Success || actualType != XA_INTEGER || format != 8 || remaining ||
        !bytes || items < 128 || items > 32768) return std::nullopt;
    return X11Edid(std::vector<std::uint8_t>(bytes, bytes + items));
}
}
// SDL 3.2 keeps initial enumeration order when RandR primary changes. Query
// the borrowed exact connection/output mapping on each application instead.
SDL_DisplayID X11PrimaryDisplay()
{
    int count=0;
    const std::unique_ptr<SDL_DisplayID, decltype(&SDL_free)> ids(SDL_GetDisplays(&count),SDL_free);
    if (!ids) return 0;
    for (int i=0; i<count; ++i) {
        const auto props=SDL_GetDisplayProperties(ids.get()[i]);
        auto* connection=static_cast<::Display*>(SDL_GetPointerProperty(props,"Carnivores.display.x11.connection",nullptr));
        const auto root=SDL_GetNumberProperty(props,"Carnivores.display.x11.root",0);
        const auto output=SDL_GetNumberProperty(props,"Carnivores.display.x11.output",0);
        if (!connection || root<=0 || output<=0) continue;
        ErrorScope errors(connection);
        const auto primary=XRRGetOutputPrimary(connection,static_cast<Window>(root));
        if (!errors.Failed() && primary && primary==static_cast<RROutput>(output)) return ids.get()[i];
    }
    return 0; // No native primary/mapping: keep SDL's documented default.
}

void DiscoverX11(DisplayCatalog& catalog, const SDL_DisplayID* ids, int count)
{
    ::Display* connection = nullptr;
    std::vector<std::uint64_t> keys;
    std::vector<Window> roots;
    for (int i = 0; i < count; ++i) {
        auto& display = catalog.displays[static_cast<std::size_t>(i)];
        display.identityStatus = "X11 requires the bundled SDL exact RandR mapping extension";
        const auto properties = SDL_GetDisplayProperties(ids[i]);
        auto* native = static_cast<::Display*>(SDL_GetPointerProperty(properties, "Carnivores.display.x11.connection", nullptr));
        const auto output = SDL_GetNumberProperty(properties, "Carnivores.display.x11.output", 0);
        const auto root = SDL_GetNumberProperty(properties, "Carnivores.display.x11.root", 0);
        if (!native || output <= 0 || root <= 0) { keys.push_back(0); roots.push_back(0); continue; }
        if (connection && native != connection) return; // Never mix sessions.
        connection = native;
        keys.push_back(static_cast<std::uint64_t>(output));
        roots.push_back(static_cast<Window>(root));
        display.identityStatus = "no valid unique EDID serial, inactive output, or cloned/missing native mapping";
    }
    if (!connection) return;
    ErrorScope errors(connection);
    std::vector<Output> outputs;
    bool complete = true;
    const Atom edid = XInternAtom(connection, "EDID", True);
    for (int screen = 0; screen < ScreenCount(connection); ++screen) {
        const Window root = RootWindow(connection, screen);
        const std::unique_ptr<XRRScreenResources, decltype(&XRRFreeScreenResources)> resources(
            XRRGetScreenResourcesCurrent(connection, root), XRRFreeScreenResources);
        if (!resources) { complete = false; break; }
        for (int index = 0; index < resources->noutput; ++index) {
            const auto output = resources->outputs[index];
            const std::unique_ptr<XRROutputInfo, decltype(&XRRFreeOutputInfo)> info(
                XRRGetOutputInfo(connection, resources.get(), output), XRRFreeOutputInfo);
            if (!info) { complete = false; break; }
            if (info->connection != RR_Connected) continue;
            Output entry;
            entry.key = output;
            entry.identity = ReadEdid(connection, output, edid);
            if (info->crtc) {
                const std::unique_ptr<XRRCrtcInfo, decltype(&XRRFreeCrtcInfo)> crtc(
                    XRRGetCrtcInfo(connection, resources.get(), info->crtc), XRRFreeCrtcInfo);
                if (!crtc) { complete = false; break; }
                entry.active = crtc->mode && crtc->noutput == 1 && crtc->outputs[0] == output;
            }
            // A root mismatch invalidates this exact native association.
            for (std::size_t i = 0; i < keys.size(); ++i)
                if (keys[i] == output && roots[i] != root) keys[i] = 0;
            outputs.push_back(std::move(entry));
        }
    }
    if (errors.Failed() || !complete) {
        for (auto& display : catalog.displays) display.identityStatus = "incomplete/changing X11 native discovery";
        return;
    }
    Associate(catalog, keys, outputs);
}
} // namespace Platform::LinuxIdentity
