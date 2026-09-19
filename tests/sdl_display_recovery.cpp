// Opt in only in the disposable Xorg/Weston fixture scripts, never a user's desktop.
#include "../Hunt/Platform/Platform.h"
#include "../Hunt/Game/DisplayRecovery.h"
#include "../Hunt/Debug/Log.h"
#include <SDL3/SDL.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

void LogWrite(LogLevel, const char*, int, const char* format, ...)
{
    va_list args;va_start(args,format);std::vprintf(format,args);va_end(args);std::putchar('\n');
}
#define REQUIRE(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s: %s\n",__LINE__,#x,SDL_GetError());return 1; } } while(false)
void Pump(GameDisplay::DisplayRecovery& recovery, unsigned milliseconds, bool* removed=nullptr)
{
    const auto end=SDL_GetTicks()+milliseconds;
    do {
        int code=0;Platform::Event event;
        auto result=Platform::PumpOneEvent(code,&event);
        recovery.Observe(event,Platform::Milliseconds());
        if(removed && event.occupiedDisplayRemoved) *removed=true;
        if(result==Platform::PumpResult::Idle) SDL_Delay(1);
    } while(SDL_GetTicks()<end);
}
int main()
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(!std::getenv("CARNIVORES_TEST_DISPLAY_RECOVERY")) return 77;
    REQUIRE(Platform::InitializeApplication());
    struct Cleanup { ~Cleanup() { Platform::DestroyGLContext();Platform::ShutdownApplication(); } } cleanup;
    REQUIRE(Platform::CreateGameWindow());REQUIRE(Platform::CreateGLContext());
    int count=0;auto** windows=SDL_GetWindows(&count);auto* window=windows && count==1?windows[0]:nullptr;SDL_free(windows);
    REQUIRE(window);
    REQUIRE(!(SDL_GetWindowFlags(window)&SDL_WINDOW_HIGH_PIXEL_DENSITY));
    const bool wayland=SDL_strcmp(SDL_GetCurrentVideoDriver(),"wayland")==0;
    REQUIRE(wayland || SDL_strcmp(SDL_GetCurrentVideoDriver(),"x11")==0);
    GameDisplay::Configuration request;request.size={800,600};request.mode=Platform::WindowMode::Windowed;
    request.monitor={GameDisplay::MonitorPreferenceKind::SessionIndex,1,{}};request.refresh={120,1};
    GameDisplay::DisplayRecovery recovery;
    auto catalog=Platform::QueryDisplayCatalog();REQUIRE(catalog.displays.size()==2);
    const auto Apply=[&]() {
        const auto fresh=Platform::QueryDisplayCatalog();
        const auto selected=GameDisplay::SelectMonitor(fresh,request.monitor,request.size,request.refresh);
        Platform::ConfigureGameWindow(request.mode,request.size,{400,300},selected.exclusiveMode,selected.target);
        recovery.Applied(request,selected);
    };
    Apply();Pump(recovery,250);
    REQUIRE(GameDisplay::UsableDrawable(Platform::QueryWindowState().pixels));
    REQUIRE(SDL_SetWindowSize(window,640,480));REQUIRE(SDL_SyncWindow(window));Pump(recovery,200);
    auto state=Platform::QueryWindowState();
    REQUIRE(state.logical.width==640 && state.logical.height==480);
    REQUIRE(state.pixels.width==640 && state.pixels.height==480);
    REQUIRE(request.size.width==800 && request.size.height==600);
    if(wayland) {
        int n=0;auto* displays=SDL_GetDisplays(&n);REQUIRE(displays && n==2);
        const float first=SDL_GetDesktopDisplayMode(displays[0])->pixel_density,second=SDL_GetDesktopDisplayMode(displays[1])->pixel_density;
        std::printf("Mixed-scale outputs: %.3f and %.3f\n",first,second);
        const bool requireMixed=std::getenv("CARNIVORES_TEST_MIXED_SCALE")!=nullptr;
        REQUIRE(!requireMixed || (first!=second));
        for(unsigned index=0;index<2;++index) {
            request.monitor.index=index;request.mode=Platform::WindowMode::Exclusive;Apply();Pump(recovery,250);
            state=Platform::QueryWindowState();
            REQUIRE(SDL_GetDisplayForWindow(window)==displays[index]);
            REQUIRE(state.logical.width==800 && state.logical.height==600);
            REQUIRE(state.pixels.width==800 && state.pixels.height==600);
            Platform::SwapGLBuffers();
            REQUIRE(request.size.width==800 && request.size.height==600);
            std::printf("output %u scale=%.3f logical=%dx%d drawable=%dx%d requested=%dx%d\n",index,
                SDL_GetDesktopDisplayMode(displays[index])->pixel_density,state.logical.width,state.logical.height,
                state.pixels.width,state.pixels.height,request.size.width,request.size.height);
        }
        SDL_free(displays);
    } else {
        // These exact commands are constrained to DUMMY outputs in a fresh Xorg server.
        REQUIRE(std::system("xrandr --output DUMMY1 --off")==0);
        bool removed=false;Pump(recovery,350,&removed);
        catalog=Platform::QueryDisplayCatalog();REQUIRE(catalog.displays.size()==1);REQUIRE(removed);
        REQUIRE(recovery.Ready(Platform::Milliseconds()));
        REQUIRE(recovery.Resolve(request,catalog,Platform::QueryWindowState()));
        REQUIRE(!GameDisplay::SelectMonitor(catalog,request.monitor,request.size,request.refresh).exclusiveMode);
        Apply();recovery.Recovered(Platform::QueryDisplayCatalog());Pump(recovery,250);
        REQUIRE(Platform::QueryWindowState().reachable);
        REQUIRE(std::system("xrandr --output DUMMY1 --mode 1280x1024 --pos 1920x0")==0);
        Pump(recovery,350);catalog=Platform::QueryDisplayCatalog();REQUIRE(catalog.displays.size()==2);
        REQUIRE(!recovery.Resolve(request,catalog,Platform::QueryWindowState()));
        REQUIRE(std::system("xrandr --output DUMMY1 --primary")==0);Pump(recovery,200);
        catalog=Platform::QueryDisplayCatalog();REQUIRE(catalog.primaryDisplay==1);
        REQUIRE(std::system("xrandr --output DUMMY0 --primary")==0);Pump(recovery,200);
        REQUIRE(Platform::QueryDisplayCatalog().primaryDisplay==0);
        // Current-state queries ignore SDL's transient disable/re-enable during mode switches.
        request.mode=Platform::WindowMode::Exclusive;
        for(unsigned i=0;i<4;++i) {
            Apply();Pump(recovery,250);REQUIRE(Platform::QueryDisplayCatalog().displays.size()==2);
            Platform::RestoreDesktopMode();Pump(recovery,250);REQUIRE(Platform::QueryDisplayCatalog().displays.size()==2);
        }
    }
    std::puts("Production event, size, recovery and retained-request checks passed");
    return 0;
}
