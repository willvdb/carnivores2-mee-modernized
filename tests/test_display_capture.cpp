#include "../Hunt/Game/MouseCapture.h"
#include <gtest/gtest.h>
#include <vector>

namespace {
std::vector<bool> captureChanges;
int pointerResets = 0;

void StartCaptured()
{
    captureChanges.clear();
    pointerResets = 0;
    CaptureMouse(true);
    ASSERT_EQ(captureChanges, std::vector<bool>({true}));
}

void ExpectSuspendedEnableIsBlocked()
{
    ASSERT_TRUE(SuspendMouseCaptureForDisplay());
    ASSERT_EQ(captureChanges, std::vector<bool>({true, false}));
    CaptureMouse(true);
    EXPECT_EQ(captureChanges, std::vector<bool>({true, false}));
    EXPECT_EQ(pointerResets, 1);
}

void ExpectHealthyRecoveryRestoresCapture()
{
    const auto changesBeforeRecovery = captureChanges.size();
    ResumeMouseCaptureAfterDisplay(true);
    ASSERT_EQ(captureChanges.size(), changesBeforeRecovery + 1);
    EXPECT_TRUE(captureChanges.back());
    EXPECT_EQ(pointerResets, 2);
}
}

namespace Platform {
void SetMouseCapture(bool capture)
{
    captureChanges.push_back(capture);
}
}

void ResetMousePos()
{
    ++pointerResets;
}

TEST(DisplayCapture, InvalidDrawableBlocksFocusRegainUntilRecovery)
{
    StartCaptured();
    ASSERT_TRUE(SuspendMouseCaptureForDisplay());
    CaptureMouse(false); // Focus loss.
    CaptureMouse(true);  // Focus regain while drawable metrics remain invalid.
    EXPECT_EQ(captureChanges, std::vector<bool>({true, false, false}));
    EXPECT_EQ(pointerResets, 1);
    ExpectHealthyRecoveryRestoresCapture();
}

TEST(DisplayCapture, UnreachableWindowBlocksFocusRegainUntilRecovery)
{
    StartCaptured();
    ExpectSuspendedEnableIsBlocked(); // Focus regain while the window remains unreachable.
    ExpectHealthyRecoveryRestoresCapture();
}

TEST(DisplayCapture, InvalidDrawableBlocksUnpauseUntilRecovery)
{
    StartCaptured();
    ExpectSuspendedEnableIsBlocked(); // Existing menu dismissal/unpause request.
    ExpectHealthyRecoveryRestoresCapture();
}

TEST(DisplayCapture, TopologyRecoveryCannotCaptureWhileMetricsRemainInvalid)
{
    StartCaptured();
    ASSERT_TRUE(SuspendMouseCaptureForDisplay()); // Recovery attempt begins.
    EXPECT_FALSE(SuspendMouseCaptureForDisplay()); // Invalid metrics on later frames do not toggle capture.
    CaptureMouse(true);
    EXPECT_EQ(captureChanges, std::vector<bool>({true, false}));
    ExpectHealthyRecoveryRestoresCapture();
}

TEST(DisplayCapture, RecoveryHonorsInactivePausedOrNonGameState)
{
    StartCaptured();
    ASSERT_TRUE(SuspendMouseCaptureForDisplay());
    ResumeMouseCaptureAfterDisplay(false);
    EXPECT_EQ(captureChanges, std::vector<bool>({true, false, false}));
    EXPECT_EQ(pointerResets, 1);
}
