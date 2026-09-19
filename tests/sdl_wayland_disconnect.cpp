// Regression for pinned SDL's pending-window loops on a dead Wayland socket.
// Fault injection closes this disposable test client's connection, not the server.
#include <SDL3/SDL.h>
#include <sys/socket.h>
#include <cstdio>
#include <cstdlib>

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (!std::getenv("CARNIVORES_TEST_WAYLAND_PRESENTATION")) {
        std::puts("Skipped: run only on the disposable Wayland test compositor");
        return 77;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    struct Cleanup { ~Cleanup() { SDL_Quit(); } } cleanup;
    if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") != 0) return 1;
    SDL_Window* window=SDL_CreateWindow("Wayland disconnect regression",800,600,SDL_WINDOW_OPENGL);
    if (!window) return 1;
    SDL_GLContext context=SDL_GL_CreateContext(window);
    if (!context) return 1;
    SDL_GL_SwapWindow(window);
    SDL_SharedObject* library=SDL_LoadObject("libwayland-client.so.0");
    if (!library) return 1;
    const auto getFD=reinterpret_cast<int (*)(void*)>(SDL_LoadFunction(library,"wl_display_get_fd"));
    void* display=SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER,nullptr);
    if (!display || !getFD || !SDL_SetWindowFullscreen(window,true)) {
        SDL_UnloadObject(library);
        return 1;
    }
    const int result=shutdown(getFD(display),SHUT_RDWR);
    SDL_UnloadObject(library);
    if (result != 0) return 1;
    std::puts("Disconnected with fullscreen request pending");
    const bool synchronized=SDL_SyncWindow(window);
    std::printf("Sync returned %d: %s\n", synchronized, SDL_GetError());
    // This setter also flushes outstanding fullscreen callbacks. It must not
    // loop forever waiting for callbacks from the now disconnected server.
    SDL_SetWindowSize(window,640,480);
    std::puts("Pending state flush returned");
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    return synchronized ? 1 : 0;
}
