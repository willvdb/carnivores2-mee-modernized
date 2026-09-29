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
    ST(model.snapshot() && model.snapshot()->associations.size() == 1);
    const auto expedition_id = to_utf8(model.snapshot()->associations.front().instance_id);
    const auto first_expedition_id = model.snapshot()->expeditions.front().id;
    ST(model.snapshot()->hunters.size()==4); ST(model.snapshot()->expeditions.size()==3);
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

    // 5. Real Console bindings and owned asset-free play loop.
    app.dispatch(Action::nav_left); settle(app);
    app.dispatch(Action::confirm); settle(app);
    ST(model.screen() == Screen::console);
    ST(screens.focused_id() == "exp-" + first_expedition_id);
    app.dispatch(Action::confirm); settle(app);
    ST(model.selected_expedition() == first_expedition_id);
    app.dispatch(Action::nav_down); settle(app);
    ST(screens.focused_id()=="exp-"+model.snapshot()->expeditions[1].id);
    app.dispatch(Action::nav_down); settle(app);
    ST(screens.focused_id()=="exp-"+model.snapshot()->expeditions[2].id);
    app.dispatch(Action::nav_down); settle(app);
    ST(screens.focused_id()=="exp-"+model.snapshot()->expeditions[2].id);
    std::vector<std::string> duplicate_ids;
    for(const auto& h:model.snapshot()->hunters) if(h.name=="Fixture Hunter") duplicate_ids.push_back(h.id);
    ST(duplicate_ids.size()==2); ST(duplicate_ids[0]!=duplicate_ids[1]);
    for(const auto& id:duplicate_ids) ST(screens.console()->GetElementById("hunter-"+id)!=nullptr);
    ST(app.capture("04a-console-before-navigation"));
    app.dispatch(Action::nav_right); settle(app);
    std::cout << "self-test: console cross-pane focus " << screens.focused_id() << std::endl;
    ST(screens.focused_id() == "association-select");
    auto* association = rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(screens.console()->GetElementById("association-select"));
    ST(association != nullptr);

    if (!association) return 1;
    association->SetValue(to_utf8(model.snapshot()->associations.front().id));
    ST(wait_for(app,[&]{return !model.loop.busy() && model.loop.catalog().has_value();}));
    ST(model.loop.error().empty());
    ST(model.selected_expedition()==expedition_id);
    ST(model.viewed_hunter()==to_utf8(model.snapshot()->associations.front().hunter_id));
    association->Focus(true); association->Click(); settle(app);
    ST(select_open(association));
    app.dispatch(Action::back); settle(app);
    ST(!select_open(association)); ST(model.screen()==Screen::console);
    auto* disabled_launch=screens.console()->GetElementById("launch-button");
    ST(disabled_launch && disabled_launch->IsClassSet("disabled"));
    ST(app.capture("04-console-loadout"));
    auto activate = [&](Rml::ElementDocument* doc, const char* id) {
        auto* button=doc->GetElementById(id); ST(button != nullptr);
        if(button) { button->Focus(true); button->ScrollIntoView(); settle(app); app.dispatch(Action::confirm); settle(app); }
    };
    auto finish = [&] {
        ST(wait_for(app,[&]{return !model.loop.busy();},10000));
        if(!model.loop.error().empty()) std::cerr<<"loop error: "<<model.loop.error()<<'\n';
        ST(model.loop.error().empty()); settle(app);
    };
    activate(screens.console(),"plan-button"); finish(); ST(model.loop.state()==LoopState::validated);
    ST(app.capture("05-validated-intent"));
    activate(screens.console(),"prepare-button"); finish(); ST(model.loop.state()==LoopState::prepared);
    if(!model.loop.session()) return 1;
    const auto first_session=model.loop.session()->id;
    const auto g0=model.loop.session()->generation;
    ST(!model.loop.can(Operation::accept));
    ST(app.capture("06-prepared"));
    activate(screens.console(),"launch-button");
    app.dispatch(Action::back); settle(app); ST(model.screen()==Screen::lodge);
    finish(); ST(model.loop.state()==LoopState::returned);
    app.dispatch(Action::confirm); settle(app); ST(model.screen()==Screen::console);
    ST(model.loop.session()->state==U"candidate"); ST(!model.loop.can(Operation::accept));
    activate(screens.console(),"review-button"); settle(app);
    ST(screens.review()->IsModal()); ST(screens.focused_id()=="review-close");
    ST(app.capture("07-return-review"));
    activate(screens.review(),"inspect-button"); finish(); ST(model.loop.state()==LoopState::reviewing);
    activate(screens.review(),"acceptance-preview"); finish(); ST(model.loop.can(Operation::accept));
    ST(model.loop.preview() && !model.loop.preview()->candidate_sha256.empty());
    ST(!screens.review()->GetElementById("accept-button")->IsClassSet("disabled"));
    ST(app.capture("08-acceptance-preview"));
    activate(screens.review(),"decline-button"); settle(app); ST(model.loop.state()==LoopState::declined);
    ST(!model.loop.can(Operation::accept)); ST(model.loop.association()->current_generation==g0);
    activate(screens.console(),"review-button"); settle(app);
    activate(screens.review(),"acceptance-preview"); finish();
    activate(screens.review(),"accept-button"); finish(); ST(model.loop.state()==LoopState::accepted);
    ST(app.capture("09-acceptance-receipt"));
    ST(model.loop.association()->current_generation!=g0);
    app.dispatch(Action::back); settle(app); ST(screens.focused_id()=="review-button");
    activate(screens.console(),"prepare-button"); finish(); ST(model.loop.state()==LoopState::prepared);
    ST(model.loop.session()->id!=first_session);
    ST(model.loop.session()->generation==model.loop.association()->current_generation);
    ST(app.capture("10-prepared-from-accepted-generation"));

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
    ST(model.selected_expedition() == expedition_id);

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
