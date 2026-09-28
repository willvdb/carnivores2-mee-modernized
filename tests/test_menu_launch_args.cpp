// test_menu_launch_args.cpp -- production menu launch argument contracts

#include <gtest/gtest.h>

#include "../Menu/LaunchArgs.h"

namespace {

TEST(MenuLaunchArgs, BuildsRegularHuntArguments)
{
    HuntLaunchRequest request;
    request.projectName = "area1";
    request.registration = 7;
    request.dinoFlags = 5;
    request.weaponFlags = 3;
    request.timeOfDay = 0;

    std::string output;
    ASSERT_TRUE(BuildHuntLaunchArguments(request, output));
    EXPECT_EQ(output,
              " reg=7 prj=huntdat/areas/area1 din=5 wep=3 dtm=0");
}

TEST(MenuLaunchArgs, BuildsTrophyArguments)
{
    HuntLaunchRequest request;
    request.projectName = "trophy";
    request.registration = 12;
    request.timeOfDay = 1;

    std::string output;
    ASSERT_TRUE(BuildHuntLaunchArguments(request, output));
    EXPECT_EQ(output,
              " reg=12 prj=huntdat/areas/trophy din=0 wep=0 dtm=1");
}

TEST(MenuLaunchArgs, UsesResolvedExternalBasename)
{
    HuntLaunchRequest request;
    request.projectName = "area6";
    request.mapFile = "external";
    request.dinoFlags = 1;
    request.weaponFlags = 2;
    request.timeOfDay = 2;

    std::string output;
    ASSERT_TRUE(BuildHuntLaunchArguments(request, output));
    EXPECT_NE(output.find("prj=huntdat/areas/external"), std::string::npos);
    EXPECT_EQ(output.find("area6"), std::string::npos);
}

TEST(MenuLaunchArgs, LaunchParamStreamKeepsTheBaseArguments)
{
    HuntLaunchRequest request;
    request.projectName = "area3";
    request.dinoFlags = 31;
    request.weaponFlags = 57;
    request.timeOfDay = 1;

    std::string output;
    ASSERT_TRUE(BuildHuntLaunchArguments(request, output));

    std::stringstream params = MakeLaunchParamStream(output);
    params << " smod=0.85,0.7,0.8,1,1.25,1";
    params << " -borderless";

    // The full prefix must survive the appends. Constructing the stream from
    // the base string instead of writing it in leaves the put pointer at
    // position 0; the appends then overwrite the prefix and throw away the
    // project argument, which is the regression this test guards.
    EXPECT_EQ(params.str(),
              output + " smod=0.85,0.7,0.8,1,1.25,1 -borderless");
    EXPECT_NE(params.str().find("prj=huntdat/areas/area3"), std::string::npos);
}

TEST(MenuLaunchArgs, RejectsMalformedRequests)
{
    HuntLaunchRequest request;
    request.projectName = "area 1";
    request.dinoFlags = 1024;

    std::string output = "unchanged";
    EXPECT_FALSE(BuildHuntLaunchArguments(request, output));
    EXPECT_EQ(output, "unchanged");

    request.projectName = "area1";
    request.dinoFlags = 0;
    request.timeOfDay = 3;
    EXPECT_FALSE(BuildHuntLaunchArguments(request, output));
    EXPECT_EQ(output, "unchanged");
}

}  // namespace
