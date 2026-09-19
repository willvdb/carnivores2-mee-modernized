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

namespace {
struct FullscreenFixture : ::testing::Test {
    inline static SDL_DisplayMode mode{};
    inline static SDL_DisplayMode reported{};
    inline static bool acceptMode, acceptEnter, synchronized, settled, shown, pixelsValid;
    inline static int setCalls, enterCalls, syncCalls;
    inline static SDL_DisplayID actualDisplay;
    inline static Platform::Size actualPixels;
    Platform::SDLDetails::FullscreenAPI api{
        [](SDL_Window*, const SDL_DisplayMode*) -> bool { ++setCalls; return acceptMode; },
        [](SDL_Window*, bool enter) -> bool { EXPECT_TRUE(enter); ++enterCalls; return acceptEnter; },
        [](SDL_Window*) -> bool { ++syncCalls; settled = true; return synchronized; },
        [](SDL_Window*) -> SDL_WindowFlags { return settled && shown ? SDL_WINDOW_FULLSCREEN : 0; },
        [](SDL_Window*) -> SDL_DisplayID { return settled ? actualDisplay : 1; },
        [](SDL_Window*, int* w, int* h) -> bool { *w=actualPixels.width; *h=actualPixels.height; return pixelsValid; },
        [](SDL_DisplayID) -> const SDL_DisplayMode* { return &reported; }
    };
    void SetUp() override {
        mode={}; mode.displayID=2; mode.w=800; mode.h=600;
        mode.format=SDL_PIXELFORMAT_XRGB8888; mode.refresh_rate_numerator=60000; mode.refresh_rate_denominator=1001;
        reported=mode; acceptMode=acceptEnter=synchronized=shown=pixelsValid=true; settled=false;
        setCalls=enterCalls=syncCalls=0; actualDisplay=2; actualPixels={800,600};
    }
    Platform::SDLDetails::FullscreenResult Apply(bool emulated=false) {
        return Platform::SDLDetails::EnterFullscreen(nullptr,mode,emulated,api);
    }
};
}
TEST_F(FullscreenFixture, WaitsForDelayedStateBeforeConfirming)
{
    const auto result=Apply(); EXPECT_TRUE(result.confirmed); EXPECT_TRUE(result.synchronized);
    EXPECT_EQ(result.display,2u); EXPECT_EQ(syncCalls,1); EXPECT_EQ(enterCalls,1);
}
TEST_F(FullscreenFixture, RejectedModeDoesNotEnterOrWait)
{
    acceptMode=false; const auto result=Apply(); EXPECT_FALSE(result.requested); EXPECT_FALSE(result.confirmed);
    EXPECT_EQ(enterCalls,0); EXPECT_EQ(syncCalls,0);
}
TEST_F(FullscreenFixture, RejectedEntryDoesNotWaitOrReportSuccess)
{
    acceptEnter=false; EXPECT_FALSE(Apply().confirmed); EXPECT_EQ(syncCalls,0);
}
TEST_F(FullscreenFixture, TimeoutCannotBecomeSuccessEvenWhenCachedStateMatches)
{
    synchronized=false; EXPECT_FALSE(Apply().confirmed); EXPECT_EQ(syncCalls,1);
}
TEST_F(FullscreenFixture, WrongOutputAndRejectedFullscreenAreNotSuccess)
{
    actualDisplay=1; EXPECT_FALSE(Apply().confirmed);
    actualDisplay=2; shown=false; EXPECT_FALSE(Apply().confirmed);
}
TEST_F(FullscreenFixture, RenderSizeAndExactX11RefreshAreVerified)
{
    actualPixels.width=1024; EXPECT_FALSE(Apply().confirmed);
    actualPixels.width=800; reported.refresh_rate_numerator=60; reported.refresh_rate_denominator=1;
    EXPECT_FALSE(Apply().confirmed); // Never round 60000/1001 to 60 Hz.
    reported.refresh_rate_numerator=120000; reported.refresh_rate_denominator=2002;
    EXPECT_TRUE(Apply().confirmed);
    reported.w=1024; EXPECT_FALSE(Apply().confirmed);
}
TEST_F(FullscreenFixture, WaylandConfirmsPresentationWithoutClaimingPhysicalModeSwitch)
{
    reported.w=1920; reported.h=1080; reported.refresh_rate_numerator=144; reported.refresh_rate_denominator=1;
    const auto result=Apply(true); EXPECT_TRUE(result.confirmed);
    ASSERT_TRUE(result.currentMode); EXPECT_EQ(result.currentMode->size.width,1920);
    actualDisplay=1; EXPECT_FALSE(Apply(true).confirmed);
    actualDisplay=2; actualPixels.width=1920; EXPECT_FALSE(Apply(true).confirmed);
}

TEST_F(FullscreenFixture, FailedPixelQueryCannotConfirmCachedDimensions)
{
    pixelsValid=false; EXPECT_FALSE(Apply().confirmed);
}

TEST_F(FullscreenFixture, LostSecondaryUsesPrimaryAutomaticThenPrimaryDesktopFallback)
{
    using namespace Platform;
    const DisplayTarget preference{{{1920,0},{1280,1024}}};
    const auto preferredMode = SDLDetails::CopyDisplayMode(mode);
    SDLDetails::WindowDisplay mapped{2, preference, preferredMode};
    ASSERT_TRUE(SDLDetails::RefreshWindowDisplayBounds(mapped,
        [](SDL_DisplayID id, SDL_Rect* bounds) -> bool {
            EXPECT_EQ(id,2u); *bounds={1920,0,1280,1024}; return true;
        }));
    ASSERT_TRUE(mapped.target); ASSERT_TRUE(mapped.exclusiveMode);
    EXPECT_EQ(mapped.id,2u);
    EXPECT_FALSE(SDLDetails::RefreshWindowDisplayBounds(mapped,
        [](SDL_DisplayID id, SDL_Rect*) -> bool { EXPECT_EQ(id,2u); return false; },
        []() -> SDL_DisplayID { return 1; }));
    EXPECT_EQ(mapped.id,1u);
    EXPECT_FALSE(mapped.target);
    EXPECT_FALSE(mapped.exclusiveMode); // Secondary rational refresh cannot be reused.

    SDL_DisplayMode closest{};
    EXPECT_FALSE(SDLDetails::FindAutomaticDisplayMode(mapped.id,{800,600},closest,
        [](SDL_DisplayID id, int w, int h, float rate, bool, SDL_DisplayMode*) -> bool {
            EXPECT_EQ(id,1u); EXPECT_EQ(w,800); EXPECT_EQ(h,600); EXPECT_EQ(rate,0);
            return false; // Primary cannot satisfy the requested size.
        }));
    reported.displayID=1; reported.w=1920; reported.h=1080;
    reported.refresh_rate_numerator=144; reported.refresh_rate_denominator=1;
    actualDisplay=1; actualPixels={1920,1080};
    api.setMode=[](SDL_Window*, const SDL_DisplayMode* request) -> bool {
        ++setCalls;
        EXPECT_EQ(request->displayID,1u); EXPECT_EQ(request->w,1920); EXPECT_EQ(request->h,1080);
        EXPECT_EQ(request->refresh_rate_numerator,144); EXPECT_EQ(request->refresh_rate_denominator,1);
        return true;
    };
    const auto result=SDLDetails::EnterDesktopFullscreen(nullptr,mapped.id,api,
        [](SDL_DisplayID id) -> const SDL_DisplayMode* { EXPECT_EQ(id,1u); return &reported; });
    EXPECT_TRUE(result.confirmed); EXPECT_EQ(result.display,1u); EXPECT_EQ(setCalls,1);
    EXPECT_EQ(preference.bounds.origin.x,1920); // Caller-owned preference is retained.
    EXPECT_EQ(preferredMode.refresh.numerator,60000u);
    EXPECT_EQ(preferredMode.refresh.denominator,1001u);
}
