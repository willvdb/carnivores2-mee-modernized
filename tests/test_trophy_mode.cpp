#include <gtest/gtest.h>
#include "GameMode.h"
#include <cstring>

GameMode g_GameMode = GameMode::Normal;
char ProjectName[128] = {};

TEST(TrophyRoom, SessionSurvivesMovementAndOverlayModes)
{
    std::strcpy(ProjectName, "huntdat/areas/trophy");
    for (GameMode mode : {GameMode::Normal, GameMode::Swimming,
                         GameMode::Binocular, GameMode::MapMode,
                         GameMode::Paused, GameMode::ExitCountdown}) {
        g_GameMode = mode;
        EXPECT_TRUE(InTrophyRoom());
        EXPECT_FALSE(CanUseMap());
    }
    g_GameMode = GameMode::Normal;
    ProjectName[0] = 0;
}

TEST(TrophyRoom, NormalHuntsDoNotEnableRoomInteractions)
{
    std::strcpy(ProjectName, "huntdat/areas/area1");
    g_GameMode = GameMode::Normal;
    EXPECT_FALSE(InTrophyRoom());
    EXPECT_TRUE(CanUseMap());
    g_GameMode = GameMode::TrophyMode;
    EXPECT_TRUE(InTrophyRoom()); // retain explicit legacy mode support
    EXPECT_FALSE(CanUseMap());
    g_GameMode = GameMode::Normal;
    ProjectName[0] = 0;
}
