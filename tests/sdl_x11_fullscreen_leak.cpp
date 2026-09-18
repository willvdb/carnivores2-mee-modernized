// SDL-only regression: no engine or game assets. Run on a disposable X11 server
// with an 800x600 mode and a different desktop mode. See docs/SDL_X11_MODE_LEAK.md.
#include <SDL3/SDL.h>
#include <atomic>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
std::atomic<int> allocations{0};

void* SDLCALL CountMalloc(size_t size)
{
    void* p = std::malloc(size);
    if (p) ++allocations;
    return p;
}
void* SDLCALL CountCalloc(size_t count, size_t size)
{
    void* p = std::calloc(count, size);
    if (p) ++allocations;
    return p;
}
void* SDLCALL CountRealloc(void* old, size_t size)
{
    if (!old) return CountMalloc(size);
    if (!size) {
        std::free(old);
        --allocations;
        return nullptr;
    }
    return std::realloc(old, size);
}
void SDLCALL CountFree(void* p)
{
    if (p) --allocations;
    std::free(p);
}

int Decimal(const char* text)
{
    if (!text || !*text) return -1;
    int value = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9' || value > (INT_MAX - (*text - '0')) / 10)
            return -1;
        value = value * 10 + (*text - '0');
    }
    return value;
}

void PumpEvents()
{
    for (int i = 0; i < 20; ++i) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {}
        SDL_Delay(10);
    }
}

bool Run(int index, int cycles)
{
    const char* driver = SDL_GetCurrentVideoDriver();
    if (!driver || std::strcmp(driver, "x11") != 0) {
        std::fprintf(stderr, "This regression requires SDL's x11 driver\n");
        return false;
    }
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    const SDL_DisplayID id = displays && index < count ? displays[index] : 0;
    SDL_free(displays);
    if (!id) {
        std::fprintf(stderr, "Requested display index %d unavailable (%d displays)\n", index, count);
        return false;
    }

    SDL_Rect bounds{};
    const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(id);
    if (!desktop || (desktop->w == 800 && desktop->h == 600) ||
        !SDL_GetDisplayBounds(id, &bounds)) {
        std::fprintf(stderr, "Need a desktop mode different from 800x600\n");
        return false;
    }
    const int desktopWidth = desktop->w, desktopHeight = desktop->h;
    SDL_Window* window = SDL_CreateWindow("SDL X11 allocation regression", 800, 600, SDL_WINDOW_OPENGL);
    if (!window) return false;
    struct WindowCleanup {
        SDL_Window* window;
        ~WindowCleanup() { SDL_DestroyWindow(window); }
    } cleanup{window};
    if (!SDL_SetWindowPosition(window, bounds.x + (bounds.w - 800) / 2,
                               bounds.y + (bounds.h - 600) / 2) ||
        !SDL_SyncWindow(window)) return false;
    PumpEvents();

    for (int i = 0; i < cycles; ++i) {
        SDL_DisplayMode mode{};
        if (!SDL_GetClosestFullscreenDisplayMode(id, 800, 600, 0, false, &mode) ||
            mode.w != 800 || mode.h != 600 || mode.displayID != id) {
            std::fprintf(stderr, "Target needs an exact 800x600 exclusive mode\n");
            return false;
        }
        if (!SDL_SetWindowFullscreenMode(window, &mode) ||
            !SDL_SetWindowFullscreen(window, true) || !SDL_SyncWindow(window)) return false;
        PumpEvents();
        const SDL_DisplayMode* current = SDL_GetCurrentDisplayMode(id);
        if (!current || current->w != 800 || current->h != 600 ||
            SDL_GetDisplayForWindow(window) != id ||
            !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)) {
            std::fprintf(stderr, "Exclusive mode did not reach the requested display/size\n");
            return false;
        }

        if (!SDL_SetWindowFullscreen(window, false) || !SDL_SyncWindow(window)) return false;
        PumpEvents();
        current = SDL_GetCurrentDisplayMode(id);
        desktop = SDL_GetDesktopDisplayMode(id);
        if (!current || !desktop || current->w != desktopWidth || current->h != desktopHeight ||
            desktop->w != desktopWidth || desktop->h != desktopHeight) {
            std::fprintf(stderr, "Desktop mode was not preserved/restored\n");
            return false;
        }
    }
    std::printf("Completed %d exclusive/windowed cycles on display index %d\n", cycles, index);
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* requested = argc > 1 ? argv[1] : std::getenv("CARNIVORES_SDL_X11_LEAK_DISPLAY");
    if (!requested) {
        std::puts("Skipped: set CARNIVORES_SDL_X11_LEAK_DISPLAY on a disposable X11 server");
        return 77;
    }
    const int index = Decimal(requested);
    const int cycles = argc > 2 ? Decimal(argv[2]) : 20;
    if (argc > 3 || index < 0 || cycles < 1 || cycles > 100) return 1;
    if (!SDL_SetMemoryFunctions(CountMalloc, CountCalloc, CountRealloc, CountFree)) return 1;
    const bool passed = SDL_Init(SDL_INIT_VIDEO) && Run(index, cycles);
    if (!passed) std::fprintf(stderr, "Fullscreen regression failed: %s\n", SDL_GetError());
    SDL_Quit();
    std::printf("SDL allocations remaining after shutdown: %d\n", allocations.load());
    // Hooks cover SDL allocations even without sanitizers. ASan/LSan also cover
    // dependency allocations and must be run without leak suppressions.
    return passed && allocations == 0 ? 0 : 1;
}
