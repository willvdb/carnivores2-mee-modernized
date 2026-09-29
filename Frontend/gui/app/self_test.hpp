#pragma once
namespace c2::frontend::gui::app {
class App;
// Drives the real RmlUi documents through the app's own dispatch path
// (no human, no display needed with SDL_VIDEODRIVER=offscreen). Returns a
// process exit code; failures are printed with their check.
int run_self_test(App& app);
} // namespace c2::frontend::gui::app
