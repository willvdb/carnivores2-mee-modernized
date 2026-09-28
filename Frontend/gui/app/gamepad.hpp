#pragma once
// Controller input mapped to the same semantic actions the keyboard uses.
// Real focus navigation: actions become RmlUi key events, never a fake cursor.
#include <SDL3/SDL.h>
#include <cstdint>
#include <vector>

namespace c2::frontend::gui::app {
enum class Action { nav_up, nav_down, nav_left, nav_right, confirm, back, focus_next, focus_previous };

class GamepadInput {
public:
    GamepadInput();
    ~GamepadInput();
    GamepadInput(const GamepadInput&) = delete;
    GamepadInput& operator=(const GamepadInput&) = delete;
    // Consumes gamepad events; appends immediate actions. Returns true if the
    // event was a gamepad event.
    bool handle_event(const SDL_Event& event, std::uint64_t now_ms, std::vector<Action>& out);
    // Emits repeat actions for a held direction. Call once per loop iteration.
    void tick(std::uint64_t now_ms, std::vector<Action>& out);
    // Drops held state (window focus lost, device removed).
    void clear_held();
    // Milliseconds until the next repeat is due, or a large value when idle.
    std::uint32_t next_wake_ms(std::uint64_t now_ms) const;
    bool connected() const noexcept { return gamepad_ != nullptr; }
    // Tunables (also documented in the evaluation record).
    static constexpr float kDeadZoneEnter = 0.55f;
    static constexpr float kDeadZoneExit = 0.35f;
    static constexpr std::uint64_t kRepeatDelayMs = 400;
    static constexpr std::uint64_t kRepeatIntervalMs = 120;

private:
    enum class Dir { none, up, down, left, right };
    void open_first();
    void set_direction(Dir dir, std::uint64_t now_ms, std::vector<Action>& out);
    static Action to_action(Dir dir);
    SDL_Gamepad* gamepad_ = nullptr;
    Dir held_ = Dir::none;
    Dir stick_ = Dir::none;
    Dir dpad_ = Dir::none;
    std::uint64_t held_since_ = 0;
    std::uint64_t last_repeat_ = 0;
    float axis_x_ = 0.f, axis_y_ = 0.f;
};
} // namespace c2::frontend::gui::app
