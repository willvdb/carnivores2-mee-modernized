#include "../Hunt/Game/DisplaySelection.h"
#include "../Hunt/Platform/DisplayIdentityMapping.h"
#include <gtest/gtest.h>
#include <algorithm>
#ifdef _WIN32
#include "../Hunt/Platform/DisplayIdentityWin32.h"
#endif

namespace {
Platform::DisplayIdentity Id(const char* value = "00610062") { return {1, "win-monitor-interface", value}; }
GameDisplay::MonitorPreference Saved() { return {GameDisplay::MonitorPreferenceKind::Identity, 0, Id()}; }
Platform::DisplayCatalog Catalog()
{
    Platform::Display primary, secondary;
    primary.bounds = {{0, 0}, {1920, 1080}};
    primary.identity = Id("00630064");
    secondary.bounds = {{-1280, 50}, {1280, 1024}};
    secondary.identity = Id();
    primary.modes = {{{800, 600}, 32, {144, 1}}};
    secondary.modes = {{{800, 600}, 32, {120, 1}}};
    return {{primary, secondary}, 0};
}
}

TEST(DisplayPersistence, VersionedOwnedTokenRoundTripAndConfigWhitespace)
{
    auto preference = Saved();
    auto token = GameDisplay::IdentityToken(preference.identity);
    ASSERT_EQ(token, "v1:win-monitor-interface:00610062");
    ASSERT_TRUE(GameDisplay::ParseConfigMonitor(" \t" + token + " \r # retain\n", preference));
    EXPECT_TRUE(Platform::EqualDisplayIdentity(preference.identity, Id()));
    token.clear();
    EXPECT_EQ(preference.identity.value, "00610062");
    ASSERT_TRUE(GameDisplay::ParseConfigMonitor(" primary # default", preference));
    EXPECT_EQ(preference.kind, GameDisplay::MonitorPreferenceKind::Primary);
}

TEST(DisplayPersistence, RejectsTruncationOverflowExtraTokensAndRuntimeIndices)
{
    for (const std::string text : {"", "0", "1", "v0:win-monitor-interface:00", "v4294967296:x:00",
         "v1::00", "v1:x:", "v1:x:0", "v1:x:GG", "v1:x:00 extra", "v1:x:00:00", "v1:x:00\n", "v1:X:00"}) {
        auto preference = Saved();
        EXPECT_FALSE(GameDisplay::ParseMonitorIdentity(text, preference)) << text;
        EXPECT_EQ(preference.kind, GameDisplay::MonitorPreferenceKind::Primary);
    }
    auto preference = Saved();
    EXPECT_FALSE(GameDisplay::ParseMonitorIdentity("v1:x:" + std::string(2050, '0'), preference));
    EXPECT_FALSE(GameDisplay::ParseMonitorIdentity("v1:" + std::string(49, 'x') + ":00", preference));
    EXPECT_TRUE(GameDisplay::ParseMonitorIdentity("v1:x:" + std::string(2048, '0'), preference));
}

TEST(DisplayPersistence, UnknownDomainAndVersionAreRetainedButNeverGuessed)
{
    for (const auto* token : {"v2:win-monitor-interface:00610062", "v1:another-os:00610062"}) {
        GameDisplay::MonitorPreference preference;
        ASSERT_TRUE(GameDisplay::ParseMonitorIdentity(token, preference));
        const auto selected = GameDisplay::SelectMonitor(Catalog(), preference, {800, 600}, {144, 1});
        EXPECT_EQ(selected.fallback, GameDisplay::DisplayFallback::UnsupportedIdentity);
        EXPECT_EQ(selected.index, 0u);
        EXPECT_FALSE(selected.target);
        EXPECT_FALSE(selected.exclusiveMode);
        EXPECT_EQ(GameDisplay::IdentityToken(preference.identity), token);
    }
}

TEST(DisplayPersistence, ConfigThenFinalCommandLinePrecedenceAndLastOccurrence)
{
    using GameDisplay::DisplayArgument;
    auto preference = Saved();
    ASSERT_EQ(GameDisplay::ApplyMonitorArgument("-display=0", preference), DisplayArgument::Applied);
    ASSERT_TRUE(GameDisplay::ParseConfigMonitor("v1:win-monitor-interface:00610062", preference));
    ASSERT_EQ(GameDisplay::ApplyMonitorArgument("/DISPLAY=0", preference), DisplayArgument::Applied);
    EXPECT_EQ(preference.kind, GameDisplay::MonitorPreferenceKind::SessionIndex);
    EXPECT_EQ(preference.index, 0u);
    ASSERT_EQ(GameDisplay::ApplyMonitorArgument("-display-id=v1:win-monitor-interface:00610062", preference), DisplayArgument::Applied);
    EXPECT_EQ(preference.kind, GameDisplay::MonitorPreferenceKind::Identity);
    ASSERT_EQ(GameDisplay::ApplyMonitorArgument("/display=PRIMARY", preference), DisplayArgument::Applied);
    EXPECT_EQ(preference.kind, GameDisplay::MonitorPreferenceKind::Primary);
    EXPECT_EQ(GameDisplay::ApplyMonitorArgument("-windowed", preference), DisplayArgument::Unrelated);
}

TEST(DisplayPersistence, MalformedSessionOptionsConsumeAndClearSavedPreference)
{
    for (const auto* argument : {"-display=", "/display=x=40", "-display-id=", "-display-id=v1:x:00-debug", "/DISPLAY-ID=y=3"}) {
        auto preference = Saved();
        ASSERT_EQ(GameDisplay::ApplyMonitorArgument(argument, preference), GameDisplay::DisplayArgument::Invalid);
        EXPECT_EQ(preference.kind, GameDisplay::MonitorPreferenceKind::Primary);
    }
    EXPECT_TRUE(GameDisplay::WantsDisplayList("/LIST-DISPLAYS"));
    EXPECT_FALSE(GameDisplay::WantsDisplayList("-list-displays=x=2"));
}

TEST(DisplayPersistence, ReorderingAndMovingPreserveIdentityAndTargetSpecificRefresh)
{
    auto catalog = Catalog();
    std::swap(catalog.displays[0], catalog.displays[1]);
    catalog.primaryDisplay = 1;
    catalog.displays[0].bounds->origin = {3000, -700};
    const auto selected = GameDisplay::SelectMonitor(catalog, Saved(), {800, 600}, {120, 1});
    ASSERT_EQ(selected.index, 0u);
    ASSERT_TRUE(selected.target);
    EXPECT_EQ(selected.target->bounds.origin.x, 3000);
    ASSERT_TRUE(selected.exclusiveMode);
    EXPECT_TRUE(Platform::EqualRefresh(selected.exclusiveMode->refresh, {120, 1}));
    const auto unavailable = GameDisplay::SelectMonitor(catalog, Saved(), {800, 600}, {144, 1});
    EXPECT_TRUE(unavailable.target);
    EXPECT_FALSE(unavailable.exclusiveMode);
}

TEST(DisplayPersistence, MissingOrChangedDeviceReturnsPrimaryAutomaticThenRecovers)
{
    auto catalog = Catalog();
    auto preference = Saved();
    const auto original = catalog.displays[1];
    catalog.displays.pop_back();
    auto selected = GameDisplay::SelectMonitor(catalog, preference, {800, 600}, {144, 1});
    EXPECT_EQ(selected.fallback, GameDisplay::DisplayFallback::MissingIdentity);
    EXPECT_EQ(selected.index, 0u);
    EXPECT_FALSE(selected.target);
    EXPECT_FALSE(selected.exclusiveMode); // Primary supports 144: must still clear it.
    catalog.displays.push_back(original);
    selected = GameDisplay::SelectMonitor(catalog, preference, {800, 600}, {120, 1});
    EXPECT_EQ(selected.fallback, GameDisplay::DisplayFallback::None);
    EXPECT_TRUE(selected.target);
    EXPECT_TRUE(selected.exclusiveMode);
    EXPECT_TRUE(Platform::EqualDisplayIdentity(preference.identity, Id()));
}

TEST(DisplayPersistence, DuplicatesNeverChooseByEnumerationOrder)
{
    auto catalog = Catalog();
    catalog.displays[0].identity = Id(); // Same identity, DIFFERENT bounds.
    for (int i = 0; i < 2; ++i) {
        const auto selected = GameDisplay::SelectMonitor(catalog, Saved(), {800, 600}, {144, 1});
        EXPECT_EQ(selected.fallback, GameDisplay::DisplayFallback::AmbiguousIdentity);
        EXPECT_FALSE(selected.target);
        EXPECT_FALSE(selected.exclusiveMode);
        std::reverse(catalog.displays.begin(), catalog.displays.end());
    }
}

TEST(DisplayPersistence, MissingOrAmbiguousBoundsCannotCarryRefreshToPrimary)
{
    for (bool missing : {true, false}) {
        auto catalog = Catalog();
        if (missing) catalog.displays[1].bounds.reset();
        else catalog.displays[1].bounds = catalog.displays[0].bounds;
        const auto selected = GameDisplay::SelectMonitor(catalog, Saved(), {800, 600}, {144, 1});
        EXPECT_EQ(selected.index, 0u);
        EXPECT_FALSE(selected.target);
        EXPECT_FALSE(selected.exclusiveMode);
    }
}

TEST(DisplayPersistence, UnsupportedDiscoveryAndNoPrimaryUseBackendDefaultAutomatic)
{
    auto catalog = Catalog();
    for (auto& display : catalog.displays) display.identity.reset();
    catalog.primaryDisplay.reset();
    const auto selected = GameDisplay::SelectMonitor(catalog, Saved(), {800, 600}, {144, 1});
    EXPECT_FALSE(selected.index);
    EXPECT_FALSE(selected.target);
    EXPECT_FALSE(selected.exclusiveMode);
}

TEST(DisplayPersistence, DefaultAndSessionRequestsPreserveEarlierPolicy)
{
    auto catalog = Catalog();
    GameDisplay::MonitorPreference preference;
    auto selected = GameDisplay::SelectMonitor(catalog, preference, {800, 600}, {144, 1});
    EXPECT_FALSE(selected.target);
    EXPECT_TRUE(selected.exclusiveMode);
    preference = {GameDisplay::MonitorPreferenceKind::SessionIndex, 900, {}};
    selected = GameDisplay::SelectMonitor(catalog, preference, {800, 600}, {144, 1});
    EXPECT_EQ(selected.fallback, GameDisplay::DisplayFallback::IndexOutOfRange);
    EXPECT_TRUE(selected.exclusiveMode); // Existing 4d out-of-range semantics.
}

TEST(DisplayIdentityAssociation, RequiresUniqueFullRectangleOnBothSides)
{
    auto catalog = Catalog();
    using Native = Platform::DisplayIdentityDetails::NativeIdentity;
    std::vector<Native> native{{*catalog.displays[0].bounds, Id("aa")}, {*catalog.displays[1].bounds, Id()}};
    Platform::DisplayIdentityDetails::Associate(catalog, native);
    ASSERT_TRUE(catalog.displays[1].identity);
    native.push_back(native[1]);
    native[2].identity.reset(); // Even an unidentified duplicate disqualifies it.
    Platform::DisplayIdentityDetails::Associate(catalog, native);
    EXPECT_FALSE(catalog.displays[1].identity);
    native.pop_back();
    catalog.displays.push_back(catalog.displays[1]);
    Platform::DisplayIdentityDetails::Associate(catalog, native);
    EXPECT_FALSE(catalog.displays[1].identity);
    EXPECT_FALSE(catalog.displays[2].identity);
    EXPECT_TRUE(catalog.displays[0].identity);
}

TEST(DisplayIdentityAssociation, OwnedSnapshotSurvivesDiscoveryAndModeChanges)
{
    auto catalog = Catalog();
    {
        std::vector<Platform::DisplayIdentityDetails::NativeIdentity> native{{*catalog.displays[1].bounds, Id()}};
        Platform::DisplayIdentityDetails::Associate(catalog, native);
    }
    ASSERT_TRUE(catalog.displays[1].identity);
    EXPECT_TRUE(Platform::EqualDisplayIdentity(*catalog.displays[1].identity, Id()));
    std::vector<Platform::DisplayIdentityDetails::NativeIdentity> moved{{{{-1280, 51}, {1280, 1024}}, Id()}};
    Platform::DisplayIdentityDetails::Associate(catalog, moved);
    EXPECT_FALSE(catalog.displays[1].identity);
}

#ifdef _WIN32
TEST(WindowsDisplayIdentity, RegistrationNotLabelIndexOrInactiveChild)
{
    auto enumerate = [](const wchar_t* source, DWORD index, DISPLAY_DEVICEW* device, DWORD flags) -> BOOL {
        EXPECT_STREQ(source, L"source");
        EXPECT_EQ(flags, DWORD{EDD_GET_DEVICE_INTERFACE_NAME});
        EXPECT_EQ(device->cb, sizeof(*device));
        if (index >= 2) return FALSE;
        device->StateFlags = index == 1 ? DISPLAY_DEVICE_ACTIVE : 0;
        wcscpy_s(device->DeviceID, L"Ab");
        return TRUE;
    };
    const auto identity = Platform::Win32Details::ReadMonitorIdentity(L"source", enumerate);
    ASSERT_TRUE(identity);
    EXPECT_TRUE(Platform::EqualDisplayIdentity(*identity, Id()));
}
TEST(WindowsDisplayIdentity, CloneMissingAndTruncatedRegistrationRemainUnavailable)
{
    for (int scenario = 0; scenario < 5; ++scenario) {
        const auto enumerate = [scenario](const wchar_t*, DWORD index, DISPLAY_DEVICEW* device, DWORD) -> BOOL {
            if (index >= (scenario == 0 ? 2u : 1u)) return FALSE;
            device->StateFlags = DISPLAY_DEVICE_ACTIVE;
            if (scenario == 0) wcscpy_s(device->DeviceID, L"Ab");
            if (scenario == 2) std::fill(std::begin(device->DeviceID), std::end(device->DeviceID), L'a');
            if (scenario == 3) device->StateFlags = 0;
            if (scenario == 4) std::fill(device->DeviceID, device->DeviceID + 127, L'a');
            return TRUE;
        };
        EXPECT_FALSE(Platform::Win32Details::ReadMonitorIdentity(L"source", enumerate));
    }
}
TEST(WindowsDisplayIdentity, MissingBoundsCannotProducePartialIdentityEntry)
{
    bool enumerated = false;
    const auto failedInfo = [](HMONITOR, MONITORINFO*) -> BOOL { return FALSE; };
    const auto enumerate = [&](const wchar_t*, DWORD, DISPLAY_DEVICEW*, DWORD) -> BOOL {
        enumerated = true;
        return FALSE;
    };
    EXPECT_FALSE(Platform::Win32Details::ReadMonitorIdentityEntry(nullptr, failedInfo, enumerate));
    EXPECT_FALSE(enumerated);
}
TEST(WindowsDisplayIdentity, BoundsAndRegistrationAreCopiedTogether)
{
    const auto info = [](HMONITOR, MONITORINFO* output) -> BOOL {
        EXPECT_EQ(output->cbSize, sizeof(MONITORINFOEXW));
        auto* extended = reinterpret_cast<MONITORINFOEXW*>(output);
        extended->rcMonitor = {-1280, -50, 0, 974};
        wcscpy_s(extended->szDevice, L"source");
        return TRUE;
    };
    const auto enumerate = [](const wchar_t* source, DWORD index, DISPLAY_DEVICEW* device, DWORD) -> BOOL {
        EXPECT_STREQ(source, L"source");
        if (index) return FALSE;
        device->StateFlags = DISPLAY_DEVICE_ACTIVE;
        wcscpy_s(device->DeviceID, L"Ab");
        return TRUE;
    };
    const auto entry = Platform::Win32Details::ReadMonitorIdentityEntry(nullptr, info, enumerate);
    ASSERT_TRUE(entry);
    EXPECT_TRUE(Platform::EqualDisplayBounds(entry->bounds, {{-1280, -50}, {1280, 1024}}));
    ASSERT_TRUE(entry->identity);
    EXPECT_TRUE(Platform::EqualDisplayIdentity(*entry->identity, Id()));
}
#endif
