#include "../Hunt/Game/RefreshPreference.h"
#include "../Hunt/Game/RefreshSelection.h"
#include <gtest/gtest.h>
#include <limits>

namespace {
void ExpectRate(Platform::RefreshRate rate, std::uint32_t numerator, std::uint32_t denominator)
{
    EXPECT_EQ(rate.numerator, numerator);
    EXPECT_EQ(rate.denominator, denominator);
}
}

TEST(RefreshParser, AutomaticAndPositiveDecimalFractionsPreserveRepresentation)
{
    Platform::RefreshRate rate{144, 1};
    ASSERT_TRUE(GameDisplay::ParseRefreshRate("0", rate));
    ExpectRate(rate, 0, 0);
    for (const char* text : {"60", "120", "144", "60000/1001", "120000/1001",
                             "60000/1000", "4294967295/4294967295"}) {
        ASSERT_TRUE(GameDisplay::ParseRefreshRate(text, rate)) << text;
        EXPECT_TRUE(GameDisplay::HasRefresh(rate));
    }
    ASSERT_TRUE(GameDisplay::ParseRefreshRate("60", rate));
    ExpectRate(rate, 60, 1);
    ASSERT_TRUE(GameDisplay::ParseRefreshRate("60000/1000", rate));
    ExpectRate(rate, 60000, 1000);
}

TEST(RefreshParser, InvalidSyntaxAndOverflowClearPreviousPreference)
{
    for (const char* text : {"", "-60", "+60", "60.0", "59.94", "1e2", "0x3c", "60/0",
                             "0/1", "0/0", "00", "abc", "1/2/3", "60/", "/60", "60/-1",
                             " 60", "60 ", "60 120", "4294967296", "1/4294967296",
                             "99999999999999999999999999999999999999999999999999999999"}) {
        Platform::RefreshRate rate{120, 1};
        EXPECT_FALSE(GameDisplay::ParseRefreshRate(text, rate)) << text;
        ExpectRate(rate, 0, 0);
    }
}

TEST(RefreshParser, ConfigConsumesWholeValueAndAllowsLineWhitespaceAndComments)
{
    Platform::RefreshRate rate{};
    ASSERT_TRUE(GameDisplay::ParseConfigRefresh(" \t60000/1001  # exact fraction\r", rate));
    ExpectRate(rate, 60000, 1001);
    for (const char* text : {"", "# missing", "60 trailing", "60 / 1", "60.0 # invalid"}) {
        EXPECT_FALSE(GameDisplay::ParseConfigRefresh(text, rate));
        ExpectRate(rate, 0, 0);
    }
    const std::string longValue = std::string(70, '0') + "60/0";
    EXPECT_FALSE(GameDisplay::ParseConfigRefresh(longValue, rate));
}

TEST(RefreshPreference, EarlyCommandLineConfigFinalCommandLineAreIdempotent)
{
    using GameDisplay::RefreshArgument;
    Platform::RefreshRate rate{};
    ExpectRate(rate, 0, 0);
    EXPECT_EQ(GameDisplay::ApplyRefreshArgument("-refresh=120", rate), RefreshArgument::Applied);
    EXPECT_TRUE(GameDisplay::ParseConfigRefresh("60000/1001", rate));
    ExpectRate(rate, 60000, 1001);
    for (int pass = 0; pass < 2; ++pass) {
        EXPECT_EQ(GameDisplay::ApplyRefreshArgument("-refresh=120", rate), RefreshArgument::Applied);
        ExpectRate(rate, 120, 1);
    }
    EXPECT_EQ(GameDisplay::ApplyRefreshArgument("/refresh=60000/1001", rate), RefreshArgument::Applied);
    ExpectRate(rate, 60000, 1001);
    EXPECT_EQ(GameDisplay::ApplyRefreshArgument("-refresh=0", rate), RefreshArgument::Applied);
    ExpectRate(rate, 0, 0);
    EXPECT_TRUE(GameDisplay::ParseConfigRefresh("144", rate));
    EXPECT_EQ(GameDisplay::ApplyRefreshArgument("/REFRESH=60/0", rate), RefreshArgument::Invalid);
    ExpectRate(rate, 0, 0);
}

TEST(RefreshPreference, UnrelatedArgumentsDoNotTouchPreference)
{
    Platform::RefreshRate rate{60, 1};
    for (const char* arg : {"-res=800x600", "/vmode5", "reg=0", "prj=HUNTDAT", "-fullscreen",
                            "-refreshing=144", "x=60", "", "-r"}) {
        EXPECT_EQ(GameDisplay::ApplyRefreshArgument(arg, rate), GameDisplay::RefreshArgument::Unrelated);
        ExpectRate(rate, 60, 1);
    }
}

TEST(RefreshComparison, ExactValuesWithoutNormalizationOrFloatingPoint)
{
    EXPECT_TRUE(GameDisplay::EqualRefresh({60, 1}, {60000, 1000}));
    EXPECT_TRUE(GameDisplay::EqualRefresh({60, 1}, {120000, 2000}));
    EXPECT_TRUE(GameDisplay::EqualRefresh({120, 2}, {60, 1}));
    EXPECT_FALSE(GameDisplay::EqualRefresh({60000, 1001}, {60, 1}));
    EXPECT_FALSE(GameDisplay::EqualRefresh({}, {60, 1}));
    EXPECT_FALSE(GameDisplay::EqualRefresh({60, 0}, {60, 1}));
    EXPECT_FALSE(GameDisplay::EqualRefresh({}, {}));
    constexpr auto max = (std::numeric_limits<std::uint32_t>::max)();
    EXPECT_TRUE(GameDisplay::EqualRefresh({max, max}, {max - 1, max - 1}));
    EXPECT_FALSE(GameDisplay::EqualRefresh({max, max - 1}, {max - 1, max - 2}));
}

TEST(RefreshPolicy, FirstExactDimensionsDepthAndRateInBackendOrder)
{
    const std::vector<Platform::DisplayMode> modes{
        {{800, 600}, 8, {60, 1}}, {{1024, 600}, 32, {60, 1}}, {{800, 768}, 32, {60, 1}},
        {{800, 600}, 16, {60000, 1000}}, {{800, 600}, 32, {60, 1}},
        {{800, 600}, 32, {60000, 1001}}, {{800, 600}, 32, {120, 1}}, {{800, 600}, 32, {}}};
    EXPECT_EQ(GameDisplay::FindRefreshMode(modes, {800, 600}, {60, 1}), 3u);
    EXPECT_EQ(GameDisplay::FindRefreshMode(modes, {800, 600}, {120000, 2000}), 3u);
    EXPECT_EQ(GameDisplay::FindRefreshMode(modes, {800, 600}, {60000, 1001}), 5u);
    EXPECT_EQ(GameDisplay::FindRefreshMode(modes, {800, 600}, {120, 1}), 6u);
    EXPECT_FALSE(GameDisplay::FindRefreshMode(modes, {800, 600}, {5994, 100}));
    EXPECT_FALSE(GameDisplay::FindRefreshMode(modes, {800, 600}, {144, 1}));
    EXPECT_FALSE(GameDisplay::FindRefreshMode(modes, {640, 480}, {60, 1}));
    EXPECT_FALSE(GameDisplay::FindRefreshMode(modes, {800, 600}, {}));
    EXPECT_FALSE(GameDisplay::FindRefreshMode({}, {800, 600}, {60, 1}));
}

TEST(RefreshPolicy, IntegerHzConversionNeverRoundsFractionalRates)
{
    EXPECT_EQ(GameDisplay::IntegerRefreshHz({60, 1}), 60u);
    EXPECT_EQ(GameDisplay::IntegerRefreshHz({60000, 1000}), 60u);
    EXPECT_EQ(GameDisplay::IntegerRefreshHz({120, 2}), 60u);
    EXPECT_EQ(GameDisplay::IntegerRefreshHz({1, 1}), 1u);
    EXPECT_EQ(GameDisplay::IntegerRefreshHz({4294967295u, 1}), 4294967295u);
    EXPECT_FALSE(GameDisplay::IntegerRefreshHz({60000, 1001}));
    EXPECT_FALSE(GameDisplay::IntegerRefreshHz({1, 2}));
    EXPECT_FALSE(GameDisplay::IntegerRefreshHz({0, 1}));
    EXPECT_FALSE(GameDisplay::IntegerRefreshHz({60, 0}));
    EXPECT_FALSE(GameDisplay::IntegerRefreshHz({}));
}
