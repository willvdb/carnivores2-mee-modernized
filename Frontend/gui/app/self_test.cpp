#include "self_test.hpp"
#include "app.hpp"
#include "c2/frontend/gui/demo_store.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <cmath>
#include <iostream>

namespace c2::frontend::gui::app {
namespace {
int failures = 0;
void check(bool ok, const char* what, int line) {
    if (ok) return;
    ++failures;
    std::cerr << "self-test check failed (line " << line << "): " << what << '\n';
}
#define ST(expr) check(static_cast<bool>(expr), #expr, __LINE__)

void settle(App& app, int frames = 3) {
    for (int i = 0; i < frames; ++i) app.step(false);
}
bool wait_for(App& app, const std::function<bool()>& predicate, int max_frames = 2000) {
    for (int i = 0; i < max_frames; ++i) {
        app.step(false);
        if (predicate()) return true;
        SDL_Delay(1);
    }
    return predicate();
}
bool select_open(Rml::Element* select) {
    if (!select) return false;
    for (int i = 0; i < select->GetNumChildren(true); ++i)
        if (auto* c = select->GetChild(i); c && c->GetTagName() == "selectbox" && c->IsPseudoClassSet("checked")) return true;
    return false;
}
} // namespace

int run_self_test(App& app) {
    auto& model = app.model();
    auto& screens = app.screens();
    auto* ctx = app.context();
    std::cout << "self-test: video driver " << SDL_GetCurrentVideoDriver() << ", " << app.gl_version() << ", assets " << app.asset_root().u8string() << '\n';

    // 1. The demo store loads through the real core, off the UI thread.
    ST(wait_for(app, [&] { return model.load_state() != LoadState::loading; }));
    ST(model.load_state() == LoadState::ready);
    if (model.load_state() != LoadState::ready) std::cerr << "load error: " << model.load_error() << '\n';
    const auto& ids = demo_identities();
    ST(model.snapshot() && model.snapshot()->hunters.size() == ids.hunter_ids.size());
    ST(model.snapshot() && model.snapshot()->expeditions.size() == ids.expedition_ids.size());
    settle(app);
    ST(app.capture("01-lodge"));

    // 2. Lodge: initial focus and D-pad/arrow navigation between hotspots.
    ST(model.screen() == Screen::lodge);
    ST(screens.focused_id() == "dest-expeditions");
    app.dispatch(Action::nav_right); settle(app);
    ST(screens.focused_id() == "dest-profile");
    app.dispatch(Action::nav_right); settle(app);
    ST(screens.focused_id() == "dest-exit");
    app.dispatch(Action::nav_left); settle(app);
    ST(screens.focused_id() == "dest-profile");
    app.dispatch(Action::focus_next); settle(app);
    ST(screens.focused_id() == "dest-exit");   // Tab skips unavailable hotspots
    app.dispatch(Action::focus_previous); settle(app);
    ST(screens.focused_id() == "dest-profile");
    ST(!screens.handle_back());                 // nothing to dismiss on the lodge

    // 3. Setup: confirm enters, the text field gets focus, caret keys stay put.
    app.dispatch(Action::confirm); settle(app);
    ST(model.screen() == Screen::setup);
    ST(screens.focused_id() == "name-input");
    ST(app.capture("02-setup"));
    // Select all (Ctrl+A) and type: the widget's change event drives the model.
    auto type_replace = [&](const char* text) {
        ctx->ProcessKeyDown(Rml::Input::KI_A, Rml::Input::KM_CTRL);
        ctx->ProcessKeyUp(Rml::Input::KI_A, Rml::Input::KM_CTRL);
        ctx->ProcessTextInput(text);
        settle(app);
    };
    type_replace("Ranger");
    ST(model.draft().values.hunter_name == "Ranger");
    if (auto* input = screens.setup()->GetElementById("name-input"))
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(input)) ST(control->GetValue() == "Ranger");
    app.dispatch(Action::nav_left); settle(app);
    ST(screens.focused_id() == "name-input");   // left/right edit the caret, no focus change
    app.dispatch(Action::focus_next); settle(app);
    ST(screens.focused_id() == "folder-input");
    app.dispatch(Action::focus_next); settle(app);
    ST(screens.focused_id() == "browse-button");
    app.dispatch(Action::focus_previous); settle(app);
    ST(screens.focused_id() == "folder-input");
    // Down from a single-line field: does RmlUi keep it or move focus? Record only.
    app.dispatch(Action::nav_down); settle(app);
    std::cout << "self-test: Down inside a text field moved focus to '" << screens.focused_id() << "'\n";
    // Cancel via back: edits discarded, lodge focus restored to Profile.
    app.dispatch(Action::back); settle(app);
    ST(model.screen() == Screen::lodge);
    ST(model.confirmed_settings().hunter_name == "Evaluation Hunter");
    ST(screens.focused_id() == "dest-profile");

    // 4. Setup confirm with a real folder check on the worker.
    app.dispatch(Action::confirm); settle(app);
    ST(screens.focused_id() == "name-input");
    ST(model.draft().values.hunter_name == "Evaluation Hunter");   // draft restarted from confirmed
    if (auto* input = screens.setup()->GetElementById("name-input")) {
        auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(input);
        ST(control && control->GetValue() == "Evaluation Hunter");   // widget shows the draft
    }
    type_replace("Björn Ødegård");                                   // non-ASCII through the widget
    ST(model.draft().values.hunter_name == "Björn Ødegård");
    app.dispatch(Action::focus_next); settle(app);
    ST(screens.focused_id() == "folder-input");
    type_replace(app.asset_root().u8string().c_str());
    if (auto* confirm = screens.setup()->GetElementById("setup-confirm")) confirm->Click();
    ST(wait_for(app, [&] { return model.screen() == Screen::lodge; }));
    ST(model.confirmed_settings().hunter_name == "Björn Ødegård");
    ST(model.confirmed_settings().content_folder == app.asset_root().u8string());
    // A missing folder is an error state on the same screen.
    app.dispatch(Action::confirm); settle(app);
    app.dispatch(Action::focus_next); settle(app);
    ST(screens.focused_id() == "folder-input");
    type_replace((app.asset_root() / "definitely-missing").u8string().c_str());
    if (auto* confirm = screens.setup()->GetElementById("setup-confirm")) confirm->Click();
    ST(wait_for(app, [&] { return model.draft().folder_status == FolderStatus::error; }));
    ST(model.screen() == Screen::setup);
    ST(app.capture("03-setup-folder-error"));
    ST(model.confirmed_settings().content_folder == app.asset_root().u8string());
    app.dispatch(Action::back); settle(app);
    ST(model.screen() == Screen::lodge);

    // 5. Console: list focus, selection by Enter, details, modal preview.
    app.dispatch(Action::nav_left); settle(app);
    ST(screens.focused_id() == "dest-expeditions");
    app.dispatch(Action::confirm); settle(app);
    ST(model.screen() == Screen::console);
    ST(screens.focused_id() == "exp-" + ids.expedition_ids[0]);
    ST(app.capture("04-console-empty-selection"));
    app.dispatch(Action::nav_down); settle(app);
    ST(screens.focused_id() == "exp-" + ids.expedition_ids[1]);
    app.dispatch(Action::confirm); settle(app);
    ST(model.selected_expedition() == ids.expedition_ids[1]);
    ST(screens.focused_id() == "exp-" + ids.expedition_ids[1]);   // the row was not rebuilt
    if (auto* row = screens.console()->GetElementById("exp-" + ids.expedition_ids[1])) ST(row->IsClassSet("selected"));
    if (auto* row = screens.console()->GetElementById("exp-" + ids.expedition_ids[0])) ST(!row->IsClassSet("selected"));
    app.dispatch(Action::nav_down); settle(app);
    ST(screens.focused_id() == "exp-" + ids.expedition_ids[2]);
    app.dispatch(Action::nav_down); settle(app);
    ST(screens.focused_id() == "exp-" + ids.expedition_ids[2]);   // no wrap past the end
    app.dispatch(Action::nav_right); settle(app);
    const auto in_details = screens.focused_id();
    std::cout << "self-test: Right from the list focused '" << in_details << "'\n";
    ST(in_details == "area-select");
    ST(app.capture("05-console-details"));
    app.dispatch(Action::nav_left); settle(app);
    ST(screens.focused_id() == "exp-" + ids.expedition_ids[1]);   // Left returns to the selected row
    app.dispatch(Action::nav_right); settle(app);
    app.dispatch(Action::nav_down); settle(app);
    ST(screens.focused_id() == "weapon-select");                   // vertical nav inside the pane
    // Duplicate hunter names are distinct rows with distinct identities.
    ST(screens.console()->GetElementById("hunter-" + ids.hunter_ids[0]) != nullptr);
    ST(screens.console()->GetElementById("hunter-" + ids.hunter_ids[1]) != nullptr);
    if (auto* row = screens.console()->GetElementById("hunter-" + ids.hunter_ids[1])) { row->Focus(true); settle(app); app.dispatch(Action::confirm); settle(app); }
    ST(model.viewed_hunter() == ids.hunter_ids[1]);
    if (auto* row = screens.console()->GetElementById("hunter-" + ids.hunter_ids[2])) ST(row->GetInnerRML().find("Bj") != std::string::npos);

    // Dropdown: value binding both ways; Back closes an open box before leaving.
    auto* area = rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(screens.console()->GetElementById("area-select"));
    ST(area != nullptr);
    if (area) {
        std::cout << "self-test: area select has " << area->GetNumOptions() << " options\n";
        ST(area->GetNumOptions() == static_cast<int>(PresentationModel::demo_areas().size()));
        ST(area->GetValue() == model.area());
        model.set_area("demo-area-2"); screens.sync(); settle(app);
        ST(area->GetValue() == "demo-area-2");            // model -> widget
        area->SetValue("demo-area-3"); settle(app);
        ST(model.area() == "demo-area-3");                // widget -> model
        area->Focus(true); settle(app);
        area->Click(); settle(app);
        ST(select_open(area));
        ST(app.capture("06-console-dropdown-open"));
        ST(screens.handle_back()); settle(app);
        ST(!select_open(area));
        ST(model.screen() == Screen::console);            // back only closed the box
        ST(screens.focused_id() == "area-select");
    }

    // Modal preview: focus moves in, Back closes it and restores focus.
    auto* preview_button = screens.console()->GetElementById("preview-button");
    ST(preview_button != nullptr);
    if (preview_button) { preview_button->Focus(true); settle(app); }
    app.dispatch(Action::confirm); settle(app);
    ST(model.preview_open());
    ST(screens.preview()->IsModal());
    ST(screens.focused_id() == "preview-close");
    ST(app.capture("07-preview-dialog"));
    ST(screens.preview()->GetInnerRML().find(ids.expedition_ids[1]) != std::string::npos);
    // A click landing on the console behind the modal must not act.
    if (auto* back = screens.console()->GetElementById("console-back")) {
        const auto box = back->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto size = back->GetBox().GetSize(Rml::BoxArea::Border);
        const float dp = ctx->GetDensityIndependentPixelRatio();
        const int x = static_cast<int>((box.x + size.x / 2) * dp), y = static_cast<int>((box.y + size.y / 2) * dp);
        ctx->ProcessMouseMove(x, y, 0);
        ctx->ProcessMouseButtonDown(0, 0);
        ctx->ProcessMouseButtonUp(0, 0);
        settle(app);
        ST(model.screen() == Screen::console && model.preview_open());
    }
    app.dispatch(Action::back); settle(app);
    ST(!model.preview_open());
    ST(model.screen() == Screen::console);
    ST(screens.focused_id() == "preview-button");
    ST(model.selected_expedition() == ids.expedition_ids[1]);   // dismissal changed nothing
    // Launch is disabled: Tab from Preview skips it, and its explanation is plain text.
    app.dispatch(Action::focus_next); settle(app);
    ST(screens.focused_id() != "launch-button");
    app.dispatch(Action::focus_previous); settle(app);
    ST(screens.focused_id() == "preview-button");
    ST(screens.console()->GetInnerRML().find("outside this evaluation") != std::string::npos);

    // 6. Back to the lodge restores the destination used to leave.
    app.dispatch(Action::back); settle(app);
    ST(model.screen() == Screen::lodge);
    ST(screens.focused_id() == "dest-expeditions");

    // 7. Aspect fit and display scale for the lodge stage.
    auto near = [](float a, float b) { return std::fabs(a - b) < 1.5f; };
    app.apply_pixel_size(800, 900); settle(app);
    ST(near(screens.lodge_stage_dp().x, 800.f) && near(screens.lodge_stage_dp().y, 450.f));
    ST(app.capture("08-lodge-narrow-tall"));
    app.apply_pixel_size(1600, 500); settle(app);
    ST(near(screens.lodge_stage_dp().x, 888.9f) && near(screens.lodge_stage_dp().y, 500.f));
    ST(app.capture("09-lodge-ultrawide"));
    ctx->SetDensityIndependentPixelRatio(2.f);
    app.apply_pixel_size(1600, 500); settle(app);
    ST(near(screens.lodge_stage_dp().x, 444.4f) && near(screens.lodge_stage_dp().y, 250.f));
    ST(app.capture("10-lodge-ultrawide-2x-scale"));
    app.dispatch(Action::confirm); settle(app);   // console at 2x: text and layout scale, not the image
    ST(app.capture("11-console-2x-scale"));
    app.dispatch(Action::back); settle(app);
    ctx->SetDensityIndependentPixelRatio(1.f);
    app.apply_pixel_size(1280, 720); settle(app);
    ST(near(screens.lodge_stage_dp().x, 1280.f) && near(screens.lodge_stage_dp().y, 720.f));
    // Hotspots share the stage transform: the Exit hotspot sits at 68%..92% of the stage width.
    if (auto* exit = screens.lodge()->GetElementById("dest-exit"); exit) {
        auto* stage = screens.lodge()->GetElementById("lodge-stage");
        const float left = exit->GetAbsoluteOffset(Rml::BoxArea::Border).x - stage->GetAbsoluteOffset(Rml::BoxArea::Border).x;
        ST(near(left, 0.68f * screens.lodge_stage_dp().x));
    }

    // 8. Virtual controller: D-pad, stick dead zone with hysteresis, repeat,
    //    confirm/back, and held state cleared on window focus loss. These are
    //    SDL virtual-device events, not a physical controller.
    {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = "c2 self-test virtual gamepad";
        const SDL_JoystickID vid = SDL_AttachVirtualJoystick(&desc);
        ST(vid != 0);
        SDL_Joystick* joystick = vid ? SDL_OpenJoystick(vid) : nullptr;
        ST(joystick != nullptr);
        if (joystick) {
            settle(app);   // delivers SDL_EVENT_GAMEPAD_ADDED to the app
            std::cout << "self-test: controllers open in the app:";
            for (const auto& n : app.gamepad_names()) std::cout << " [" << n << "]";
            std::cout << '\n';
            ST(app.gamepad_connected());
            auto press = [&](SDL_GamepadButton button) {
                SDL_SetJoystickVirtualButton(joystick, button, true);
                SDL_UpdateJoysticks(); app.step(false);
                SDL_SetJoystickVirtualButton(joystick, button, false);
                SDL_UpdateJoysticks(); app.step(false);
            };
            ST(screens.focused_id() == "dest-expeditions");
            press(SDL_GAMEPAD_BUTTON_DPAD_RIGHT); settle(app);
            ST(screens.focused_id() == "dest-profile");
            press(SDL_GAMEPAD_BUTTON_DPAD_LEFT); settle(app);
            ST(screens.focused_id() == "dest-expeditions");
            // Stick inside the dead zone does nothing; past it moves once.
            SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, static_cast<Sint16>(0.40f * 32767));
            SDL_UpdateJoysticks(); settle(app);
            ST(screens.focused_id() == "dest-expeditions");
            SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, static_cast<Sint16>(0.80f * 32767));
            SDL_UpdateJoysticks(); settle(app);
            ST(screens.focused_id() == "dest-profile");
            // Holding past the repeat delay repeats; a synthetic focus loss clears it.
            const auto t0 = SDL_GetTicks();
            while (SDL_GetTicks() - t0 < GamepadInput::kRepeatDelayMs + GamepadInput::kRepeatIntervalMs / 2) { app.step(false); SDL_Delay(5); }
            ST(screens.focused_id() == "dest-exit");
            SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, static_cast<Sint16>(-0.80f * 32767));
            SDL_UpdateJoysticks(); settle(app);
            ST(screens.focused_id() == "dest-profile");
            SDL_Event lost{};
            lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            lost.window.windowID = SDL_GetWindowID(app.window());
            SDL_PushEvent(&lost);
            const auto t1 = SDL_GetTicks();
            while (SDL_GetTicks() - t1 < GamepadInput::kRepeatDelayMs + 2 * GamepadInput::kRepeatIntervalMs) { app.step(false); SDL_Delay(5); }
            ST(screens.focused_id() == "dest-profile");   // no stuck repeat after focus loss
            SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 0);
            SDL_UpdateJoysticks(); settle(app);
            // South confirms into setup, East backs out with focus restored.
            press(SDL_GAMEPAD_BUTTON_SOUTH); settle(app);
            ST(model.screen() == Screen::setup);
            press(SDL_GAMEPAD_BUTTON_EAST); settle(app);
            ST(model.screen() == Screen::lodge);
            ST(screens.focused_id() == "dest-profile");
            press(SDL_GAMEPAD_BUTTON_DPAD_LEFT); settle(app);
            ST(screens.focused_id() == "dest-expeditions");
            SDL_CloseJoystick(joystick);
        }
        if (vid) { SDL_DetachVirtualJoystick(vid); settle(app); }
        std::cout << "self-test: after detaching the virtual controller, controllers open: " << app.gamepad_names().size() << '\n';
    }

    // 9. A store reload keeps identities and drops nothing that still exists.
    app.reload_store();
    ST(wait_for(app, [&] { return model.load_state() != LoadState::loading; }));
    ST(model.load_state() == LoadState::ready);
    ST(model.selected_expedition() == ids.expedition_ids[1]);

    // 10. Exit through the lodge destination.
    app.dispatch(Action::nav_right); app.dispatch(Action::nav_right); settle(app);
    ST(screens.focused_id() == "dest-exit");
    app.dispatch(Action::confirm); settle(app);
    ST(model.exit_requested());
    ST(!app.running());

    if (failures) {
        std::cerr << "self-test: " << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "self-test passed\n";
    return 0;
}
} // namespace c2::frontend::gui::app
