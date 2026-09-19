#include "../Hunt/Platform/DisplayIdentityLinux.h"
#include "../Hunt/Game/DisplaySelection.h"
#include <gtest/gtest.h>
using namespace Platform;
namespace {
std::vector<std::uint8_t> Edid()
{
    std::vector<std::uint8_t> data(128);
    std::fill(data.begin() + 1, data.begin() + 7, 255);
    data[8] = 0x10; data[9] = 0xac; data[10] = 0x34; data[11] = 0x12;
    data[12] = 0x78; data[13] = 0x56; data[18] = 1; data[19] = 4;
    unsigned sum = 0; for (auto value : data) sum += value;
    data[127] = static_cast<std::uint8_t>(256 - sum % 256);
    return data;
}
void Checksum(std::vector<std::uint8_t>& data)
{
    for (std::size_t base = 0; base < data.size(); base += 128) {
        data[base + 127] = 0; unsigned sum = 0;
        for (std::size_t i = 0; i < 128; ++i) sum += data[base + i];
        data[base + 127] = static_cast<std::uint8_t>(256 - sum % 256);
    }
}
void SerialText(std::vector<std::uint8_t>& data, const std::string& serial)
{
    data[57] = 0xff; std::fill(data.begin() + 59, data.begin() + 72, ' ');
    std::copy(serial.begin(), serial.end(), data.begin() + 59);
    if (serial.size() < 13) data[59 + serial.size()] = 10;
    Checksum(data);
}
DisplayCatalog Catalog()
{
    Display primary, secondary;
    primary.bounds = {{0,0},{1920,1080}}; secondary.bounds = {{-1280,50},{1280,1024}};
    primary.modes = {{{800,600},32,{144,1}}}; secondary.modes = {{{800,600},32,{120,1}}};
    return {{primary, secondary}, 0};
}
}
TEST(LinuxDisplayIdentity, EdidOwnedSerialSurvivesTimingAndConnectorIndependentChanges)
{
    auto data = Edid(); const auto identity = LinuxIdentity::X11Edid(data); ASSERT_TRUE(identity);
    EXPECT_EQ(identity->domain, "linux-x11-edid-serial");
    data[32] = 12; Checksum(data); // Color/timing metadata does not identify a monitor.
    EXPECT_TRUE(EqualDisplayIdentity(*identity, *LinuxIdentity::X11Edid(data)));
    data[12]++; Checksum(data); EXPECT_FALSE(EqualDisplayIdentity(*identity, *LinuxIdentity::X11Edid(data)));
    data.clear(); EXPECT_FALSE(identity->value.empty());
}
TEST(LinuxDisplayIdentity, EdidRejectsHeaderChecksumsVersionsLengthsAndInvalidVendor)
{
    for (int scenario = 0; scenario < 9; ++scenario) {
        auto data = Edid();
        switch (scenario) {
        case 0: data.resize(127); break;
        case 1: data.push_back(0); break;
        case 2: data[0] = 1; Checksum(data); break;
        case 3: data[127]++; break;
        case 4: data[126] = 1; Checksum(data); break;
        case 5: data[18] = 2; Checksum(data); break;
        case 6: data[19] = 2; Checksum(data); break;
        case 7: data[8] = 0; data[9] = 0; Checksum(data); break;
        case 8: data[10] = 0; data[11] = 0; Checksum(data); break;
        }
        EXPECT_FALSE(LinuxIdentity::X11Edid(data)) << scenario;
    }
    auto data = Edid(); data.resize(256); data[126] = 1; Checksum(data);
    ASSERT_TRUE(LinuxIdentity::X11Edid(data)); data[200]++; EXPECT_FALSE(LinuxIdentity::X11Edid(data));
}
TEST(LinuxDisplayIdentity, MissingAndPlaceholderSerialsNeverBecomePanelIdentity)
{
    for (std::uint32_t serial : {0u,1u,0xffffffffu,0x01010101u}) {
        auto data = Edid();
        for (int i = 0; i < 4; ++i) data[12 + i] = static_cast<std::uint8_t>(serial >> (i * 8));
        Checksum(data); EXPECT_FALSE(LinuxIdentity::X11Edid(data));
        SerialText(data, "SN-12345"); EXPECT_TRUE(LinuxIdentity::X11Edid(data));
    }
    for (const char* serial : {"", "Unknown", "0", "000001", "ffffffff", "123456789", "N/A", " default", "abc "})
        EXPECT_FALSE(LinuxIdentity::WaylandSerial("Maker", "Model", serial)) << serial;
}
TEST(LinuxDisplayIdentity, TextSerialRejectsMalformedPaddingDuplicateDescriptorsAndTruncation)
{
    auto data = Edid(); SerialText(data, "ABC123"); ASSERT_TRUE(LinuxIdentity::X11Edid(data));
    auto bad = data; bad[70] = 'x'; Checksum(bad); EXPECT_FALSE(LinuxIdentity::X11Edid(bad));
    bad = data; bad[56] = 1; Checksum(bad); EXPECT_FALSE(LinuxIdentity::X11Edid(bad));
    bad = data; std::copy(data.begin()+54,data.begin()+72,bad.begin()+72); Checksum(bad); EXPECT_FALSE(LinuxIdentity::X11Edid(bad));
    bad = data; bad[60] = 0; Checksum(bad); EXPECT_FALSE(LinuxIdentity::X11Edid(bad));
    EXPECT_FALSE(LinuxIdentity::WaylandSerial("Maker", "Model", std::string(129,'s')));
    EXPECT_FALSE(LinuxIdentity::WaylandSerial("", "Model", "serial-1"));
    EXPECT_FALSE(LinuxIdentity::WaylandSerial("Maker", "unknown", "serial-1"));
}
TEST(LinuxDisplayIdentity, WaylandTupleIsLengthDelimitedCaseSensitiveAndOwned)
{
    std::string make="AB", model="C", serial="SN42";
    const auto identity = LinuxIdentity::WaylandSerial(make,model,serial); ASSERT_TRUE(identity);
    EXPECT_FALSE(EqualDisplayIdentity(*identity,*LinuxIdentity::WaylandSerial("A","BC","SN42")));
    EXPECT_FALSE(EqualDisplayIdentity(*identity,*LinuxIdentity::WaylandSerial("AB","C","sn42")));
    make.clear(); model.clear(); serial.clear();
    GameDisplay::MonitorPreference preference;
    EXPECT_TRUE(GameDisplay::ParseMonitorIdentity(GameDisplay::IdentityToken(*identity),preference));
    EXPECT_TRUE(EqualDisplayIdentity(*identity,preference.identity));
}
TEST(LinuxDisplayIdentity, ExactNativeJoinRejectsDuplicateKeysDisabledClonesAndMissingMetadata)
{
    auto catalog=Catalog(); const auto identity=LinuxIdentity::X11Edid(Edid());
    std::vector<LinuxIdentity::Output> outputs{{11,"",identity,true},{22,"",std::nullopt,true}};
    LinuxIdentity::Associate(catalog,{22,11},outputs);
    EXPECT_FALSE(catalog.displays[0].identity); ASSERT_TRUE(catalog.displays[1].identity);
    outputs.push_back({11,"",std::nullopt,true}); LinuxIdentity::Associate(catalog,{22,11},outputs);
    EXPECT_FALSE(catalog.displays[1].identity);
    outputs.pop_back(); LinuxIdentity::Associate(catalog,{11,11},outputs); EXPECT_FALSE(catalog.displays[0].identity);
    outputs[0].active=false; LinuxIdentity::Associate(catalog,{22,11},outputs); EXPECT_FALSE(catalog.displays[1].identity);
}
TEST(LinuxDisplayIdentity, DuplicateSerialOnDisabledOrUnmappedOutputDisqualifiesEnabledOne)
{
    auto catalog=Catalog(); const auto identity=LinuxIdentity::WaylandSerial("Maker","Model","SN42");
    std::vector<LinuxIdentity::Output> outputs{{11,"a",identity,true},{0,"b",identity,false}};
    LinuxIdentity::Associate(catalog,{11,22},outputs);
    EXPECT_FALSE(catalog.displays[0].identity);
    EXPECT_EQ(catalog.displays[0].identityStatus,"duplicate native monitor serial metadata");
}
TEST(LinuxDisplayIdentity, WaylandProtocolNamesJoinOnlyWithinUniqueEnabledSnapshot)
{
    std::vector<LinuxIdentity::Output> heads{{0,"DP-1",{},true},{0,"DP-2",{},true}};
    LinuxIdentity::JoinWaylandOutputs(heads,{{42,"DP-2"},{84,"DP-1"}});
    EXPECT_EQ(heads[0].key,84u); EXPECT_EQ(heads[1].key,42u);
    LinuxIdentity::JoinWaylandOutputs(heads,{{42,"DP-2"},{84,"DP-1"},{99,"DP-1"}});
    EXPECT_EQ(heads[0].key,0u);
    heads[1].name="DP-1"; LinuxIdentity::JoinWaylandOutputs(heads,{{84,"DP-1"}});
    EXPECT_EQ(heads[0].key,0u); EXPECT_EQ(heads[1].key,0u);
    heads[1].name="DP-2"; heads[0].active=false;
    LinuxIdentity::JoinWaylandOutputs(heads,{{84,"DP-1"}}); EXPECT_EQ(heads[0].key,0u);
}
TEST(LinuxDisplayIdentity, LinuxSavedIntentMovesReordersFallsBackAutomaticAndRecovers)
{
    for (const auto& identity : {*LinuxIdentity::X11Edid(Edid()),*LinuxIdentity::WaylandSerial("Maker","Model","SN42")}) {
        const GameDisplay::MonitorPreference saved{GameDisplay::MonitorPreferenceKind::Identity,0,identity};
        auto catalog=Catalog(); catalog.displays[1].identity=identity;
        std::swap(catalog.displays[0],catalog.displays[1]); catalog.primaryDisplay=1;
        catalog.displays[0].bounds->origin={3000,-700};
        auto result=GameDisplay::SelectMonitor(catalog,saved,{800,600},{120,1});
        ASSERT_TRUE(result.target); EXPECT_EQ(result.target->bounds.origin.x,3000); ASSERT_TRUE(result.exclusiveMode);
        const auto original=catalog.displays[0].identity; catalog.displays[0].identity.reset();
        result=GameDisplay::SelectMonitor(catalog,saved,{800,600},{144,1});
        EXPECT_EQ(result.index,1u); EXPECT_FALSE(result.target); EXPECT_FALSE(result.exclusiveMode);
        catalog.displays[0].identity=original; result=GameDisplay::SelectMonitor(catalog,saved,{800,600},{120,1});
        EXPECT_EQ(result.fallback,GameDisplay::DisplayFallback::None); EXPECT_TRUE(result.exclusiveMode);
        catalog.displays[0].identity->domain="win-monitor-interface";
        result=GameDisplay::SelectMonitor(catalog,saved,{800,600},{144,1}); EXPECT_FALSE(result.exclusiveMode); EXPECT_FALSE(result.target);
        EXPECT_TRUE(EqualDisplayIdentity(saved.identity,identity));
    }
}
