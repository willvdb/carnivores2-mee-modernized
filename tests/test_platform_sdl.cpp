#include "../Hunt/Platform/Platform.h"
#include "../Hunt/Debug/Log.h"
#include "../Hunt/Platform/PlatformSDLInternal.h"
#include <SDL3/SDL.h>
#include <gtest/gtest.h>

void LogWrite(LogLevel, const char*, int, const char*, ...) {}

class PlatformSDL : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy"));
        ASSERT_TRUE(Platform::InitializeApplication()) << Platform::LastError();
        SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
    }
    void TearDown() override { Platform::ShutdownApplication(); }
    void Push(Uint32 type) {
        SDL_Event event{}; event.type=type;
        ASSERT_TRUE(SDL_PushEvent(&event));
    }
    void PushKey(bool down) {
        SDL_Event event{}; event.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
        event.key.down=down; event.key.scancode=SDL_SCANCODE_W; event.key.key=SDLK_W;
        ASSERT_TRUE(SDL_PushEvent(&event));
    }
};
TEST_F(PlatformSDL, OneEventPerIterationAndNoPrematureKeyboardState)
{
    PushKey(true); PushKey(false);
    int code=-7; Platform::Event event;
    EXPECT_EQ(Platform::PumpOneEvent(code,&event),Platform::PumpResult::Dispatched);
    EXPECT_EQ(event.type,Platform::EventType::KeyDown); EXPECT_EQ(event.key.key,'W');
    Platform::KeyboardState state{}; ASSERT_TRUE(Platform::PollKeyboardState(state));
    EXPECT_EQ(state['W'],0x80);
    EXPECT_EQ(Platform::PumpOneEvent(code,&event),Platform::PumpResult::Dispatched);
    EXPECT_EQ(event.type,Platform::EventType::None);
    ASSERT_TRUE(Platform::PollKeyboardState(state)); EXPECT_EQ(state['W'],0);
    EXPECT_EQ(Platform::PumpOneEvent(code,&event),Platform::PumpResult::Idle);
    EXPECT_EQ(code,-7);
}
TEST_F(PlatformSDL, FocusLossClearsDownAndReportsEachTransitionInOrder)
{
    PushKey(true); Push(SDL_EVENT_WINDOW_FOCUS_LOST); Push(SDL_EVENT_WINDOW_FOCUS_GAINED);
    int code=-7; Platform::Event event;
    Platform::PumpOneEvent(code,&event);
    EXPECT_EQ(Platform::PumpOneEvent(code,&event),Platform::PumpResult::Dispatched);
    EXPECT_EQ(event.type,Platform::EventType::FocusChanged); EXPECT_FALSE(event.focused);
    Platform::KeyboardState state{}; ASSERT_TRUE(Platform::PollKeyboardState(state)); EXPECT_EQ(state['W'],0);
    EXPECT_EQ(Platform::PumpOneEvent(code,&event),Platform::PumpResult::Dispatched);
    EXPECT_EQ(event.type,Platform::EventType::FocusChanged); EXPECT_TRUE(event.focused);
    EXPECT_EQ(Platform::PumpOneEvent(code,&event),Platform::PumpResult::Idle);
    EXPECT_EQ(event.type,Platform::EventType::None); EXPECT_EQ(code,-7);
}
TEST_F(PlatformSDL, UnhandledEventStillConsumesOneIteration)
{
    Push(SDL_EVENT_USER); PushKey(true);
    int code=123; Platform::Event event;
    EXPECT_EQ(Platform::PumpOneEvent(code,&event),Platform::PumpResult::Dispatched);
    EXPECT_EQ(event.type,Platform::EventType::None);
    EXPECT_EQ(Platform::PumpOneEvent(code,&event),Platform::PumpResult::Dispatched);
    EXPECT_EQ(event.type,Platform::EventType::KeyDown);
}
TEST_F(PlatformSDL, WindowCloseReturnsNormalQuit)
{
    Push(SDL_EVENT_WINDOW_CLOSE_REQUESTED);
    int code=-7;
    EXPECT_EQ(Platform::PumpOneEvent(code),Platform::PumpResult::Quit); EXPECT_EQ(code,0);
}
TEST_F(PlatformSDL, ApplicationQuitAndRequestedQuitReturnZero)
{
    Push(SDL_EVENT_QUIT);
    int code=-7;
    EXPECT_EQ(Platform::PumpOneEvent(code),Platform::PumpResult::Quit); EXPECT_EQ(code,0);
    Platform::RequestQuit(); code=-7;
    EXPECT_EQ(Platform::PumpOneEvent(code),Platform::PumpResult::Quit); EXPECT_EQ(code,0);
}
TEST_F(PlatformSDL, TimerUnitsAndInputHints)
{
    EXPECT_GT(Platform::CounterFrequency(),0);
    const auto start=Platform::Counter(); Platform::SleepMilliseconds(1);
    EXPECT_GE(Platform::Counter(),start);
    const auto before=Platform::WrapMilliseconds(SDL_GetTicks());
    const auto actual=Platform::Milliseconds();
    const auto after=Platform::WrapMilliseconds(SDL_GetTicks());
    EXPECT_LE(actual-before,after-before);
    EXPECT_STREQ(SDL_GetHint(SDL_HINT_MOUSE_EMULATE_WARP_WITH_RELATIVE),"0");
    EXPECT_STREQ(SDL_GetHint(SDL_HINT_MOUSE_AUTO_CAPTURE),"0");
    EXPECT_STREQ(SDL_GetHint(SDL_HINT_WINDOWS_RAW_KEYBOARD),"0");
    EXPECT_STREQ(SDL_GetHint(SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4),"0");
}
TEST_F(PlatformSDL, NewEventsBehindSDLPollBoundaryAreNotAnIdleFrame)
{
    Push(SDL_EVENT_USER);
    SDL_Event ignored{};
    ASSERT_TRUE(SDL_PollEvent(&ignored)); // Leaves SDL's end-of-poll marker.
    PushKey(true);
    int code=-7; Platform::Event event;
    EXPECT_EQ(Platform::PumpOneEvent(code,&event),Platform::PumpResult::Dispatched);
    EXPECT_EQ(event.type,Platform::EventType::KeyDown);
    EXPECT_EQ(event.key.key,'W');
    EXPECT_EQ(Platform::PumpOneEvent(code,&event),Platform::PumpResult::Idle);
    EXPECT_EQ(code,-7);
}

namespace {
int relativeReads = 0;
SDL_MouseButtonFlags SDLCALL ReadRelativeMotion(float* x, float* y)
{
    ++relativeReads;
    *x = 0.25f; *y = -12.75f;
    return 0;
}
}
TEST(SDLMouseLook, WaylandConsumesFractionalMotionWithoutAbsoluteCenterOrSensitivityChanges)
{
    relativeReads = 0;
    const auto delta = Platform::SDLDetails::ReadWaylandMouseLook(true, true, ReadRelativeMotion);
    EXPECT_FLOAT_EQ(delta.x, 0.25f); EXPECT_FLOAT_EQ(delta.y, -12.75f);
    EXPECT_EQ(relativeReads, 1);
}
TEST(SDLMouseLook, UncapturedOrUnfocusedWaylandDrainsMotionWithoutMovingView)
{
    relativeReads = 0;
    for (auto flags : {std::pair{false, false}, std::pair{true, false}, std::pair{false, true}}) {
        const auto delta = Platform::SDLDetails::ReadWaylandMouseLook(flags.first, flags.second, ReadRelativeMotion);
        EXPECT_FLOAT_EQ(delta.x, 0); EXPECT_FLOAT_EQ(delta.y, 0);
    }
    EXPECT_EQ(relativeReads, 3);
}
