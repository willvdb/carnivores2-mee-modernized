// Writes synthetic EDID only on the disposable server started by the test runner.
#include "../Hunt/Platform/Platform.h"
#include "../Hunt/Platform/DisplayIdentityLinux.h"
#include "../Hunt/Game/DisplaySelection.h"
#include "../Hunt/Debug/Log.h"
#include <SDL3/SDL.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <cstdio>
#include <cstdlib>
#include <memory>
void LogWrite(LogLevel, const char*, int, const char*, ...) {}
namespace {
bool Check(bool value,const char* label) { if(!value) std::fprintf(stderr,"FAILED: %s\n",label); return value; }
std::vector<std::uint8_t> Edid(std::uint8_t serial) {
    std::vector<std::uint8_t> bytes(128);
    std::fill(bytes.begin()+1,bytes.begin()+7,255);
    bytes[8]=0x10; bytes[9]=0xac; bytes[10]=0x34; bytes[11]=0x12;
    bytes[12]=serial; bytes[13]=0x56; bytes[18]=1; bytes[19]=4;
    unsigned sum=0;for(auto b:bytes)sum+=b;bytes[127]=static_cast<std::uint8_t>(256-sum%256);
    return bytes;
}
}
int main() {
    if(!std::getenv("CARNIVORES_TEST_X11_IDENTITY")) return 77;
    const std::unique_ptr<Display,decltype(&XCloseDisplay)> connection(XOpenDisplay(nullptr),XCloseDisplay);
    if(!connection) return 1;
    const std::unique_ptr<XRRScreenResources,decltype(&XRRFreeScreenResources)> resources(
        XRRGetScreenResourcesCurrent(connection.get(),DefaultRootWindow(connection.get())),XRRFreeScreenResources);
    if(!resources) return 1;
    std::vector<RROutput> outputs;
    for(int i=0;i<resources->noutput;++i) {
        auto* info=XRRGetOutputInfo(connection.get(),resources.get(),resources->outputs[i]);
        if(info && info->connection==RR_Connected && info->crtc)outputs.push_back(resources->outputs[i]);
        if(info)XRRFreeOutputInfo(info);
    }
    if(!Check(outputs.size()==2,"fixture requires exactly two active virtual outputs"))return 1;
    const Atom atom=XInternAtom(connection.get(),"EDID",False);
    auto set=[&](int output,const std::vector<std::uint8_t>& bytes,int format=8) {
        XRRChangeOutputProperty(connection.get(),outputs[output],atom,XA_INTEGER,format,PropModeReplace,bytes.data(),static_cast<int>(bytes.size()));
        XSync(connection.get(),False);
    };
    auto a=Edid(42),b=Edid(84);set(0,a);set(1,b);
    if(!Platform::InitializeApplication())return 1;
    struct Cleanup { ~Cleanup(){Platform::ShutdownApplication();} } cleanup;
    auto catalog=Platform::QueryDisplayCatalog();
    if(!Check(catalog.displays.size()==2,"production catalog"))return 1;
    for(const auto& display:catalog.displays) if(!Check(display.identity.has_value(),display.identityStatus.c_str()))return 1;
    const auto wanted=*Platform::LinuxIdentity::X11Edid(b);
    GameDisplay::MonitorPreference saved{GameDisplay::MonitorPreferenceKind::Identity,0,wanted};
    const auto selected=GameDisplay::SelectMonitor(catalog,saved,{800,600},{999,1});
    if(!Check(selected.target && !selected.exclusiveMode,"serial resolves to current output with unavailable refresh automatic"))return 1;
    // Native mapping is exact: compare the serial's current X output CRTC to the selected owned bounds.
    auto* info=XRRGetOutputInfo(connection.get(),resources.get(),outputs[1]);
    auto* crtc=info ? XRRGetCrtcInfo(connection.get(),resources.get(),info->crtc) : nullptr;
    bool exact=crtc && selected.target->bounds.origin.x==crtc->x && selected.target->bounds.origin.y==crtc->y;
    if(crtc)XRRFreeCrtcInfo(crtc);
    if(info)XRRFreeOutputInfo(info);
    if(!Check(exact,"serial is attached to the exact native output"))return 1;
    // Read properties afresh without SDL reinitialization: replacement, duplicate, malformed, recovery.
    set(0,b); catalog=Platform::QueryDisplayCatalog();
    for(const auto& display:catalog.displays) if(!Check(!display.identity,"duplicate EDID rejected"))return 1;
    set(0,a); b[127]++; set(1,b);catalog=Platform::QueryDisplayCatalog();
    const auto missing=GameDisplay::SelectMonitor(catalog,saved,{800,600},{60,1});
    if(!Check(!missing.target && !missing.exclusiveMode,"invalid current EDID falls to primary automatic"))return 1;
    b=Edid(84);set(1,b);catalog=Platform::QueryDisplayCatalog();
    if(!Check(GameDisplay::SelectMonitor(catalog,saved,{800,600}).target.has_value(),"restored serial recovers without rewriting intent"))return 1;
    // A malformed property is rejected by native discovery after SDL initial enumeration.
    set(1,std::vector<std::uint8_t>(4));catalog=Platform::QueryDisplayCatalog();
    if(!Check(!GameDisplay::SelectMonitor(catalog,saved,{800,600}).target,"truncated EDID rejected"))return 1;
    set(1,b);
    const auto copy=Platform::QueryDisplayCatalog();
    Platform::ShutdownApplication();
    if(!Check(copy.displays[0].identity && copy.displays[1].identity,"owned identities survive SDL shutdown"))return 1;
    std::puts("X11 identity production discovery, exact association, duplicate/invalid rejection and recovery passed");
    return 0;
}
