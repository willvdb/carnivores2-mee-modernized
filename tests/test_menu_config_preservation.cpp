#include "temp_path.h"
#include <gtest/gtest.h>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {
struct { int RenderAPI=1, FOV=62, ObjectDetail=48, OptFpsLimit=1, VerboseLogging=0,
             NightVisionKey=78, Resolution=0, DisplayMode=2; } g_Options;
struct { int w=800, h=600; } g_ResolutionList[1];
int g_ResCount=1;
bool g_HasSavedHunt=false;
std::string g_SavedHuntArea;
int g_SavedHuntDinos=0, g_SavedHuntWeapons=0, g_SavedHuntUtils=0, g_SavedHuntTime=0;
std::string path;
std::string GetConfigWritePath() { return path; }
#include "menu_save_config.inc"
}

TEST(MenuConfigPreservation, ModernIdentityRefreshUnknownKeysAndCommentsSurviveRepeatedRewrites)
{
    path = TestTempPath("carnivores-menu-config-");
    const std::string retained =
        "# user display preference\n"
        "display_identity v1:win-monitor-interface:00610062 # registered path\n"
        "display_identity v1:linux-x11-edid-serial:10ac3412012a5600000000 # native EDID\n"
        "display_identity v1:linux-wayland-wlr-serial:00014100014200025332 # compositor metadata\n"
        "refresh_rate 60000/1001 # exact\n"
        "future_key preserve me\n"
        "display_identity v2:future-domain:abcd # unknown version remains\n";
    { std::ofstream file(path); file << retained << "display_mode 0\n"; }
    for (int pass = 0; pass < 2; ++pass) {
        g_Options.DisplayMode = pass + 1;
        SaveConfig();
        std::ifstream input(path);
        std::string text{std::istreambuf_iterator<char>(input), {}};
        EXPECT_EQ(text.substr(0, retained.size()), retained);
        EXPECT_NE(text.find("display_mode " + std::to_string(pass + 1) + "\n"), std::string::npos);
    }
    std::filesystem::remove(path);
}
