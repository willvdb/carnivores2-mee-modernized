#pragma once
// Application glue: SDL3 window + OpenGL 3.3 core context, RmlUi lifetime,
// the event loop with frame pacing, semantic input dispatch, the worker and
// the native folder dialog. Everything RmlUi happens on this (UI) thread.
#include "c2/frontend/gui/presentation.hpp"
#include "c2/frontend/gui/worker.hpp"
#include "gamepad.hpp"
#include "render_png.hpp"
#include "screens.hpp"
#include <RmlUi_Platform_SDL.h>
#include <SDL3/SDL.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace c2::frontend::gui::app {
struct Options {
    std::optional<std::filesystem::path> store;    // read-only supplied store
    std::optional<std::filesystem::path> assets;   // asset root override
    bool self_test = false;
    std::optional<std::filesystem::path> capture_dir;   // self-test writes PNG evidence here
    int width = 1280, height = 720;
};

class App {
public:
    explicit App(Options options);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Creates the window, context, RmlUi, screens and starts the first load.
    bool init(std::string& error);
    // Runs until exit is requested or the window is closed. Returns exit code.
    int run();
    // One loop iteration. wait=true blocks for events with pacing; false polls.
    void step(bool wait);
    void dispatch(Action action);
    bool running() const noexcept { return running_; }
    void request_quit() { running_ = false; }
    // Reloads the store off-thread; older completions are rejected by the model.
    void reload_store();
    void open_folder_dialog();
    // Applies a new drawable size (pixels) as the window event handler would.
    void apply_pixel_size(int width, int height);
    // Renders one frame and writes it as PNG into the capture directory (no-op
    // without --capture). Returns false when a capture was requested but failed.
    bool capture(const std::string& name);

    Rml::Context* context() const noexcept { return context_; }
    PresentationModel& model() noexcept { return *model_; }
    Screens& screens() noexcept { return *screens_; }
    Worker& worker() noexcept { return worker_; }
    SDL_Window* window() const noexcept { return window_; }
    const std::filesystem::path& asset_root() const noexcept { return asset_root_; }
    const std::string& gl_version() const noexcept { return gl_version_; }
    bool gamepad_connected() const noexcept { return gamepad_.connected(); }
    std::vector<std::string> gamepad_names() const { return gamepad_.names(); }
    static constexpr int kMinWidth = 800, kMinHeight = 520;

private:
    struct DialogSink {   // shared with SDL dialog callbacks; survives the App
        std::mutex mutex;
        App* app = nullptr;
    };
    struct DialogCall {
        std::shared_ptr<DialogSink> sink;
        RequestId id;
    };
    static void SDLCALL dialog_callback(void* userdata, const char* const* filelist, int filter);
    void handle_event(const SDL_Event& event);
    void render();
    std::uint32_t wait_timeout_ms(std::uint64_t now_ms) const;

    Options options_;
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_ = nullptr;
    std::unique_ptr<SystemInterface_SDL> system_;
    std::unique_ptr<PngRenderInterface> render_;
    std::unique_ptr<TextInputMethodEditor_SDL> ime_;
    Rml::Context* context_ = nullptr;
    std::unique_ptr<PresentationModel> model_;
    std::unique_ptr<Screens> screens_;
    Worker worker_;
    GamepadInput gamepad_;
    std::shared_ptr<DialogSink> dialog_sink_;
    std::filesystem::path asset_root_;
    std::filesystem::path demo_store_;   // owned disposable store, removed on exit
    std::uint32_t wake_event_ = 0;
    std::string gl_version_;
    bool gl_loaded_ = false;
    bool rml_initialised_ = false;
    bool running_ = false;
    bool visible_ = true;
    bool focused_ = true;
};
} // namespace c2::frontend::gui::app
