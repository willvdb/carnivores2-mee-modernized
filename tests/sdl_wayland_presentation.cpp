// Opt-in production-backend regression. Only run on the disposable compositor
// created by tests/wayland/run_presentation_test.sh, never a user's desktop.
#include "../Hunt/Platform/Platform.h"
#include "../Hunt/Debug/Log.h"
#include <SDL3/SDL.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

void LogWrite(LogLevel, const char*, int, const char* format, ...)
{
    va_list args; va_start(args, format); std::vprintf(format, args); va_end(args);
    std::putchar('\n');
}

bool Check(bool value, const char* label)
{
    if (!value) std::fprintf(stderr, "%s: %s\n", label, SDL_GetError());
    return value;
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (!std::getenv("CARNIVORES_TEST_WAYLAND_PRESENTATION")) {
        std::puts("Skipped: opt in only on a disposable Wayland compositor");
        return 77;
    }
    if (!Platform::InitializeApplication()) return 1;
    struct Cleanup { ~Cleanup() { Platform::DestroyGLContext(); Platform::ShutdownApplication(); } } cleanup;
    if (!Check(SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0, "requires Wayland") ||
        !Platform::CreateGameWindow() || !Platform::CreateGLContext()) return 1;
    Platform::ShowLoadingWindow({210, 106});
    Platform::SwapGLBuffers();
    const auto catalog = Platform::QueryDisplayCatalog();
    if (!Check(catalog.displays.size() == 2, "fixture needs two outputs")) return 1;
    int count=0;
    SDL_Window** windows=SDL_GetWindows(&count);
    SDL_Window* window=windows && count == 1 ? windows[0] : nullptr;
    SDL_free(windows);
    if (!Check(window != nullptr, "one game window")) return 1;
    const auto* driver=SDL_GetCurrentVideoDriver();
    std::printf("Testing production Platform on %s\n", driver);
    for (std::size_t index=0; index<catalog.displays.size(); ++index) {
        const auto& display=catalog.displays[index];
        if (!display.bounds || !display.desktopMode) return 1;
        const auto configure = [&](Platform::WindowMode mode, Platform::Size size) {
            // The game resolves fresh owned bounds on every mode application.
            // Wayland reports emulated fullscreen bounds while that mode is active.
            const auto snapshot=Platform::QueryDisplayCatalog();
            if (index >= snapshot.displays.size() || !snapshot.displays[index].bounds) return false;
            const Platform::DisplayTarget target{*snapshot.displays[index].bounds};
            Platform::ConfigureGameWindow(mode,size,{size.width/2,size.height/2},{},target);
            return true;
        };
        int idCount=0; SDL_DisplayID* ids=SDL_GetDisplays(&idCount);
        const auto expected=ids && index < static_cast<std::size_t>(idCount) ? ids[index] : 0;
        SDL_free(ids);
        for (int cycle=0; cycle<2; ++cycle) {
            if (!configure(Platform::WindowMode::Exclusive,{800,600})) return 1;
            Platform::SwapGLBuffers();
            auto actual=Platform::ClientSize();
            if (!Check(expected && SDL_GetDisplayForWindow(window) == expected, "fullscreen target") ||
                !Check(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN, "fullscreen settled") ||
                !Check(actual.width==800 && actual.height==600, "emulated render dimensions")) return 1;
            if (!configure(Platform::WindowMode::Windowed,{800,600})) return 1;
            if (!Check(!(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN), "windowed settled")) return 1;
        }
        if (!configure(Platform::WindowMode::Exclusive,{777,555})) return 1;
        Platform::SwapGLBuffers();
        const auto actual=Platform::ClientSize();
        if (!Check(SDL_GetDisplayForWindow(window)==expected,"fallback target") ||
            !Check(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN,"fallback remains fullscreen") ||
            !Check(actual.width==display.desktopMode->size.width && actual.height==display.desktopMode->size.height,
                   "unsupported exact size uses desktop fullscreen")) return 1;
        Platform::RestoreDesktopMode();
        if (!Check(!(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN), "desktop restored")) return 1;
        const auto* restored=SDL_GetCurrentDisplayMode(expected);
        if (!Check(restored && restored->w==display.desktopMode->size.width &&
                   restored->h==display.desktopMode->size.height,"reported desktop dimensions restored")) return 1;
    }
    std::puts("Wayland presentation/fallback/restoration passed on both virtual outputs");
    return 0;
}
