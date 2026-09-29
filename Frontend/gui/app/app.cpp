#include "app.hpp"
#include "c2/frontend/gui/demo_store.hpp"
#include "c2/frontend/gui/source.hpp"
#include "capture.hpp"
#include "resources.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace c2::frontend::gui::app {
App::App(Options options) : options_(std::move(options)), dialog_sink_(std::make_shared<DialogSink>()) {
    dialog_sink_->app = this;
}

App::~App() {
    {
        std::lock_guard<std::mutex> lock(dialog_sink_->mutex);
        dialog_sink_->app = nullptr;   // a late dialog callback finds nobody
    }
    worker_.set_wake({});
    screens_.reset();                  // closes documents (deferred to context update)
    if (context_) context_->Update();  // runs the deferred document destruction
    if (rml_initialised_) Rml::Shutdown();
    render_.reset();
    if (gl_loaded_) RmlGL3::Shutdown();
    ime_.reset();
    system_.reset();
    if (gl_) SDL_GL_DestroyContext(gl_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
    if (!demo_store_.empty()) remove_demo_store(demo_store_);
}

bool App::init(std::string& error) {
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    // The self-test runs with a hidden window and no keyboard focus; SDL drops
    // joystick events for unfocused apps unless told otherwise. Production
    // keeps SDL's default (no controller input while another app is focused).
    if (options_.self_test) SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        error = std::string("SDL_Init failed: ") + SDL_GetError();
        return false;
    }
    std::vector<std::string> tried;
    auto root = resolve_asset_root(options_.assets, tried);
    if (!root) {
        error = "no asset root contains rml/lodge.rml; tried:";
        for (const auto& t : tried) error += "\n  " + t;
        return false;
    }
    asset_root_ = root->directory;
    if (auto missing = missing_essentials(asset_root_); !missing.empty()) {
        error = "essential GUI resources are missing under " + asset_root_.u8string() + ":";
        for (const auto& m : missing) error += "\n  " + m.u8string();
        return false;
    }

    // OpenGL 3.3 core, as RmlUi's GL3 renderer requires (its own glad loader).
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    const SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                  (options_.self_test ? SDL_WINDOW_HIDDEN : 0);
    window_ = SDL_CreateWindow("Carnivores frontend (RmlUi evaluation)", options_.width, options_.height, flags);
    if (!window_) {
        error = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
        return false;
    }
    SDL_SetWindowMinimumSize(window_, kMinWidth, kMinHeight);
    gl_ = SDL_GL_CreateContext(window_);
    if (!gl_) {
        error = std::string("SDL_GL_CreateContext (3.3 core) failed: ") + SDL_GetError();
        return false;
    }
    SDL_GL_MakeCurrent(window_, gl_);
    SDL_GL_SetSwapInterval(1);

    Rml::String gl_message;
    if (!RmlGL3::Initialize(&gl_message)) {
        error = "OpenGL function loading failed: " + gl_message;
        return false;
    }
    gl_loaded_ = true;
    gl_version_ = gl_message;
    render_ = std::make_unique<PngRenderInterface>();
    if (!*render_) {
        error = "RmlUi GL3 renderer could not initialise (OpenGL 3.3 core profile required)";
        return false;
    }
    system_ = std::make_unique<SystemInterface_SDL>(window_);
    ime_ = std::make_unique<TextInputMethodEditor_SDL>();
    Rml::SetSystemInterface(system_.get());
    Rml::SetRenderInterface(render_.get());
    Rml::SetTextInputHandler(ime_.get());
    if (!Rml::Initialise()) {
        error = "Rml::Initialise failed";
        return false;
    }
    rml_initialised_ = true;
    if (!Rml::LoadFontFace((asset_root_ / "fonts" / "DejaVuSans.ttf").u8string())) {
        error = "cannot load fonts/DejaVuSans.ttf";
        return false;
    }
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(window_, &pw, &ph);
    context_ = Rml::CreateContext("main", Rml::Vector2i(pw, ph));
    if (!context_) {
        error = "Rml::CreateContext failed";
        return false;
    }
    context_->SetDensityIndependentPixelRatio(SDL_GetWindowDisplayScale(window_));
    render_->SetViewport(pw, ph);

    // Data source: explicit read-only store, else an owned disposable demo store.
    DataSource source;
    if (options_.store) {
        source.kind = DataSource::Kind::supplied;
        source.directory = options_.store->u8string();
    } else {
        try {
            demo_store_ = create_demo_store();
        } catch (const std::exception& e) {
            error = std::string("cannot create the disposable demo store: ") + e.what();
            return false;
        }
        source.kind = DataSource::Kind::demo;
        source.directory = demo_store_.u8string();
    }
    model_ = std::make_unique<PresentationModel>(source);
    ScreenCallbacks callbacks;
    callbacks.open_folder_dialog = [this] { open_folder_dialog(); };
    callbacks.check_folder = [this](RequestId id, std::string path) {
        worker_.submit_result<bool>(
            [path] {
                std::error_code ec;
                return std::filesystem::is_directory(std::filesystem::u8path(path), ec) && !ec;
            },
            [this, id](bool is_directory) {
                if (model_->complete_folder_check(id, is_directory, "")) screens_->sync();
            });
    };
    screens_ = std::make_unique<Screens>(*context_, *model_, asset_root_, std::move(callbacks));
    if (!screens_->load(error)) return false;

    wake_event_ = SDL_RegisterEvents(1);
    worker_.set_wake([type = wake_event_] {
        SDL_Event e{};
        e.type = type;
        SDL_PushEvent(&e);
    });
    running_ = true;
    reload_store();
    return true;
}

void App::reload_store() {
    const RequestId id = model_->begin_load();
    const auto directory = std::filesystem::u8path(model_->source().directory);
    worker_.submit_result<SnapshotResult>([directory] { return read_store_snapshot(directory); },
        [this, id](SnapshotResult result) {
            if (model_->complete_load(id, std::move(result))) screens_->sync();
        });
    screens_->sync();
}

// --- native folder dialog --------------------------------------------------------
void App::open_folder_dialog() {
    if (screens_->folder_dialog_pending()) return;
    const RequestId id = screens_->next_dialog_request();
    screens_->folder_dialog_started(id);
    auto* call = new DialogCall{dialog_sink_, id};
    const std::string start = model_->draft().values.content_folder;
    SDL_ShowOpenFolderDialog(&App::dialog_callback, call, window_, start.empty() ? nullptr : start.c_str(), false);
    screens_->sync();
}

void SDLCALL App::dialog_callback(void* userdata, const char* const* filelist, int) {
    std::unique_ptr<DialogCall> call(static_cast<DialogCall*>(userdata));
    std::optional<std::string> folder;
    std::string error;
    if (!filelist) error = SDL_GetError();
    else if (filelist[0]) folder = filelist[0];   // else: cancelled
    std::lock_guard<std::mutex> lock(call->sink->mutex);
    App* app = call->sink->app;
    if (!app) return;   // the application is gone; nothing to deliver
    const RequestId id = call->id;
    app->worker_.post([app, id, folder, error] { app->screens_->folder_dialog_finished(id, folder, error); });
}

// --- loop ---------------------------------------------------------------------------
int App::run() {
    while (running_) step(true);
    return 0;
}

std::uint32_t App::wait_timeout_ms(std::uint64_t now_ms) const {
    std::uint32_t timeout = visible_ ? 100 : 250;
    const double next = context_->GetNextUpdateDelay();
    if (next < timeout / 1000.0) timeout = static_cast<std::uint32_t>(std::max(1.0, next * 1000.0));
    timeout = std::min(timeout, gamepad_.next_wake_ms(now_ms));
    if (worker_.pending_completions()) timeout = 0;
    return timeout;
}

void App::step(bool wait) {
    const std::uint64_t now = SDL_GetTicks();
    SDL_Event event;
    bool has = wait ? SDL_WaitEventTimeout(&event, static_cast<int>(wait_timeout_ms(now))) : SDL_PollEvent(&event);
    while (has) {
        handle_event(event);
        has = SDL_PollEvent(&event);
    }
    worker_.drain();
    std::vector<Action> actions;
    gamepad_.tick(SDL_GetTicks(), actions);
    for (auto a : actions) dispatch(a);
    screens_->sync();
    if (model_->exit_requested()) running_ = false;
    context_->Update();
    if (visible_ || !wait) render();
}

void App::render() {
    render_->Clear();
    render_->BeginFrame();
    context_->Render();
    render_->EndFrame();
    SDL_GL_SwapWindow(window_);
}

bool App::capture(const std::string& name) {
    if (!options_.capture_dir) return true;
    std::error_code ec;
    std::filesystem::create_directories(*options_.capture_dir, ec);
    screens_->sync();
    context_->Update();
    render_->Clear();
    render_->BeginFrame();
    context_->Render();
    render_->EndFrame();
    const auto dims = context_->GetDimensions();
    std::string error;
    const bool ok = capture_framebuffer_png(dims.x, dims.y, *options_.capture_dir / (name + ".png"), error);
    if (!ok) std::cerr << "capture " << name << " failed: " << error << '\n';
    SDL_GL_SwapWindow(window_);
    return ok;
}

void App::apply_pixel_size(int width, int height) {
    render_->SetViewport(width, height);
    context_->SetDimensions(Rml::Vector2i(width, height));
    screens_->relayout_lodge();
}

void App::handle_event(const SDL_Event& event) {
    if (event.type == wake_event_) return;
    std::vector<Action> actions;
    if (gamepad_.handle_event(event, SDL_GetTicks(), actions)) {
        for (auto a : actions) dispatch(a);
        return;
    }
    switch (event.type) {
    case SDL_EVENT_QUIT:
        running_ = false;
        return;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        running_ = false;
        return;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        apply_pixel_size(event.window.data1, event.window.data2);
        return;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        context_->SetDensityIndependentPixelRatio(SDL_GetWindowDisplayScale(window_));
        screens_->relayout_lodge();
        return;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        focused_ = false;
        gamepad_.clear_held();   // no stuck repeat after Alt-Tab
        return;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        focused_ = true;
        return;
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_WINDOW_OCCLUDED:
        visible_ = false;
        return;
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_EXPOSED:
        visible_ = true;
        break;   // RmlUi may also want the event
    case SDL_EVENT_KEY_DOWN: {
        // Arrows and Escape take the same semantic path as the controller so
        // mouse, keyboard and gamepad drive one set of focus rules.
        const auto key = RmlSDL::ConvertKey(event.key.key);
        switch (key) {
        case Rml::Input::KI_ESCAPE: dispatch(Action::back); return;
        case Rml::Input::KI_UP: dispatch(Action::nav_up); return;
        case Rml::Input::KI_DOWN: dispatch(Action::nav_down); return;
        case Rml::Input::KI_LEFT: dispatch(Action::nav_left); return;
        case Rml::Input::KI_RIGHT: dispatch(Action::nav_right); return;
        default: break;
        }
        break;
    }
    case SDL_EVENT_KEY_UP: {
        const auto key = RmlSDL::ConvertKey(event.key.key);
        if (key == Rml::Input::KI_ESCAPE || key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN ||
            key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT)
            return;   // dispatch() already sent the matching key-up
        break;
    }
    case SDL_EVENT_TEXT_EDITING:
        ime_->HandleEdit(event.edit);
        return;
    default: break;
    }
    SDL_Event copy = event;
    RmlSDL::InputEventHandler(context_, window_, copy);
}

void App::dispatch(Action action) {
    using Rml::Input::KeyIdentifier;
    auto key = [this](KeyIdentifier k, int modifiers = 0) {
        context_->ProcessKeyDown(k, modifiers);
        context_->ProcessKeyUp(k, modifiers);
    };
    switch (action) {
    case Action::nav_up: key(Rml::Input::KI_UP); break;
    case Action::nav_down: key(Rml::Input::KI_DOWN); break;
    case Action::nav_left: {
        auto* before = context_->GetFocusElement();
        key(Rml::Input::KI_LEFT);
        if (context_->GetFocusElement() == before) screens_->cross_pane_left();
        break;
    }
    case Action::nav_right:
        if (!screens_->cross_pane_right()) key(Rml::Input::KI_RIGHT);
        break;
    case Action::confirm: key(Rml::Input::KI_RETURN); break;
    case Action::focus_next: key(Rml::Input::KI_TAB); break;
    case Action::focus_previous: key(Rml::Input::KI_TAB, Rml::Input::KM_SHIFT); break;
    case Action::back: screens_->handle_back(); break;
    }
}
} // namespace c2::frontend::gui::app
