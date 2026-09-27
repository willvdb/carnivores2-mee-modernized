// test_game_mode.cpp
// Unit tests for the pure predicates in Hunt/Core/GameMode.h.
//
// These encode rules that were previously spelled out in a comment next to an
// inline comparison, so nothing stopped the two from drifting apart. The
// predicates that read engine globals (IsScopeView, InTrophyRoomMap, ...) are
// not exercised here: they need g_GameMode / ProjectName defined, and are
// covered by game runs instead.

#include <gtest/gtest.h>

#include "Core/GameMode.h"

namespace {

TEST(GameModeTest, OverlayModeCoversTheFullscreenAimingViews) {
    EXPECT_TRUE(IsOverlayMode(GameMode::OpticScope));
    EXPECT_TRUE(IsOverlayMode(GameMode::Binocular));
    EXPECT_TRUE(IsOverlayMode(GameMode::MapMode));
}

TEST(GameModeTest, OverlayModeExcludesEveryOtherState) {
    const GameMode notOverlays[] = {
        GameMode::Normal,  GameMode::Swimming,   GameMode::Underwater,
        GameMode::Flying,  GameMode::Paused,     GameMode::ExitCountdown,
        GameMode::TrophyMode, GameMode::Dead,    GameMode::Falling,
        GameMode::Crouching, GameMode::NightVision, GameMode::DogMode,
        GameMode::SurvivalMode, GameMode::ScannerMode, GameMode::SonarMode,
    };
    for (GameMode mode : notOverlays) {
        EXPECT_FALSE(IsOverlayMode(mode))
            << "unexpected overlay: " << static_cast<int>(mode);
    }
}

TEST(GameModeTest, OnlyInWorldMovementMayBeOverwrittenByWater) {
    EXPECT_TRUE(IsInWorldMovementMode(GameMode::Normal));
    EXPECT_TRUE(IsInWorldMovementMode(GameMode::Swimming));
    EXPECT_TRUE(IsInWorldMovementMode(GameMode::Crouching));  // vestigial, see header
}

TEST(GameModeTest, OverlaysThePlayerOpenedSurviveASubmersion) {
    // Stomping one of these every frame is what made the map and exit prompt
    // unreachable while the camera was under water.
    const GameMode overlays[] = {
        GameMode::MapMode,       GameMode::ExitCountdown, GameMode::Paused,
        GameMode::Binocular,     GameMode::OpticScope,    GameMode::TrophyMode,
        GameMode::NightVision,   GameMode::SonarMode,     GameMode::ScannerMode,
        GameMode::SurvivalMode,  GameMode::DogMode,
    };
    for (GameMode mode : overlays) {
        EXPECT_FALSE(IsInWorldMovementMode(mode))
            << "water would clobber mode " << static_cast<int>(mode);
    }
}

TEST(GameModeTest, TheTwoPredicatesDoNotOverlap) {
    for (int i = 0; i <= static_cast<int>(GameMode::SonarMode); ++i) {
        const GameMode mode = static_cast<GameMode>(i);
        EXPECT_FALSE(IsOverlayMode(mode) && IsInWorldMovementMode(mode))
            << "mode " << i << " is both an overlay and in-world movement";
    }
}

TEST(GameModeTest, MenuEntryStashesAndTakesOverTheCurrentMode) {
    GameMode saved = GameMode::Normal;
    EXPECT_EQ(EnterMenuState(GameMode::OpticScope, saved),
              GameMode::ExitCountdown);
    EXPECT_EQ(saved, GameMode::OpticScope);
}

TEST(GameModeTest, MenuDismissRestoresOnlyFullScreenOverlayModes) {
    EXPECT_EQ(RestoreMenuState(GameMode::OpticScope), GameMode::OpticScope);
    EXPECT_EQ(RestoreMenuState(GameMode::Binocular), GameMode::Binocular);
    EXPECT_EQ(RestoreMenuState(GameMode::MapMode), GameMode::MapMode);
    EXPECT_EQ(RestoreMenuState(GameMode::Paused), GameMode::Normal);
    EXPECT_EQ(RestoreMenuState(GameMode::Normal), GameMode::Normal);
}

}  // namespace
