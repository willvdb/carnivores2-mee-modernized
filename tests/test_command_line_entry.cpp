// test_command_line_entry.cpp -- production ProcessCommandLine contracts

#include <gtest/gtest.h>

#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

#include "Hunt.h"
#include "Platform/System.h"
#include "Session/Session.h"
#ifdef _WIN32
#include "Network/NetworkManager.h"
NetworkManager g_Network;
#endif

namespace {
#ifdef _WIN32
constexpr size_t kServerDiagnosticCount = 1;
#else
constexpr size_t kServerDiagnosticCount = 0;
#endif


std::vector<std::string> g_logMessages;

class CommandLineArguments final
{
public:
    explicit CommandLineArguments(std::initializer_list<std::string> values)
    {
        for (const std::string& value : values)
            m_values.emplace_back(value);
        for (std::string& value : m_values)
            m_argv.push_back(value.data());
    }

    void Install()
    {
        Platform::SetArguments(m_values);
    }

private:
    std::vector<std::string> m_values;
    std::vector<char*> m_argv;
};

class RestoreProcessArguments final {
    std::vector<std::string> saved = Platform::Arguments();
public:
    ~RestoreProcessArguments() { Platform::SetArguments(saved); }
};

void ResetCommandLineState()
{
    std::strcpy(ProjectName, "old-project");
#ifdef _WIN32
    std::strcpy(g_Network.m_serverAddress, "old-server");
#endif
    TargetDino = 0;
    WeaponPres = 0;
    OptDayNight = 0;
    PlayerX = 0.0f;
    PlayerZ = 0.0f;
    LockLanding = false;
    DisplayConfiguration = {};
    DisplayConfiguration.size = {640, 480};
    FULLSCREEN = true;
    BORDERLESS = true;
    WinW = 640;
    WinH = 480;
    ResCount = 1;
    ResolutionList[0].width = 800;
    ResolutionList[0].height = 600;
    CurRes = -1;
    OptRes = -1;
    g_GameMode = GameMode::Normal;
    RadarMode = false;
    NightVisionMode = false;
    ScoreMod_Camo = 0.0f;
    ScoreMod_Radar = 0.0f;
    ScoreMod_Scent = 0.0f;
    ScoreMod_Double = 0.0f;
    ScoreMod_Tranq = 0.0f;
    ScoreMod_Observer = 0.0f;
    g_logMessages.clear();
}

}  // namespace

void PrintLog(const char* message)
{
    g_logMessages.emplace_back(message ? message : "");
}

void PrintLogVerbose(const char* message)
{
    PrintLog(message);
}

std::int32_t _HeapFree(Platform::HeapHandle heap, std::uint32_t flags, void* memory) {
    return Platform::FreeHeap(heap, flags, memory);
}
[[noreturn]] void DoHalt(const char* message) { throw std::runtime_error(message); }
[[noreturn]] void DoHalt2(const char* message) { DoHalt(message); }
// Display synchronization itself is covered by the platform suite.
void SyncLegacyDisplayState() {
    WinW = DisplayConfiguration.size.width;
    WinH = DisplayConfiguration.size.height;
    FULLSCREEN = DisplayConfiguration.mode == Platform::WindowMode::Exclusive;
    BORDERLESS = DisplayConfiguration.mode == Platform::WindowMode::Borderless;
}

TEST(CommandLineEntry, ProductionPathAppliesMenuArguments)
{
    RestoreProcessArguments restoreArguments;
    ResetCommandLineState();
    CommandLineArguments arguments{
        "Carnivores1",
        "prj=huntdat/areas/area1",
#ifdef _WIN32
        "server=localhost",
#endif
        "din=5",
        "wep=3",
        "dtm=2",
        "x=4.5",
        "y=6.5",
        "/res=800x600",
        "/windowed",
        "-nightvision",
        "-radar",
        "smod=1.1,1.2,1.3,1.4,1.5,1.6"};
    arguments.Install();

    ProcessCommandLine();

    EXPECT_STREQ(ProjectName, "huntdat/areas/area1");
#ifdef _WIN32
    EXPECT_STREQ(g_Network.m_serverAddress, "localhost");
#endif
    EXPECT_EQ(TargetDino, 5 * 1024);
    EXPECT_EQ(WeaponPres, 3);
    EXPECT_EQ(OptDayNight, 2);
    EXPECT_FLOAT_EQ(PlayerX, 4.5f * 256.0f);
    EXPECT_FLOAT_EQ(PlayerZ, 6.5f * 256.0f);
    EXPECT_TRUE(LockLanding);
    EXPECT_FALSE(FULLSCREEN);
    EXPECT_FALSE(BORDERLESS);
    EXPECT_EQ(WinW, 800);
    EXPECT_EQ(WinH, 600);
    EXPECT_EQ(CurRes, 0);
    EXPECT_EQ(OptRes, 0);
    EXPECT_EQ(g_GameMode, GameMode::NightVision);
    EXPECT_TRUE(NightVisionMode);
    EXPECT_TRUE(RadarMode);
    EXPECT_FLOAT_EQ(ScoreMod_Camo, 1.1f);
    EXPECT_FLOAT_EQ(ScoreMod_Observer, 1.6f);
    EXPECT_TRUE(g_logMessages.empty());
}

TEST(CommandLineEntry, ProductionPathRejectsUnsafeStringsWithoutMutation)
{
    RestoreProcessArguments restoreArguments;
    ResetCommandLineState();
    CommandLineArguments arguments{
        "Carnivores1",
        std::string("prj=huntdat/areas/") + std::string(160, 'p'),
#ifdef _WIN32
        std::string("server=") + std::string(160, 's'),
#endif
    };
    arguments.Install();

    ProcessCommandLine();

    EXPECT_STREQ(ProjectName, "old-project");
#ifdef _WIN32
    EXPECT_STREQ(g_Network.m_serverAddress, "old-server");
#endif
    ASSERT_EQ(g_logMessages.size(), 1u + kServerDiagnosticCount);
    EXPECT_NE(g_logMessages[0].find("prj="), std::string::npos);
#ifdef _WIN32
#ifdef _WIN32
    EXPECT_NE(g_logMessages[1].find("server="), std::string::npos);
#endif
#endif
}

TEST(CommandLineEntry, ProductionPathAcceptsExactCapacityAndMixedCaseOptions)
{
    RestoreProcessArguments restoreArguments;
    ResetCommandLineState();
    const std::string project(127, 'p');
    const std::string server(127, 's');
    CommandLineArguments arguments{
        "Carnivores1",
        "PrJ=" + project,
#ifdef _WIN32
        "SeRvEr=" + server,
#endif
        "/ReS=800X600"};
    arguments.Install();

    ProcessCommandLine();

    EXPECT_STREQ(ProjectName, project.c_str());
#ifdef _WIN32
    EXPECT_STREQ(g_Network.m_serverAddress, server.c_str());
#endif
    EXPECT_EQ(WinW, 800);
    EXPECT_EQ(WinH, 600);
    EXPECT_EQ(CurRes, 0);
    EXPECT_EQ(OptRes, 0);
    EXPECT_TRUE(g_logMessages.empty());
}

TEST(CommandLineEntry, ProductionPathRejectsEmptyAndMalformedValues)
{
    RestoreProcessArguments restoreArguments;
    ResetCommandLineState();
    CommandLineArguments arguments{
        "Carnivores1",
        "prj=",
#ifdef _WIN32
        "server=",
#endif
        "/res=800x600junk"};
    arguments.Install();

    ProcessCommandLine();

    EXPECT_STREQ(ProjectName, "old-project");
#ifdef _WIN32
    EXPECT_STREQ(g_Network.m_serverAddress, "old-server");
#endif
    EXPECT_EQ(WinW, 640);
    EXPECT_EQ(WinH, 480);
    ASSERT_EQ(g_logMessages.size(), 2u + kServerDiagnosticCount);
    EXPECT_NE(g_logMessages[0].find("prj="), std::string::npos);
#ifdef _WIN32
#ifdef _WIN32
    EXPECT_NE(g_logMessages[1].find("server="), std::string::npos);
#endif
#endif
    EXPECT_NE(g_logMessages.back().find("res="), std::string::npos);
}

TEST(CommandLineEntry, ProductionPathIgnoresUnrelatedPrefixes)
{
    RestoreProcessArguments restoreArguments;
    ResetCommandLineState();
    CommandLineArguments arguments{"Carnivores1", "xprj=not-an-option", "xserver=not-an-option"};
    arguments.Install();

    ProcessCommandLine();

    EXPECT_STREQ(ProjectName, "old-project");
#ifdef _WIN32
    EXPECT_STREQ(g_Network.m_serverAddress, "old-server");
#endif
    EXPECT_TRUE(g_logMessages.empty());
}

TEST(CommandLineEntry, RejectsTrailingGarbageRangesAndSubstringFlags) {
    RestoreProcessArguments restoreArguments;
    ResetCommandLineState();
    Platform::SetArguments({"engine", "din=1024", "wep=-1", "dtm=3", "x=nan", "y=1junk", "-radar-extra"});
    ProcessCommandLine();
    EXPECT_EQ(TargetDino, 0); EXPECT_EQ(WeaponPres, 0); EXPECT_EQ(OptDayNight, 0);
    EXPECT_FALSE(LockLanding); EXPECT_FALSE(RadarMode);
    EXPECT_EQ(g_logMessages.size(), 5u);
}
