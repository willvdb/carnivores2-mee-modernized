#include "gamepad.hpp"
#include <cmath>

namespace c2::frontend::gui::app {
GamepadInput::GamepadInput() { open_first(); }
GamepadInput::~GamepadInput() {
    if (gamepad_) SDL_CloseGamepad(gamepad_);
}

void GamepadInput::open_first() {
    if (gamepad_) return;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids) {
        for (int i = 0; i < count && !gamepad_; ++i) gamepad_ = SDL_OpenGamepad(ids[i]);
        SDL_free(ids);
    }
}

Action GamepadInput::to_action(Dir dir) {
    switch (dir) {
    case Dir::up: return Action::nav_up;
    case Dir::down: return Action::nav_down;
    case Dir::left: return Action::nav_left;
    default: return Action::nav_right;
    }
}

void GamepadInput::set_direction(Dir dir, std::uint64_t now_ms, std::vector<Action>& out) {
    if (dir == held_) return;
    held_ = dir;
    if (dir == Dir::none) return;
    held_since_ = now_ms;
    last_repeat_ = now_ms;
    out.push_back(to_action(dir));
}

bool GamepadInput::handle_event(const SDL_Event& event, std::uint64_t now_ms, std::vector<Action>& out) {
    switch (event.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        if (!gamepad_) gamepad_ = SDL_OpenGamepad(event.gdevice.which);
        return true;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (gamepad_ && SDL_GetGamepadID(gamepad_) == event.gdevice.which) {
            SDL_CloseGamepad(gamepad_);
            gamepad_ = nullptr;
            clear_held();
            open_first();   // fall back to another connected controller, if any
        }
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        Dir d = Dir::none;
        switch (event.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: d = Dir::up; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: d = Dir::down; break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: d = Dir::left; break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: d = Dir::right; break;
        case SDL_GAMEPAD_BUTTON_SOUTH: out.push_back(Action::confirm); return true;
        case SDL_GAMEPAD_BUTTON_EAST: out.push_back(Action::back); return true;
        case SDL_GAMEPAD_BUTTON_BACK: out.push_back(Action::back); return true;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: out.push_back(Action::focus_previous); return true;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: out.push_back(Action::focus_next); return true;
        default: return true;
        }
        dpad_ = d;
        set_direction(d, now_ms, out);
        return true;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        Dir d = Dir::none;
        switch (event.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: d = Dir::up; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: d = Dir::down; break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: d = Dir::left; break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: d = Dir::right; break;
        default: return true;
        }
        if (dpad_ == d) {
            dpad_ = Dir::none;
            set_direction(stick_, now_ms, out);
        }
        return true;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        const float value = event.gaxis.value / 32767.f;
        if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) axis_x_ = value;
        else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) axis_y_ = value;
        else return true;
        // Hysteresis: a direction engages past the enter threshold and releases
        // below the exit threshold, so jitter near the edge does not repeat.
        Dir next = stick_;
        const float ax = std::fabs(axis_x_), ay = std::fabs(axis_y_);
        const float mag = ax > ay ? ax : ay;
        if (stick_ != Dir::none && mag < kDeadZoneExit) next = Dir::none;
        if (mag >= kDeadZoneEnter) {
            if (ay >= ax) next = axis_y_ < 0 ? Dir::up : Dir::down;
            else next = axis_x_ < 0 ? Dir::left : Dir::right;
        }
        if (next != stick_) {
            stick_ = next;
            if (dpad_ == Dir::none) set_direction(stick_, now_ms, out);
        }
        return true;
    }
    default: return false;
    }
}

void GamepadInput::tick(std::uint64_t now_ms, std::vector<Action>& out) {
    if (held_ == Dir::none) return;
    if (now_ms - held_since_ < kRepeatDelayMs) return;
    if (now_ms - last_repeat_ < kRepeatIntervalMs) return;
    last_repeat_ = now_ms;
    out.push_back(to_action(held_));
}

void GamepadInput::clear_held() {
    held_ = Dir::none;
    stick_ = Dir::none;
    dpad_ = Dir::none;
    axis_x_ = axis_y_ = 0.f;
}

std::uint32_t GamepadInput::next_wake_ms(std::uint64_t now_ms) const {
    if (held_ == Dir::none) return 1000000;
    const std::uint64_t first = held_since_ + kRepeatDelayMs;
    const std::uint64_t due = first > last_repeat_ + kRepeatIntervalMs ? first : last_repeat_ + kRepeatIntervalMs;
    return due > now_ms ? static_cast<std::uint32_t>(due - now_ms) : 0;
}
} // namespace c2::frontend::gui::app
