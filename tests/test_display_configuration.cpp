#include "../Hunt/Game/DisplayConfiguration.h"
#include "../Hunt/Game/DisplaySelection.h"
#include "../Hunt/Game/RefreshPreference.h"
#include <gtest/gtest.h>

namespace {
constexpr Platform::Size modes[] = {{640, 480}, {800, 600}, {1920, 1080}};
}
TEST(DisplayConfiguration, DefaultMatchesUnspecifiedPreProfileLegacyStartup)
{
    const GameDisplay::Configuration config;
    const auto legacy = GameDisplay::ProjectLegacyPresentation(config);
    EXPECT_EQ(legacy.size.width, 0);
    EXPECT_EQ(legacy.size.height, 0);
    EXPECT_EQ(legacy.fullscreen, 1);
    EXPECT_EQ(legacy.borderless, 0);
    EXPECT_EQ(config.monitor.kind, GameDisplay::MonitorPreferenceKind::Primary);
    EXPECT_FALSE(Platform::HasRefresh(config.refresh));
}
TEST(DisplayConfiguration, ProfileAdapterOnlyChangesDimensionsAndOrdinal)
{
    GameDisplay::Configuration config;
    config.mode = Platform::WindowMode::Windowed;
    config.refresh = {60000, 1001};
    ASSERT_EQ(GameDisplay::ApplyMonitorArgument("-display=3", config.monitor), GameDisplay::DisplayArgument::Applied);
    std::int32_t ordinal = -4;
    GameDisplay::ApplyLegacyProfileResolution(config, modes, 3, ordinal);
    EXPECT_EQ(ordinal, 1);
    EXPECT_EQ(config.size.width, 800);
    EXPECT_EQ(config.mode, Platform::WindowMode::Windowed);
    EXPECT_EQ(config.monitor.index, 3u);
    EXPECT_EQ(config.refresh.denominator, 1001u);
    ordinal = 99;
    GameDisplay::ApplyLegacyProfileResolution(config, nullptr, 0, ordinal);
    EXPECT_EQ(ordinal, 99);
    EXPECT_EQ(config.size.height, 600);
}
TEST(DisplayConfiguration, ConfigUnmatchedAndCommandLineUnmatchedRetainDifferentOrdinals)
{
    GameDisplay::Configuration config;
    std::int32_t ordinal = 2;
    int current = 2;
    GameDisplay::ApplyConfigResolution(config, {1111, 777}, modes, 3, ordinal);
    EXPECT_EQ(ordinal, -1);
    EXPECT_EQ(current, 2);
    EXPECT_EQ(config.size.width, 1111);
    GameDisplay::ApplyCommandLineResolution(config, {1234, 888}, modes, 3, ordinal, current);
    EXPECT_EQ(ordinal, -1);
    EXPECT_EQ(current, 2);
    EXPECT_EQ(config.size.width, 1234);
    GameDisplay::ApplyCommandLineResolution(config, {640, 480}, modes, 3, ordinal, current);
    EXPECT_EQ(ordinal, 0);
    EXPECT_EQ(current, 0);
    GameDisplay::ApplyConfigResolution(config, {1920, 1080}, modes, 3, ordinal);
    EXPECT_EQ(ordinal, 2);
    EXPECT_EQ(current, 0);
}
TEST(DisplayConfiguration, EarlyCliProfileConfigAndFinalCliHaveEstablishedPrecedence)
{
    GameDisplay::Configuration config;
    std::int32_t ordinal = 0;
    int current = 7;
    const auto cli = [&] {
        config.mode = Platform::WindowMode::Windowed;
        GameDisplay::ApplyCommandLineResolution(config, {1234, 700}, modes, 3, ordinal, current);
        GameDisplay::ApplyMonitorArgument("-display=primary", config.monitor);
        GameDisplay::ApplyRefreshArgument("-refresh=0", config.refresh);
    };
    cli();
    ordinal = 2;
    GameDisplay::ApplyLegacyProfileResolution(config, modes, 3, ordinal);
    EXPECT_EQ(config.size.width, 1920);
    GameDisplay::ApplyConfigResolution(config, {800, 600}, modes, 3, ordinal);
    ASSERT_TRUE(GameDisplay::ApplyConfigWindowMode(config, 2));
    ASSERT_TRUE(GameDisplay::ParseConfigMonitor("v1:win-monitor-interface:0061", config.monitor));
    ASSERT_TRUE(GameDisplay::ParseConfigRefresh("60000/1001", config.refresh));
    cli();
    EXPECT_EQ(config.size.width, 1234);
    EXPECT_EQ(config.size.height, 700);
    EXPECT_EQ(ordinal, 1);
    EXPECT_EQ(current, 7);
    EXPECT_EQ(config.mode, Platform::WindowMode::Windowed);
    EXPECT_EQ(config.monitor.kind, GameDisplay::MonitorPreferenceKind::Primary);
    EXPECT_FALSE(Platform::HasRefresh(config.refresh));
}
TEST(DisplayConfiguration, WindowModeProjectionAndSoftwareConfigCompatibility)
{
    GameDisplay::Configuration config;
    for (const int value : {0, 1, 2}) {
        ASSERT_TRUE(GameDisplay::ApplyConfigWindowMode(config, value));
        const auto legacy = GameDisplay::ProjectLegacyPresentation(config);
        EXPECT_EQ(legacy.fullscreen, value == 1);
        EXPECT_EQ(legacy.borderless, value == 2);
    }
    EXPECT_FALSE(GameDisplay::ApplyConfigWindowMode(config, -1));
    EXPECT_FALSE(GameDisplay::ApplyConfigWindowMode(config, 3));
    EXPECT_EQ(config.mode, Platform::WindowMode::Borderless);
    ASSERT_TRUE(GameDisplay::ApplyConfigWindowMode(config, 2, true));
    EXPECT_EQ(config.mode, Platform::WindowMode::Exclusive);
}
TEST(DisplayConfiguration, AltEnterUsesActualSizeAndRetainsMonitorAndRefreshIntent)
{
    GameDisplay::Configuration config;
    config.size = {1920, 1080};
    config.mode = Platform::WindowMode::Borderless;
    ASSERT_TRUE(GameDisplay::ParseMonitorIdentity("v1:win-monitor-interface:0061", config.monitor));
    config.refresh = {60000, 1001};
    GameDisplay::ApplyClientSize(config, {1600, 900});
    GameDisplay::ApplyClientSize(config, {0, 0});
    GameDisplay::ApplyClientSize(config, {-1, 900});
    GameDisplay::ToggleWindowMode(config);
    EXPECT_EQ(config.mode, Platform::WindowMode::Exclusive);
    GameDisplay::ToggleWindowMode(config);
    EXPECT_EQ(config.mode, Platform::WindowMode::Windowed);
    GameDisplay::ToggleWindowMode(config);
    EXPECT_EQ(config.mode, Platform::WindowMode::Exclusive);
    EXPECT_EQ(config.size.width, 1600);
    EXPECT_EQ(config.size.height, 900);
    EXPECT_EQ(config.monitor.identity.value, "0061");
    EXPECT_EQ(config.refresh.denominator, 1001u);
}
TEST(DisplayConfiguration, FailedSavedTargetDoesNotRewriteDimensionsOrSavedIntent)
{
    GameDisplay::Configuration config;
    config.size = {1111, 777};
    config.refresh = {144, 1};
    ASSERT_TRUE(GameDisplay::ParseMonitorIdentity("v1:win-monitor-interface:0061", config.monitor));
    Platform::Display primary;
    primary.bounds = {{0, 0}, {1920, 1080}};
    primary.modes = {{{1111, 777}, 32, {144, 1}}};
    const auto selected = GameDisplay::SelectMonitor({{primary}, 0}, config.monitor, config.size, config.refresh);
    EXPECT_EQ(selected.index, 0u);
    EXPECT_FALSE(selected.target);
    EXPECT_FALSE(selected.exclusiveMode);
    EXPECT_EQ(config.size.width, 1111);
    EXPECT_EQ(config.monitor.identity.value, "0061");
    EXPECT_EQ(config.refresh.numerator, 144u);
}
